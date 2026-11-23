// Unique Vintage Processor — MORE COLOUR tape machines
#include "TapeMachine.h"

namespace uvp
{
namespace
{
    // Reference: published specs and measurements gathered for each machine class
    // (ATR-102 30 ips AES, Studer A80 24-track 15 ips 456, TASCAM 34B 15 ips NAB,
    //  consumer quarter-track 7.5 ips NAB, Type I cassette 120 us). hDrive (saturation) and the
    // core's reversible fraction c (residual hysteresis) are fitted together (harness
    // `calibtape2`) so a 1 kHz tone gives the machine's typical 3rd harmonic at 0 VU (-18 dBFS)
    // and at +6 VU: SILK 0.35/1.0 %, SOLID 0.6/1.8 %, HOME 0.8/2.2 %, GARAGE 1.2/3.0 %,
    // CASSETTE 1.8/5.0 % (3 % MOL at roughly +11/+9/+8/+6/+3 VU).
    const TapeModelDef kModels[kNumTapeModels] = {
        // name           ips    recZ   recP     core {Ms,a,alpha,k,c}       hDrive elec2   bump          dip            hp          lp            wear           cap            reel            rand     drift    scrape   az   drops  lived
        { "TAPE SILK",     30.0, 7700, 40000, { 1, 1, 0, 0.06, 0.977 }, 3.17, 0.004,  50, 1.2, 1.3,  110, -0.5, 1.4,  20, 0.70,  26000, 0.65,      0,  0.0,  0.00025, 10.5, 0.00012, 0.55, 0.00010, 0.00008, 0.00004,  0, 0.0,      false },
        { "TAPE SOLID",    15.0, 5200, 30000, { 1, 1, 0, 0.10, 0.966 }, 4.35, 0.008,  48, 1.8, 1.1,  105, -0.6, 1.3,  18, 0.70,  22000, 0.60,      0,  0.0,  0.00030,  7.2, 0.00018, 0.80, 0.00014, 0.00012, 0.00006,  0, 0.0,      false },
        { "TAPE HOME",     15.0, 4600, 26000, { 1, 1, 0, 0.14, 0.950 }, 4.97, 0.020,  68, 2.4, 1.0,  145, -0.8, 1.2,  30, 0.75,  19000, 0.60,  12000, -1.5,  0.00055,  6.4, 0.00045, 1.10, 0.00030, 0.00030, 0.00010,  5, 1.0 / 45, true  },
        { "TAPE GARAGE",    7.5, 3600, 18000, { 1, 1, 0, 0.18, 0.902 }, 5.95, 0.030,  55, 2.8, 0.9,  125, -1.0, 1.1,  32, 0.80,  14500, 0.65,   9000, -2.0,  0.00080,  5.2, 0.00070, 0.90, 0.00045, 0.00050, 0.00015, 10, 1.0 / 20, true  },
        { "TAPE CASSETTE", 1.875,1330,  6200, { 1, 1, 0, 0.22, 0.954 }, 9.34, 0.035,  62, 2.0, 0.9,  140, -0.8, 1.0,  38, 0.80,  12500, 0.70,   7000, -2.5,  0.00100,  6.3, 0.00090, 1.70, 0.00060, 0.00080, 0.00020, 22, 1.0 / 10, true  },
    };
} // namespace

const TapeModelDef& tapeModel (int index) noexcept { return kModels[std::max (0, std::min (kNumTapeModels - 1, index))]; }

void TapeMachine::prepare (double sampleRate, int numChannels, int maxBlockSize)
{
    fs = sampleRate;
    numCh = std::max (1, std::min (2, numChannels));
    maxBlock = std::max (1, maxBlockSize);
    maxOsLatency = 0;
    for (int i = 0; i <= kMaxOsIndex; ++i)
    {
        os[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numCh, (size_t) i, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[(size_t) i]->initProcessing ((size_t) maxBlock);
        osLatency[(size_t) i] = (int) std::lround (os[(size_t) i]->getLatencyInSamples());
        maxOsLatency = std::max (maxOsLatency, osLatency[(size_t) i]);
    }
    centreDelay = (int) std::lround (0.006 * fs);
    for (auto& c : chans)
    {
        c.delay.prepare (maxOsLatency + 2 * centreDelay + 16);
        c.dry.prepare (maxOsLatency + centreDelay + 8);
        c.dry.setDelay (maxOsLatency + centreDelay);
    }
    dryBuf.setSize (numCh, maxBlock, false, false, true);
    wowDelay.assign ((size_t) maxBlock, 0.0f);
    onRamp.reset (fs, 0.03, targetOn ? 1.0f : 0.0f);
    switchGain.reset (fs, 0.012, 1.0f);
    rng.seed (0x51A7C0DEu);
    model = requestedModel;
    osIdx = requestedOs;
    configure (model, osIdx);
    reset();
}

void TapeMachine::configure (int m, int osIndex)
{
    const auto& d = tapeModel (m);
    fsOs = fs * (double) (1 << osIndex);
    jaNorm = JilesAtherton::measureSmallSignalGain (d.core);
    for (auto& c : chans)
    {
        c.rec.setZeroPole (fsOs, d.recZeroHz, d.recPoleHz);
        c.recInv.setInverseOf (c.rec);
        c.ja.setParams (d.core);
        c.bump.setPeak (fs, d.bumpHz, d.bumpQ, d.bumpDb);
        c.dip.setPeak (fs, d.dipHz, d.dipQ, d.dipDb);
        c.hp.setHighPass (fs, d.hpHz, d.hpQ);
        if (d.lpHz < 0.45 * fs) c.lp.setLowPass (fs, d.lpHz, d.lpQ); else c.lp.setIdentity();
        if (d.wearShelfDb != 0.0) c.wear.setHighShelf (fs, d.wearShelfHz, 0.7, d.wearShelfDb); else c.wear.setIdentity();
    }
    scrapeBp.setCoefs (1, 0, -1, 1, 0, 0); // replaced below
    {
        // band-pass around 3 kHz for scrape flutter (RBJ constant 0 dB peak gain)
        const double w = kTwoPi * std::min (3000.0, 0.4 * fs) / fs, q = 2.0, al = std::sin (w) / (2 * q), cw = std::cos (w);
        scrapeBp.setCoefs (al, 0, -al, 1 + al, -2 * cw, 1 - al);
    }
}

void TapeMachine::reset()
{
    for (auto& o : os) if (o) o->reset();
    for (auto& c : chans)
    {
        c.rec.reset(); c.recInv.reset(); c.ja.reset();
        c.bump.reset(); c.dip.reset(); c.hp.reset(); c.lp.reset(); c.wear.reset();
        c.dropLp.reset(); c.delay.reset(); c.dry.reset();
    }
    capPhase = reelPhase = azPhase = 0;
    randState1 = randState2 = driftState = driftState2 = randInt = driftInt = scrapeInt = 0;
    scrapeBp.reset();
    dropActive = false;
    dropTimer = 2.0;
    currentDriveDb = targetDriveDb;
    onRamp.snap (targetOn ? 1.0f : 0.0f);
    switchGain.snap (1.0f);
    switchState = 0;
}

void TapeMachine::updateTransport (int n) noexcept
{
    const auto& d = tapeModel (model);
    const double dt = 1.0 / fs;
    // one-pole coefficients
    const double aRand = 1.0 - std::exp (-kTwoPi * 22.0 * dt);   // random flutter band ~2..22 Hz
    const double aRandHp = 1.0 - std::exp (-kTwoPi * 2.0 * dt);
    const double aDrift = 1.0 - std::exp (-kTwoPi * 0.3 * dt);   // slow wow drift
    const double leak = dt / 4.0;
    // white-noise normalisation for the filtered bands (empirical, uniform noise in)
    const double randGain = 1.0 / std::sqrt (aRand * 0.5 / 3.0 + 1e-12);
    const double driftGain = 1.0 / std::sqrt (aDrift * 0.5 / 3.0 + 1e-12);
    const double scrapeGain = 1.0 / std::sqrt ((kTwoPi * 3000.0 / 2.0 / fs) / 3.0 + 1e-12);

    for (int i = 0; i < n; ++i)
    {
        // slow frequency jitter of the rotating parts
        capHzJit += 0.0005 * rng.nextBipolar() - 0.0005 * capHzJit;
        reelHzJit += 0.0005 * rng.nextBipolar() - 0.0005 * reelHzJit;
        const double fCap = d.capHz * (1.0 + 0.6 * capHzJit), fReel = d.reelHz * (1.0 + 0.6 * reelHzJit);
        capPhase += kTwoPi * fCap * dt;
        reelPhase += kTwoPi * fReel * dt;
        if (capPhase > kTwoPi) capPhase -= kTwoPi;
        if (reelPhase > kTwoPi) reelPhase -= kTwoPi;
        double offs = -d.capPeak / (kTwoPi * fCap) * std::cos (capPhase) - d.reelPeak / (kTwoPi * fReel) * std::cos (reelPhase);

        // random flutter: band-limited noise speed deviation, integrated to time offset
        const double w1 = rng.nextBipolar();
        randState1 += aRand * (w1 - randState1);
        randState2 += aRandHp * (randState1 - randState2);
        const double sRand = (randState1 - randState2) * randGain * d.randRms;
        randInt += sRand * dt - randInt * leak;
        // drift (wow)
        const double w2 = rng.nextBipolar();
        driftState += aDrift * (w2 - driftState);
        driftState2 += aDrift * (driftState - driftState2);
        const double sDrift = driftState2 * driftGain * 1.6 * d.driftRms;
        driftInt += sDrift * dt - driftInt * leak;
        // scrape flutter (kHz region modulation noise)
        const double sScrape = scrapeBp.process (rng.nextBipolar()) * scrapeGain * d.scrapeRms;
        scrapeInt += sScrape * dt - scrapeInt * (dt / 0.01);

        offs += randInt + driftInt + scrapeInt;
        const double lim = 0.7 * centreDelay / fs;
        offs = std::max (-lim, std::min (lim, offs));
        wowDelay[(size_t) i] = (float) (offs * fs);
    }
}

void TapeMachine::process (float* const* ch, int nCh, int n) noexcept
{
    nCh = std::min (nCh, numCh);
    if (n <= 0) return;

    if ((requestedModel != model || requestedOs != osIdx) && switchState == 0)
    {
        switchState = 1;
        switchGain.setTarget (0.0f);
    }
    if (switchState == 1 && ! switchGain.isRamping() && switchGain.getCurrent() <= 0.0f)
    {
        model = requestedModel;
        osIdx = requestedOs;
        configure (model, osIdx);
        os[(size_t) osIdx]->reset();
        for (auto& c : chans) { c.rec.reset(); c.recInv.reset(); c.ja.reset(); }
        switchState = 0;
        switchGain.setTarget (1.0f);
    }
    onRamp.setTarget (targetOn ? 1.0f : 0.0f);

    const bool fullyOff = ! targetOn && ! onRamp.isRamping() && onRamp.getCurrent() <= 0.0f;
    if (fullyOff)
    {
        for (int c = 0; c < nCh; ++c)
        {
            auto& C = chans[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                C.delay.push (x);   // keep the transport line primed
                ch[c][i] = C.dry.process (x);
            }
        }
        currentDriveDb = targetDriveDb;
        return;
    }

    const auto& d = tapeModel (model);
    for (int c = 0; c < nCh; ++c) dryBuf.copyFrom (c, 0, ch[c], n);

    // tape drive trim into the machine (compensated on the way out)
    const float g0 = dbToGain (currentDriveDb), g1 = dbToGain (targetDriveDb);
    for (int c = 0; c < nCh; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] *= g0 + (g1 - g0) * (float) (i + 1) / (float) n;

    juce::dsp::AudioBlock<float> block (ch, (size_t) nCh, (size_t) n);
    auto up = os[(size_t) osIdx]->processSamplesUp (block);
    const int nUp = (int) up.getNumSamples();
    const double hd = d.hDrive, invNorm = 1.0 / (jaNorm * d.hDrive);
    for (int c = 0; c < nCh; ++c)
    {
        auto& C = chans[(size_t) c];
        float* s = up.getChannelPointer ((size_t) c);
        for (int j = 0; j < nUp; ++j)
        {
            const double h = C.rec.process ((double) s[j]) * hd;
            const double m = C.ja.process (h);
            double y = m * invNorm;
            y += d.elec2 * y * y;
            s[j] = (float) C.recInv.process (y);
        }
    }
    os[(size_t) osIdx]->processSamplesDown (block);

    updateTransport (n);
    if (! transportOn) std::fill (wowDelay.begin(), wowDelay.begin() + n, 0.0f);
    const double pad = (double) (maxOsLatency - osLatency[(size_t) osIdx]);
    const double dtv = 1.0 / fs;
    const double azBase = d.azimuthUs * 1.0e-6 * fs;
    const double v = d.ips * 0.0254;
    for (int i = 0; i < n; ++i)
    {
        const float on = onRamp.next() * switchGain.next();
        const float gComp = 1.0f / (g0 + (g1 - g0) * (float) (i + 1) / (float) n);

        // lived-in dropouts (shared event, slightly different per track)
        float env = 0.0f;
        if (d.livedIn && transportOn)
        {
            if (! dropActive)
            {
                dropTimer -= dtv;
                if (dropTimer <= 0.0)
                {
                    const double u1 = rng.nextUnipolar(), u2 = rng.nextUnipolar(), u3 = rng.nextUnipolar();
                    dropLen = 0.02 + 0.10 * u1;
                    dropDepthUm = 0.25 + 0.95 * u2 * u2;
                    dropRatioR = 0.6 + 0.4 * u3;
                    dropPos = 0.0;
                    dropActive = true;
                    const double att8k = 54.6 * dropDepthUm * 1.0e-6 * 8000.0 / v; // spacing loss at 8 kHz (dB)
                    const double fc = att8k > 0.05 ? 8000.0 / std::sqrt (std::pow (10.0, att8k / 10.0) - 1.0) : 0.4 * fs;
                    for (int c = 0; c < nCh; ++c)
                        chans[(size_t) c].dropLp.setLowPass (fs, std::max (300.0, std::min (0.4 * fs, c == 0 ? fc : fc / std::max (0.3, dropRatioR))));
                }
            }
            if (dropActive)
            {
                const double ph = dropPos / dropLen;
                env = (float) std::pow (std::sin (kPi * ph), 2.0);
                dropPos += dtv;
                if (dropPos >= dropLen)
                {
                    dropActive = false;
                    dropTimer = -std::log (std::max (1.0e-6, (double) rng.nextUnipolar())) / std::max (1.0e-3, d.dropoutsPerSec);
                }
            }
        }
        const double az = (d.livedIn && transportOn) ? azBase * (1.0 + 0.3 * std::sin (azPhase)) : 0.0;
        azPhase += kTwoPi * 0.13 * dtv;
        if (azPhase > kTwoPi) azPhase -= kTwoPi;

        for (int c = 0; c < nCh; ++c)
        {
            auto& C = chans[(size_t) c];
            double y = ch[c][i];
            y = C.wear.process (C.lp.process (C.hp.process (C.dip.process (C.bump.process (y)))));
            const double lpY = C.dropLp.process (y);
            if (env > 0.0f)
            {
                const double depth = c == 0 ? 1.0 : dropRatioR;
                const double e = env * depth;
                y = (y + e * (lpY - y)) * std::pow (10.0, -0.6 * dropDepthUm * e / 20.0);
            }
            C.delay.push ((float) y);
            const double side = nCh > 1 ? (c == 0 ? 0.5 : -0.5) : 0.0;
            const double rd = (double) centreDelay + pad + (double) wowDelay[(size_t) i] + side * az;
            const float wet = C.delay.read (rd) * gComp;
            const float dry = C.dry.process (dryBuf.getSample (c, i));
            ch[c][i] = dry + on * (wet - dry);
        }
    }
    currentDriveDb = targetDriveDb;
}

double TapeMachine::measureThd3 (int m, double sr, double levelDbfs)
{
    return measureThd3 (tapeModel (m), tapeModel (m).hDrive, sr, levelDbfs);
}

double TapeMachine::measureThd3 (const TapeModelDef& d, double hDrive, double sr, double levelDbfs)
{
    // offline: drive a 1 kHz tone through the magnetic core path only and return H3/H1
    JilesAtherton ja;
    ja.setParams (d.core);
    ja.reset();
    const double norm = JilesAtherton::measureSmallSignalGain (d.core);
    const double amp = std::pow (10.0, levelDbfs / 20.0);
    const double fsT = sr * 4.0, f0 = 1000.0;
    FirstOrder rec, inv;
    rec.setZeroPole (fsT, d.recZeroHz, d.recPoleHz);
    inv.setInverseOf (rec);
    const int per = (int) std::lround (fsT / f0), warm = per * 20, meas = per * 40;
    double r1 = 0, i1 = 0, r3 = 0, i3 = 0;
    for (int i = 0; i < warm + meas; ++i)
    {
        const double ph = kTwoPi * f0 * i / fsT;
        const double h = rec.process (amp * std::sin (ph)) * hDrive;
        const double y = inv.process (ja.process (h) / (norm * hDrive));
        if (i >= warm)
        {
            r1 += y * std::sin (ph); i1 += y * std::cos (ph);
            r3 += y * std::sin (3 * ph); i3 += y * std::cos (3 * ph);
        }
    }
    return std::sqrt (r3 * r3 + i3 * i3) / std::max (1e-30, std::sqrt (r1 * r1 + i1 * i1));
}

} // namespace uvp
