// Unique Vintage Processor — offline measurement harness
// usage: UVPHarness <command> [args]
//   null | split | harm | supp | tape | cpu | render <outdir> | calibrate <AutoGainTables.h path>
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/VintageEngine.h"
#include "dsp/AutoGainTables.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <random>

using namespace uvp;

namespace
{
constexpr double kFs = 48000.0;

struct Sig
{
    std::vector<float> l, r;
    explicit Sig (size_t n = 0) : l (n, 0.0f), r (n, 0.0f) {}
    size_t size() const { return l.size(); }
};

double goertzelAmp (const float* x, size_t n, double f, double fs)
{
    // windowed (Hann) single-bin DFT -> amplitude of a sinusoid at f
    double re = 0, im = 0, ws = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) n);
        const double ph = kTwoPi * f * (double) i / fs;
        re += w * x[i] * std::cos (ph);
        im += w * x[i] * std::sin (ph);
        ws += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / ws;
}

double rmsDb (const float* x, size_t n)
{
    double s = 0;
    for (size_t i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double) n + 1e-30);
}

Sig pinkNoise (size_t n, double rmsDbfs, uint32_t seed = 1)
{
    // Paul Kellet's refined pink filter on white noise, independent L/R
    Sig s (n);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        auto& v = ch == 0 ? s.l : s.r;
        for (size_t i = 0; i < n; ++i)
        {
            const float w = u (rng);
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
            v[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
        }
        const double cur = rmsDb (v.data(), n);
        const float g = (float) std::pow (10.0, (rmsDbfs - cur) / 20.0);
        for (auto& x : v) x *= g;
    }
    return s;
}

/** A synthetic "bright mix": pink bed, kick/bass, snare-ish bursts, sibilant bursts, resonant cymbal ring. */
Sig brightMix (size_t n, uint32_t seed = 7)
{
    Sig s = pinkNoise (n, -24.0, seed);
    std::mt19937 rng (seed + 11);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    juce::dsp::IIR::Filter<float> bp1, bp2, bp3, hp;
    bp1.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (kFs, 5600.0, 18.0);
    bp2.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (kFs, 8400.0, 25.0);
    bp3.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (kFs, 3300.0, 12.0);
    hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (kFs, 6000.0, 0.7);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / kFs;
        const double beat = std::fmod (t, 0.5);
        // kick + bass
        double y = 0.35 * std::exp (-beat * 18.0) * std::sin (kTwoPi * (55.0 + 60.0 * std::exp (-beat * 40.0)) * beat);
        y += 0.12 * std::sin (kTwoPi * 55.0 * t) * (0.6 + 0.4 * std::sin (kTwoPi * 0.5 * t));
        // hats / cymbal with ringing resonances
        const double hb = std::fmod (t, 0.125);
        const float nz = u (rng);
        const float ring = bp1.processSample (nz) * 1.4f + bp2.processSample (nz) * 1.1f + bp3.processSample (nz) * 0.8f;
        y += 0.10 * std::exp (-hb * 35.0) * hp.processSample (nz) + 0.06 * ring;
        // sibilant bursts every 1.5 s
        const double sb = std::fmod (t, 1.5);
        if (sb < 0.12) y += 0.07 * std::sin (kPi * sb / 0.12) * bp1.processSample (u (rng)) * 3.0;
        s.l[i] += (float) y;
        s.r[i] += (float) (y * 0.97);
    }
    return s;
}

Sig runEngine (const Sig& in, const EngineParams& p, int block = 512, int* latencyOut = nullptr, VintageEngine* external = nullptr)
{
    VintageEngine local;
    VintageEngine& e = external ? *external : local;
    e.setParams (p);
    e.prepare (kFs, block, 2);
    e.setParams (p);
    if (latencyOut) *latencyOut = e.getLatencySamples();
    Sig out = in;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        float* ch[2] = { out.l.data() + pos, out.r.data() + pos };
        e.process (ch, 2, n);
    }
    return out;
}

void writeWav (const juce::File& f, const Sig& s)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::unique_ptr<juce::OutputStream> (f.createOutputStream().release());
    if (stream == nullptr) return;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (kFs).withNumChannels (2).withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, opts);
    if (writer == nullptr) return;
    const float* ch[2] = { s.l.data(), s.r.data() };
    writer->writeFromFloatArrays (ch, 2, (int) s.size());
}

// K-weighting (BS.1770) loudness in dB (ungated, stereo)
double kLoudness (const Sig& s, size_t start = 0)
{
    juce::dsp::IIR::Filter<double> f1[2], f2[2];
    for (int c = 0; c < 2; ++c)
    {
        f1[c].coefficients = new juce::dsp::IIR::Coefficients<double> (1.53512485958697, -2.69169618940638, 1.19839281085285,
                                                                        1.0, -1.69065929318241, 0.73248077421585);
        f2[c].coefficients = new juce::dsp::IIR::Coefficients<double> (1.0, -2.0, 1.0, 1.0, -1.99004745483398, 0.99007225036621);
    }
    double acc = 0;
    size_t cnt = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double a = f2[0].processSample (f1[0].processSample (s.l[i]));
        const double b = f2[1].processSample (f1[1].processSample (s.r[i]));
        if (i >= start) { acc += a * a + b * b; ++cnt; }
    }
    return 10.0 * std::log10 (acc / (double) std::max<size_t> (1, cnt) + 1e-30);
}

//==============================================================================
int cmdNull()
{
    EngineParams p; // defaults: amount 0, tape off
    const Sig in = pinkNoise ((size_t) (kFs * 3), -18.0);
    int lat = 0;
    const Sig out = runEngine (in, p, 512, &lat);
    double maxDiff = 0, err = 0;
    for (size_t i = (size_t) lat; i < in.size(); ++i)
    {
        const double d1 = std::abs ((double) out.l[i] - in.l[i - (size_t) lat]);
        const double d2 = std::abs ((double) out.r[i] - in.r[i - (size_t) lat]);
        maxDiff = std::max ({ maxDiff, d1, d2 });
        err += d1 * d1 + d2 * d2;
    }
    std::printf ("latency %d samples (%.1f ms)\nnull test at amount 0: max |diff| = %.3g, residual = %.1f dBFS\n", lat, 1000.0 * lat / kFs,
                 maxDiff, 10 * std::log10 (err / (2.0 * (double) (in.size() - (size_t) lat)) + 1e-30));
    return maxDiff == 0.0 ? 0 : 1;
}

int cmdSplit()
{
    LinearPhaseSplit sp;
    sp.prepare (kFs, 1);
    sp.setCutoff (4000.0f, true);
    sp.prepare (kFs, 1);
    const int lat = sp.getLatencySamples();
    std::printf ("split latency %d\n", lat);
    for (double f : { 500.0, 2000.0, 3000.0, 4000.0, 5000.0, 8000.0, 16000.0 })
    {
        sp.reset();
        const size_t n = (size_t) kFs;
        std::vector<float> x (n), lo (n), hi (n);
        for (size_t i = 0; i < n; ++i) x[i] = (float) (0.5 * std::sin (kTwoPi * f * (double) i / kFs));
        const float* in[1] = { x.data() };
        float* l[1] = { lo.data() };
        float* h[1] = { hi.data() };
        sp.process (in, l, h, 1, (int) n);
        const size_t st = (size_t) lat + 4000;
        const double al = goertzelAmp (lo.data() + st, n - st, f, kFs) / 0.5, ah = goertzelAmp (hi.data() + st, n - st, f, kFs) / 0.5;
        double recon = 0;
        for (size_t i = st; i < n; ++i) recon = std::max (recon, (double) std::abs (lo[i] + hi[i] - x[i - (size_t) lat]));
        std::printf ("  %6.0f Hz: low %6.2f dB  high %6.2f dB  |low+high - x| max %.2g\n", f, 20 * std::log10 (al + 1e-12),
                     20 * std::log10 (ah + 1e-12), recon);
    }
    return 0;
}

int cmdHarm()
{
    std::printf ("Harmonic stage alone, 5 kHz tone at -20 dBFS (typical HF band level), 4x oversampling\n");
    const char* names[] = { "EVEN", "ODD", "HECTIC" };
    for (int mode = 0; mode < 3; ++mode)
    {
        std::printf ("%s\n", names[mode]);
        for (float amt : { 1.0f, 2.0f, 4.0f, 6.0f, 8.0f, 9.0f, 10.0f, 11.0f })
        {
            HarmonicStage hs;
            hs.setOversampling (2);
            hs.setParams (amt, (HarmonicStage::Mode) mode);
            hs.prepare (kFs, 2, 512);
            hs.setParams (amt, (HarmonicStage::Mode) mode);
            const size_t n = (size_t) (kFs * 1.0);
            std::vector<float> l (n), r (n);
            const double f0 = 5000.0, a = 0.1;
            for (size_t i = 0; i < n; ++i) l[i] = r[i] = (float) (a * std::sin (kTwoPi * f0 * (double) i / kFs));
            for (size_t pos = 0; pos < n; pos += 512)
            {
                float* ch[2] = { l.data() + pos, r.data() + pos };
                hs.process (ch, 2, (int) std::min<size_t> (512, n - pos));
            }
            const size_t st = n / 2;
            const double h1 = goertzelAmp (l.data() + st, n - st, f0, kFs);
            std::printf ("  amount %4.1f: H1 %+6.2f dB", amt, 20 * std::log10 (h1 / a));
            for (int k = 2; k <= 4; ++k)
            {
                const double hk = goertzelAmp (l.data() + st, n - st, f0 * k, kFs);
                std::printf ("  H%d %7.1f dB", k, 20 * std::log10 (hk / h1 + 1e-12));
            }
            // intermodulation-ish: content below the tone (difference products / DC leak)
            const double sub = goertzelAmp (l.data() + st, n - st, 1000.0, kFs);
            std::printf ("  (1k %6.1f)\n", 20 * std::log10 (sub / h1 + 1e-12));
        }
    }
    return 0;
}

int cmdSupp()
{
    std::printf ("Suppressor alone on the bright mix (resonances at 3.3k, 5.6k, 8.4k), HPF 4 kHz\n");
    const Sig mixSig = brightMix ((size_t) (kFs * 4));
    for (float q : { 0.3f, 2.83f, 20.0f })
    {
        for (float amt : { 2.0f, 4.0f, 6.0f, 8.0f, 11.0f })
        {
            SpectralSuppressor s;
            s.setParams ({ amt, q, 4000.0f, true });
            s.prepare (kFs, 2);
            s.setParams ({ amt, q, 4000.0f, true });
            Sig x = mixSig;
            for (size_t pos = 0; pos < x.size(); pos += 512)
            {
                float* ch[2] = { x.l.data() + pos, x.r.data() + pos };
                s.process (ch, 2, (int) std::min<size_t> (512, x.size() - pos));
            }
            const int lat = s.getLatencySamples();
            // band energy change via short band-pass measurements
            auto bandDb = [&] (const Sig& sig, double f, size_t offset)
            {
                juce::dsp::IIR::Filter<float> bp;
                bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (kFs, f, 30.0);
                double e = 0;
                for (size_t i = 0; i + offset < sig.size(); ++i)
                {
                    const float v = bp.processSample (sig.l[i + offset]);
                    if (i > (size_t) kFs / 2) e += (double) v * v;
                }
                return 10 * std::log10 (e + 1e-30);
            };
            std::printf ("  Q %5.2f amount %4.1f:", q, amt);
            for (double f : { 1000.0, 3300.0, 5600.0, 7000.0, 8400.0, 12000.0 })
                std::printf ("  %5.0fHz %+5.1f", f, bandDb (x, f, (size_t) lat) - bandDb (mixSig, f, 0));
            std::printf ("\n");
        }
    }
    return 0;
}

int cmdSupp2()
{
    const double lvl = juce::String (std::getenv ("UVP_LEVEL") ? std::getenv ("UVP_LEVEL") : "-24").getDoubleValue();
    std::printf ("Suppressor: pink noise (%.0f dBFS RMS) + steady tones at 5.6k (+19 dB/bin) and 8.4k (+13 dB/bin), HPF 4 kHz\n", lvl);
    const size_t n = (size_t) (kFs * 4);
    Sig base = pinkNoise (n, lvl, 21);
    const double tg = std::pow (10.0, (lvl + 24.0) / 20.0);
    // local bed level per Hz-ish: set tones relative to noise in a 1/3-octave band
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / kFs;
        const float tone = (float) (tg * (0.020 * std::sin (kTwoPi * 5600.0 * t) + 0.010 * std::sin (kTwoPi * 8400.0 * t)));
        base.l[i] += tone; base.r[i] += tone;
    }
    auto bandDb = [] (const Sig& sig, double f, size_t offset, double q)
    {
        juce::dsp::IIR::Filter<float> bp;
        bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (kFs, f, q);
        double e = 0;
        for (size_t i = 0; i + offset < sig.size(); ++i)
        {
            const float v = bp.processSample (sig.l[i + offset]);
            if (i > (size_t) kFs) e += (double) v * v;
        }
        return 10 * std::log10 (e + 1e-30);
    };
    for (float q : { 0.3f, 1.5f, 6.0f, 15.0f, 40.0f })
        for (float amt : { 2.0f, 4.0f, 6.0f, 8.0f, 11.0f })
        {
            SpectralSuppressor s;
            s.setParams ({ amt, q, 4000.0f, true });
            s.prepare (kFs, 2);
            s.setParams ({ amt, q, 4000.0f, true });
            Sig x = base;
            for (size_t pos = 0; pos < x.size(); pos += 512)
            {
                float* ch[2] = { x.l.data() + pos, x.r.data() + pos };
                s.process (ch, 2, (int) std::min<size_t> (512, x.size() - pos));
            }
            const size_t lat = (size_t) s.getLatencySamples();
            const double t1 = goertzelAmp (x.l.data() + lat + (size_t) kFs, n - lat - (size_t) kFs, 5600.0, kFs) / goertzelAmp (base.l.data() + (size_t) kFs, n - lat - (size_t) kFs, 5600.0, kFs);
            const double t2 = goertzelAmp (x.l.data() + lat + (size_t) kFs, n - lat - (size_t) kFs, 8400.0, kFs) / goertzelAmp (base.l.data() + (size_t) kFs, n - lat - (size_t) kFs, 8400.0, kFs);
            std::printf ("  Q %5.2f amt %4.1f: tone5.6k %+5.1f  tone8.4k %+5.1f  noise@4.8k %+5.1f  @7k %+5.1f  @11k %+5.1f  @2k %+5.1f  meter %.1f\n", q, amt,
                         20 * std::log10 (t1), 20 * std::log10 (t2),
                         bandDb (x, 4800, lat, 25) - bandDb (base, 4800, 0, 25), bandDb (x, 7000, lat, 25) - bandDb (base, 7000, 0, 25),
                         bandDb (x, 11000, lat, 25) - bandDb (base, 11000, 0, 25), bandDb (x, 2000, lat, 25) - bandDb (base, 2000, 0, 25), s.getMeterDb());
        }
    return 0;
}

int cmdSuppDbg()
{
    const size_t n = (size_t) (kFs * 2);
    Sig base = pinkNoise (n, -24.0, 21);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / kFs;
        const float tone = (float) (0.020 * std::sin (kTwoPi * 5600.0 * t) + 0.010 * std::sin (kTwoPi * 8400.0 * t));
        base.l[i] += tone; base.r[i] += tone;
    }
    for (float q : { 6.0f, 40.0f })
        for (float amt : { 4.0f, 8.0f })
        {
            SpectralSuppressor s;
            s.setParams ({ amt, q, 4000.0f, true });
            s.prepare (kFs, 2);
            s.setParams ({ amt, q, 4000.0f, true });
            Sig x = base;
            for (size_t pos = 0; pos < x.size(); pos += 512)
            {
                float* ch[2] = { x.l.data() + pos, x.r.data() + pos };
                s.process (ch, 2, (int) std::min<size_t> (512, x.size() - pos));
            }
            std::printf ("Q %.2f amt %.1f\n", q, amt);
            for (double f : { 4800.0, 5500.0, 5600.0, 5700.0, 7000.0, 8400.0, 11000.0, 15000.0 })
            {
                float S, L, gr;
                s.debugBin ((int) std::lround (f / s.getBinHz()), S, L, gr);
                std::printf ("   %6.0f Hz: spike %+6.1f dB  loud %+6.1f dB  gr %5.1f dB\n", f, S, L, gr);
            }
        }
    return 0;
}

int cmdTape()
{
    for (int m = 0; m < kNumTapeModels; ++m)
    {
        const auto& d = tapeModel (m);
        std::printf ("%-14s THD3 @0VU(-18dBFS) %.2f%%  @+6VU %.2f%%  @+12VU %.2f%%\n", d.name,
                     100 * TapeMachine::measureThd3 (m, kFs, -18.0), 100 * TapeMachine::measureThd3 (m, kFs, -12.0),
                     100 * TapeMachine::measureThd3 (m, kFs, -6.0));
        // small-signal frequency response at -20 VU through the full tape path
        std::printf ("   response @-20VU:");
        for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 150.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0 })
        {
            TapeMachine t;
            t.setOversampling (2);
            t.setParams (true, m, 0.0f);
            t.prepare (kFs, 2, 512);
            t.setParams (true, m, 0.0f);
            t.setTransportEnabled (false);
            const size_t n = (size_t) (kFs * 1.2);
            std::vector<float> l (n), r (n);
            const double a = std::pow (10.0, -38.0 / 20.0);
            for (size_t i = 0; i < n; ++i) l[i] = r[i] = (float) (a * std::sin (kTwoPi * f * (double) i / kFs));
            for (size_t pos = 0; pos < n; pos += 512)
            {
                float* ch[2] = { l.data() + pos, r.data() + pos };
                t.process (ch, 2, (int) std::min<size_t> (512, n - pos));
            }
            const size_t st = n / 3;
            std::printf (" %.0f:%+.1f", f, 20 * std::log10 (goertzelAmp (l.data() + st, n - st, f, kFs) / a));
        }
        std::printf ("\n");
    }
    return 0;
}

int cmdCalibTape()
{
    // bisect each machine's head drive so a 1 kHz tone at 0 VU (-18 dBFS) gives its target H3
    const double target[kNumTapeModels] = { 0.0035, 0.0060, 0.0080, 0.0120, 0.0180 };
    for (int m = 0; m < kNumTapeModels; ++m)
    {
        const auto& d = tapeModel (m);
        double lo = 0.2, hi = 40.0;
        for (int it = 0; it < 40; ++it)
        {
            const double mid = std::sqrt (lo * hi);
            (TapeMachine::measureThd3 (d, mid, kFs, -18.0) < target[m] ? lo : hi) = mid;
        }
        const double hd = std::sqrt (lo * hi);
        std::printf ("%-14s hDrive %.3f  -> THD3 0VU %.3f%%  +6VU %.2f%%  +12VU %.2f%%\n", d.name, hd,
                     100 * TapeMachine::measureThd3 (d, hd, kFs, -18.0), 100 * TapeMachine::measureThd3 (d, hd, kFs, -12.0),
                     100 * TapeMachine::measureThd3 (d, hd, kFs, -6.0));
    }
    return 0;
}

int cmdCalibTape2()
{
    // two-point fit per machine: head drive (saturation) and reversible fraction c (hysteresis)
    // so THD3 at 0 VU and at +6 VU both land on the machine's targets
    const double t0[kNumTapeModels] = { 0.0035, 0.0060, 0.0080, 0.0120, 0.0180 };
    const double t6[kNumTapeModels] = { 0.0102, 0.0176, 0.0216, 0.0300, 0.0500 };
    for (int m = 0; m < kNumTapeModels; ++m)
    {
        TapeModelDef d = tapeModel (m);
        auto fitDrive = [&] (double c) {
            d.core.c = c;
            double lo = 0.05, hi = 60.0;
            for (int it = 0; it < 34; ++it)
            {
                const double mid = std::sqrt (lo * hi);
                (TapeMachine::measureThd3 (d, mid, kFs, -18.0) < t0[m] ? lo : hi) = mid;
            }
            return std::sqrt (lo * hi);
        };
        const double ratioT = t6[m] / t0[m];
        double cLo = m < 3 ? 0.95 : 0.85, cHi = 0.995;
        for (int it = 0; it < 24; ++it)
        {
            const double cm = 0.5 * (cLo + cHi);
            const double hd = fitDrive (cm);
            (TapeMachine::measureThd3 (d, hd, kFs, -12.0) / t0[m] < ratioT ? cLo : cHi) = cm;
        }
        const double c = 0.5 * (cLo + cHi), hd = fitDrive (c);
        std::printf ("%-14s c %.4f hDrive %.3f | THD3 0VU %.3f%% +6VU %.3f%% +12VU %.2f%% | monotonic check %.3f%% < %.3f%% < %.3f%%\n",
                     d.name, c, hd, 100 * TapeMachine::measureThd3 (d, hd, kFs, -18.0), 100 * TapeMachine::measureThd3 (d, hd, kFs, -12.0),
                     100 * TapeMachine::measureThd3 (d, hd, kFs, -6.0), 100 * TapeMachine::measureThd3 (d, hd * 0.8, kFs, -18.0),
                     100 * TapeMachine::measureThd3 (d, hd, kFs, -18.0), 100 * TapeMachine::measureThd3 (d, hd * 1.25, kFs, -18.0));
    }
    return 0;
}

int cmdDisplay()
{
    // what the analyser frame reads for a -20 dBFS 1 kHz sine and for -20 dBFS RMS pink noise
    for (int kind = 0; kind < 2; ++kind)
    {
        const size_t n = (size_t) (kFs * 2);
        Sig in (n);
        if (kind == 0)
            for (size_t i = 0; i < n; ++i) in.l[i] = in.r[i] = (float) (0.1 * std::sin (kTwoPi * 1000.0 * (double) i / kFs));
        else
            in = pinkNoise (n, -20.0);
        EngineParams p;
        VintageEngine e;
        runEngine (in, p, 512, nullptr, &e);
        const DisplayFrame* f = nullptr;
        e.getDisplay().read (f);
        std::printf ("%s:\n", kind == 0 ? "sine 1 kHz -20 dBFS" : "pink -20 dBFS RMS");
        for (int pt = 0; pt < DisplayFrame::kPoints; pt += 16)
            std::printf ("  %7.0f Hz  in %6.1f  out %6.1f  gr %4.1f\n", VintageEngine::displayFreqForPoint (pt), f->inDb[pt], f->outDb[pt], f->grDb[pt]);
        int pk = 0;
        for (int pt = 0; pt < DisplayFrame::kPoints; ++pt) if (f->inDb[pt] > f->inDb[pk]) pk = pt;
        std::printf ("  peak in at %.0f Hz: in %.1f out %.1f\n", VintageEngine::displayFreqForPoint (pk), f->inDb[pk], f->outDb[pk]);
    }
    return 0;
}

int cmdAgCheck()
{
    // loudness match with AG on, on a programme the tables were NOT calibrated on (different seed)
    const Sig ref = brightMix ((size_t) (kFs * 3), 11);
    std::printf ("AG check (K-weighted loudness out - in, dB):\n");
    for (int mode = 0; mode < 3; ++mode)
        for (float in : { -12.0f, 0.0f, 12.0f })
        {
            std::printf ("  mode %d input %+5.1f:", mode, in);
            for (float a : { 2.0f, 4.0f, 6.0f, 8.0f, 9.5f, 11.0f })
            {
                EngineParams p;
                p.amount = a; p.mode = mode; p.inputDb = in; p.autoGain = true; p.osIndex = 2;
                int lat = 0;
                const Sig o = runEngine (ref, p, 512, &lat);
                Sig al (ref.size());
                for (size_t i = 0; i + (size_t) lat < o.size(); ++i) { al.l[i] = o.l[i + (size_t) lat]; al.r[i] = o.r[i + (size_t) lat]; }
                std::printf ("  a%-4.1f %+5.2f", a, kLoudness (al, (size_t) kFs / 2) - kLoudness (ref, (size_t) kFs / 2));
            }
            std::printf ("\n");
        }
    for (int m = 0; m < kNumTapeModels; ++m)
    {
        std::printf ("  tape %d:", m);
        for (float d : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            EngineParams p;
            p.tapeOn = true; p.tapeModel = m; p.tapeDriveDb = d; p.autoGain = true; p.osIndex = 2;
            int lat = 0;
            const Sig o = runEngine (ref, p, 512, &lat);
            Sig al (ref.size());
            for (size_t i = 0; i + (size_t) lat < o.size(); ++i) { al.l[i] = o.l[i + (size_t) lat]; al.r[i] = o.r[i + (size_t) lat]; }
            std::printf ("  d%+5.1f %+5.2f", d, kLoudness (al, (size_t) kFs / 2) - kLoudness (ref, (size_t) kFs / 2));
        }
        std::printf ("\n");
    }
    return 0;
}

int cmdCpu()
{
    const Sig in = brightMix ((size_t) (kFs * 6));
    const char* osNames[] = { "1x", "2x", "4x", "8x", "16x" };
    std::printf ("CPU (single core, stereo 48 kHz, block 512): %% of real time\n");
    for (int mode = 0; mode < 3; ++mode)
        for (int tapeOn = 0; tapeOn < 2; ++tapeOn)
        {
            std::printf ("  mode %d tape %d:", mode, tapeOn);
            for (int os = 0; os < 5; ++os)
            {
                EngineParams p;
                p.amount = 6.0f; p.mode = mode; p.osIndex = os; p.tapeOn = tapeOn; p.tapeModel = 4; p.hpfHz = 4000.0f;
                VintageEngine e;
                const auto t0 = std::chrono::high_resolution_clock::now();
                runEngine (in, p, 512, nullptr, &e);
                const auto t1 = std::chrono::high_resolution_clock::now();
                const double sec = std::chrono::duration<double> (t1 - t0).count();
                std::printf ("  %s %5.1f%%", osNames[os], 100.0 * sec / 6.0);
            }
            std::printf ("\n");
        }
    return 0;
}

int cmdRender (const juce::String& outDir)
{
    juce::File dir (outDir);
    dir.createDirectory();
    const Sig in = brightMix ((size_t) (kFs * 8));
    writeWav (dir.getChildFile ("00_dry_bright_mix.wav"), in);
    struct R { const char* name; float amt; int mode; float q; bool tape; int model; };
    const R renders[] = {
        { "01_even_amount4", 4, 0, 2.83f, false, 1 },  { "02_odd_amount4", 4, 1, 2.83f, false, 1 },
        { "03_hectic_amount4", 4, 2, 2.83f, false, 1 }, { "04_even_amount8", 8, 0, 2.83f, false, 1 },
        { "05_even_amount11", 11, 0, 2.83f, false, 1 }, { "06_even_amount5_narrowQ", 5, 0, 20.0f, false, 1 },
        { "07_even_amount5_broadQ", 5, 0, 0.3f, false, 1 }, { "08_tape_silk", 0, 0, 2.83f, true, 0 },
        { "09_tape_solid", 0, 0, 2.83f, true, 1 },     { "10_tape_home", 0, 0, 2.83f, true, 2 },
        { "11_tape_garage", 0, 0, 2.83f, true, 3 },    { "12_tape_cassette", 0, 0, 2.83f, true, 4 },
        { "13_even4_plus_solid", 4, 0, 2.83f, true, 1 },
    };
    for (const auto& r : renders)
    {
        EngineParams p;
        p.amount = r.amt; p.mode = r.mode; p.q = r.q; p.tapeOn = r.tape; p.tapeModel = r.model; p.osIndex = 2;
        int lat = 0;
        Sig out = runEngine (in, p, 512, &lat);
        Sig aligned (in.size());
        for (size_t i = 0; i + (size_t) lat < out.size(); ++i) { aligned.l[i] = out.l[i + (size_t) lat]; aligned.r[i] = out.r[i + (size_t) lat]; }
        writeWav (dir.getChildFile (juce::String (r.name) + ".wav"), aligned);
        std::printf ("rendered %s (loudness change %+.2f dB)\n", r.name, kLoudness (aligned, (size_t) kFs) - kLoudness (in, (size_t) kFs));
    }
    return 0;
}

int cmdCalibHarm (const juce::String& headerPath)
{
    HarmonicStage::makeupEnabled = false;
    // high band (above 4 kHz) of the reference programme
    const Sig ref = brightMix ((size_t) (kFs * 3), 9);
    LinearPhaseSplit sp;
    sp.setCutoff (4000.0f, true);
    sp.prepare (kFs, 2);
    Sig lo (ref.size()), hi (ref.size());
    {
        const float* in[2] = { ref.l.data(), ref.r.data() };
        float* l[2] = { lo.l.data(), lo.r.data() };
        float* h[2] = { hi.l.data(), hi.r.data() };
        sp.process (in, l, h, 2, (int) ref.size());
    }
    std::string out = "// Unique Vintage Processor — static make-up for the processed top band (generated by UVPHarness calibharm)\n"
                      "// Keeps the band's level steady at extreme drive so heavy settings don't simply dull the top end.\n"
                      "#pragma once\n\nnamespace uvp::hmk\n{\nconstexpr int kSteps = 23; // amount 0..11 in 0.5 steps\n"
                      "inline const float (&table())[3][kSteps]\n{\n    static const float t[3][kSteps] = {\n";
    for (int mode = 0; mode < 3; ++mode)
    {
        out += "        { ";
        for (int a = 0; a < 23; ++a)
        {
            float mk = 0.0f;
            if (a > 0)
            {
                HarmonicStage hs;
                hs.setOversampling (2);
                hs.setParams (0.5f * (float) a, (HarmonicStage::Mode) mode);
                hs.prepare (kFs, 2, 512);
                hs.setParams (0.5f * (float) a, (HarmonicStage::Mode) mode);
                Sig x = hi;
                for (size_t pos = 0; pos < x.size(); pos += 512)
                {
                    float* ch[2] = { x.l.data() + pos, x.r.data() + pos };
                    hs.process (ch, 2, (int) std::min<size_t> (512, x.size() - pos));
                }
                const size_t lat = (size_t) hs.getLatencySamples(), st = (size_t) kFs;
                const double inDb = rmsDb (hi.l.data() + st, hi.size() - st - lat);
                const double outDb = rmsDb (x.l.data() + st + lat, x.size() - st - lat);
                mk = juce::jlimit (-3.0f, 9.0f, (float) (inDb - outDb));
            }
            out += juce::String (mk, 2).toStdString() + "f" + (a < 22 ? ", " : "");
        }
        out += mode < 2 ? " },\n" : " }\n";
    }
    out += "    };\n    return t;\n}\ninline float makeupDb (int mode, float amount) noexcept\n{\n    const auto& t = table();\n"
           "    mode = mode < 0 ? 0 : (mode > 2 ? 2 : mode);\n    float x = amount * 2.0f;\n"
           "    x = x < 0.0f ? 0.0f : (x > (float) (kSteps - 1) ? (float) (kSteps - 1) : x);\n"
           "    const int i0 = (int) x, i1 = i0 + 1 < kSteps ? i0 + 1 : i0;\n    const float fr = x - (float) i0;\n"
           "    return t[mode][i0] + fr * (t[mode][i1] - t[mode][i0]);\n}\n} // namespace uvp::hmk\n";
    juce::File (headerPath).replaceWithText (out);
    std::printf ("wrote %s\n", headerPath.toRawUTF8());
    return 0;
}

int cmdCalibrate (const juce::String& headerPath)
{
    using namespace agc;
    // 1) tape hDrive suggestions to hit target 3rd harmonic at 0 VU
    const double targets[] = { 0.0035, 0.006, 0.008, 0.012, 0.018 };
    for (int m = 0; m < kNumTapeModels; ++m)
        std::printf ("%s THD3@0VU now %.3f%% (target %.2f%%)\n", tapeModel (m).name, 100 * TapeMachine::measureThd3 (m, kFs, -18.0), 100 * targets[m]);

    // 2) vintage loudness compensation table on reference programme
    const Sig ref = brightMix ((size_t) (kFs * 2.5), 3);
    std::string out = "// Unique Vintage Processor — calibrated loudness compensation (generated by UVPHarness calibrate)\n"
                      "#pragma once\n\nnamespace uvp::agc\n{\n"
                      "constexpr int kAmountSteps = 23;\nconstexpr int kHpfSteps = 10;\nconstexpr int kInputSteps = 7;\n"
                      "constexpr float kHpfHz[9] = { 20.0f, 50.0f, 150.0f, 400.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 15000.0f };\n"
                      "constexpr int kTapeDriveSteps = 13;\n\n"
                      "inline const float (&vintageTable())[3][kInputSteps][kHpfSteps][kAmountSteps]\n{\n"
                      "    static const float t[3][kInputSteps][kHpfSteps][kAmountSteps] = {\n";
    // the three modes run in parallel; each value is one engine run on the reference programme
    static float vals[3][kInputSteps][kHpfSteps][kAmountSteps];
    std::vector<std::thread> workers;
    for (int mode = 0; mode < 3; ++mode)
        workers.emplace_back ([mode, &ref]
        {
            for (int ii = 0; ii < kInputSteps; ++ii)
                for (int h = 0; h < kHpfSteps; ++h)
                    for (int a = 0; a < kAmountSteps; ++a)
                    {
                        float comp = 0.0f;
                        if (a > 0)
                        {
                            EngineParams p;
                            p.amount = 0.5f * (float) a;
                            p.mode = mode;
                            p.inputDb = -18.0f + 6.0f * (float) ii;
                            p.hpfOn = h > 0;
                            p.hpfHz = h > 0 ? kHpfHz[h - 1] : 4000.0f;
                            p.autoGain = true;   // removes the input gain; the table adds the rest
                            p.osIndex = 1;
                            int lat = 0;
                            const Sig o = runEngine (ref, p, 512, &lat);
                            Sig al (ref.size());
                            for (size_t i = 0; i + (size_t) lat < o.size(); ++i) { al.l[i] = o.l[i + (size_t) lat]; al.r[i] = o.r[i + (size_t) lat]; }
                            comp = (float) (kLoudness (ref, (size_t) kFs / 2) - kLoudness (al, (size_t) kFs / 2));
                            comp = juce::jlimit (-24.0f, 24.0f, comp);
                        }
                        vals[mode][ii][h][a] = comp;
                    }
            std::printf ("mode %d done\n", mode);
            std::fflush (stdout);
        });
    for (auto& w : workers) w.join();
    for (int mode = 0; mode < 3; ++mode)
    {
        out += "    {\n";
        for (int ii = 0; ii < kInputSteps; ++ii)
        {
            out += "      {\n";
            for (int h = 0; h < kHpfSteps; ++h)
            {
                out += "        { ";
                for (int a = 0; a < kAmountSteps; ++a)
                    out += juce::String (vals[mode][ii][h][a], 2).toStdString() + "f" + (a + 1 < kAmountSteps ? ", " : "");
                out += h + 1 < kHpfSteps ? " },\n" : " }\n";
            }
            out += ii + 1 < kInputSteps ? "      },\n" : "      }\n";
        }
        out += mode < 2 ? "    },\n" : "    }\n";
    }
    out += "    };\n    return t;\n}\n\n";

    // 3) tape compensation vs drive
    out += "inline const float (&tapeTable())[5][kTapeDriveSteps]\n{\n    static const float t[5][kTapeDriveSteps] = {\n";
    const Sig tref = brightMix ((size_t) (kFs * 2.5), 5);
    for (int m = 0; m < 5; ++m)
    {
        out += "        { ";
        for (int d = 0; d < kTapeDriveSteps; ++d)
        {
            EngineParams p;
            p.tapeOn = true; p.tapeModel = m; p.tapeDriveDb = -12.0f + 2.0f * (float) d; p.autoGain = true; p.osIndex = 1;
            int lat = 0;
            const Sig o = runEngine (tref, p, 512, &lat);
            Sig al (tref.size());
            for (size_t i = 0; i + (size_t) lat < o.size(); ++i) { al.l[i] = o.l[i + (size_t) lat]; al.r[i] = o.r[i + (size_t) lat]; }
            const float comp = (float) (kLoudness (tref, (size_t) kFs / 2) - kLoudness (al, (size_t) kFs / 2));
            out += juce::String (juce::jlimit (-24.0f, 24.0f, comp), 2).toStdString() + "f" + (d + 1 < kTapeDriveSteps ? ", " : "");
        }
        out += m < 4 ? " },\n" : " }\n";
    }
    out += "    };\n    return t;\n}\n} // namespace uvp::agc\n";
    juce::File (headerPath).replaceWithText (out);
    std::printf ("wrote %s\n", headerPath.toRawUTF8());
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    const juce::String cmd = argc > 1 ? argv[1] : "null";
    if (cmd == "null") return cmdNull();
    if (cmd == "split") return cmdSplit();
    if (cmd == "harm") return cmdHarm();
    if (cmd == "supp") return cmdSupp();
    if (cmd == "supp2") return cmdSupp2();
    if (cmd == "suppdbg") return cmdSuppDbg();
    if (cmd == "tape") return cmdTape();
    if (cmd == "cpu") return cmdCpu();
    if (cmd == "agcheck") return cmdAgCheck();
    if (cmd == "display") return cmdDisplay();
    if (cmd == "calibtape") return cmdCalibTape();
    if (cmd == "calibtape2") return cmdCalibTape2();
    if (cmd == "render") return cmdRender (argc > 2 ? argv[2] : "renders");
    if (cmd == "calibharm") return cmdCalibHarm (argc > 2 ? argv[2] : "HarmonicMakeup.h");
    if (cmd == "calibrate") return cmdCalibrate (argc > 2 ? argv[2] : "AutoGainTables.h");
    std::printf ("unknown command\n");
    return 2;
}
