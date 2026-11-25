// Unique Vintage Processor — offline measurement harness
// usage: UVPHarness <command> [args]
//   null | split | harm | supp | tape | cpu | render <outdir> | calibrate <AutoGainTables.h path>
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/VintageEngine.h"
#include "dsp/AutoGainTables.h"
#include "dsp/AutoGainLpf.h"
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
    struct Cfg { const char* name; float hp; bool hpOn; float lp; bool lpOn; };
    const Cfg cfgs[] = { { "HPF 4k", 4000.0f, true, 20000.0f, false },
                         { "HPF 4k + LPF 10k", 4000.0f, true, 10000.0f, true },
                         { "LPF 2k only", 4000.0f, false, 2000.0f, true } };
    for (const auto& cfg : cfgs)
    {
        LinearPhaseSplit sp;
        sp.setBand (cfg.hp, cfg.hpOn, cfg.lp, cfg.lpOn);
        sp.prepare (kFs, 1);
        const int lat = sp.getLatencySamples();
        std::printf ("%s (latency %d): rest / band\n", cfg.name, lat);
        for (double f : { 500.0, 2000.0, 3000.0, 4000.0, 5000.0, 8000.0, 10000.0, 12000.0, 16000.0 })
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
            std::printf ("  %6.0f Hz: rest %7.2f dB  band %7.2f dB  |rest+band - x| max %.2g\n", f, 20 * std::log10 (al + 1e-12),
                         20 * std::log10 (ah + 1e-12), recon);
        }
    }
    // switching the low-pass edge on and off while running must not click
    {
        LinearPhaseSplit sp;
        sp.setBand (4000.0f, true, 20000.0f, false);
        sp.prepare (kFs, 1);
        const int lat = sp.getLatencySamples();
        const size_t n = (size_t) kFs * 2;
        std::vector<float> x (n), lo (n), hi (n);
        for (size_t i = 0; i < n; ++i) x[i] = (float) (0.3 * std::sin (kTwoPi * 9000.0 * (double) i / kFs) + 0.3 * std::sin (kTwoPi * 300.0 * (double) i / kFs));
        double worst = 0.0;
        for (size_t pos = 0; pos < n; pos += 480)
        {
            const bool on = ((pos / 9600) % 2) == 1;                   // toggles every 200 ms
            sp.setBand (4000.0f, true, on ? 7000.0f + 50.0f * (float) ((pos / 480) % 40) : 20000.0f, on);
            const float* in[1] = { x.data() + pos };
            float* l[1] = { lo.data() + pos };
            float* h[1] = { hi.data() + pos };
            sp.process (in, l, h, 1, (int) std::min<size_t> (480, n - pos));
        }
        for (size_t i = (size_t) lat + 1; i < n; ++i)
            worst = std::max (worst, (double) std::abs (lo[i] + hi[i] - x[i - (size_t) lat]));
        double maxStep = 0.0;   // biggest sample-to-sample jump in the band output (a click would show here)
        for (size_t i = (size_t) lat + 2; i < n; ++i) maxStep = std::max (maxStep, (double) std::abs (hi[i] - hi[i - 1]));
        std::printf ("LPF toggling + sweeping: reconstruction max %.2g, band max step %.3f (9 kHz sine alone steps up to %.3f)\n",
                     worst, maxStep, 0.3 * 2.0 * std::sin (kTwoPi * 9000.0 / kFs / 2.0) * 2.0);
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

int cmdCalibrateLpf (const juce::String& headerPath)
{
    // Residual of the low-pass auto-gain model (see VintageEngine::vintageCompDb), measured on the
    // same reference programme as the main tables: the extra dB each FILTER band needs.
    using namespace agc;
    const Sig ref = brightMix ((size_t) (kFs * 2.5), 3);
    constexpr int NA = kLpfAmountSteps, NL = kLpfSteps;
    static float vals[3][kInputSteps][kHpfSteps][NL][NA];
    std::vector<std::thread> workers;
    for (int mode = 0; mode < 3; ++mode)
        workers.emplace_back ([mode, &ref]
        {
            for (int ii = 0; ii < kInputSteps; ++ii)
                for (int h = 0; h < kHpfSteps; ++h)
                    for (int l = 0; l < NL; ++l)
                        for (int a = 0; a < NA; ++a)
                        {
                            float resid = 0.0f;
                            const float hp = h > 0 ? kHpfHz[h - 1] : 20.0f;
                            if (kLpfAmounts[a] > 0.0f && kLpfHz[l] > hp * 1.01f)
                            {
                                EngineParams p;
                                p.amount = kLpfAmounts[a];
                                p.mode = mode;
                                p.inputDb = -18.0f + 6.0f * (float) ii;
                                p.hpfOn = h > 0;
                                p.hpfHz = h > 0 ? kHpfHz[h - 1] : 4000.0f;
                                p.lpfOn = true;
                                p.lpfHz = kLpfHz[l];
                                p.autoGain = true;
                                p.osIndex = 1;
                                VintageEngine::useLpfResidual = false;
                                int lat = 0;
                                const Sig o = runEngine (ref, p, 512, &lat);
                                Sig al (ref.size());
                                for (size_t i = 0; i + (size_t) lat < o.size(); ++i) { al.l[i] = o.l[i + (size_t) lat]; al.r[i] = o.r[i + (size_t) lat]; }
                                resid = juce::jlimit (-12.0f, 12.0f, (float) (kLoudness (ref, (size_t) kFs / 2) - kLoudness (al, (size_t) kFs / 2)));
                            }
                            vals[mode][ii][h][l][a] = resid;
                        }
            std::printf ("mode %d done\n", mode);
            std::fflush (stdout);
        });
    for (auto& w : workers) w.join();

    std::string out = "// Unique Vintage Processor — auto gain with the FILTER low-pass in (generated by UVPHarness calibratelpf)\n"
                      "// Extra compensation (dB) on top of the power-additive model, per mode / input / high-pass knot /\n"
                      "// low-pass knot / amount knot. Zero where the band is empty.\n#pragma once\n#include \"AutoGainTables.h\"\n"
                      "#include \"AutoGainLpf.h\"\n\nnamespace uvp::agc\n{\n"
                      "inline const float (&lpfResidualTable())[3][kInputSteps][kHpfSteps][kLpfSteps][kLpfAmountSteps]\n{\n"
                      "    static const float t[3][kInputSteps][kHpfSteps][kLpfSteps][kLpfAmountSteps] = {\n";
    for (int mode = 0; mode < 3; ++mode)
    {
        out += "    {\n";
        for (int ii = 0; ii < kInputSteps; ++ii)
        {
            out += "     {\n";
            for (int h = 0; h < kHpfSteps; ++h)
            {
                out += "      {";
                for (int l = 0; l < NL; ++l)
                {
                    out += " {";
                    for (int a = 0; a < NA; ++a)
                        out += juce::String (vals[mode][ii][h][l][a], 2).toStdString() + "f" + (a + 1 < NA ? "," : "");
                    out += l + 1 < NL ? "}," : "}";
                }
                out += h + 1 < kHpfSteps ? " },\n" : " }\n";
            }
            out += ii + 1 < kInputSteps ? "     },\n" : "     }\n";
        }
        out += mode < 2 ? "    },\n" : "    }\n";
    }
    out += "    };\n    return t;\n}\n} // namespace uvp::agc\n";
    juce::File (headerPath).replaceWithText (out);
    std::printf ("wrote %s\n", headerPath.toRawUTF8());
    return 0;
}

int cmdMix()
{
    // MIX blends the whole processed signal (vintage + tape + OUTPUT) with the untouched input
    const Sig in = brightMix ((size_t) (kFs * 2), 5);
    auto heavy = [] (float mix, bool delta)
    {
        EngineParams p;
        p.amount = 11.0f; p.mode = 2; p.inputDb = 6.0f; p.outputDb = -4.0f; p.autoGain = true; p.osIndex = 2;
        p.hpfHz = 2000.0f; p.lpfHz = 12000.0f; p.lpfOn = true;
        p.tapeOn = true; p.tapeModel = 3; p.tapeDriveDb = 6.0f;
        p.mix = mix; p.delta = delta;
        return p;
    };
    auto run = [&] (const EngineParams& p, int& lat) { return runEngine (in, p, 512, &lat); };
    int lat = 0;
    const Sig o0 = run (heavy (0.0f, false), lat);
    double worst0 = 0.0;
    for (size_t i = (size_t) lat; i < in.size(); ++i)
        worst0 = std::max ({ worst0, (double) std::abs (o0.l[i] - in.l[i - (size_t) lat]), (double) std::abs (o0.r[i] - in.r[i - (size_t) lat]) });
    std::printf ("MIX 0%% (HECTIC 11, IN +6, OUT -4, AG, tape GARAGE +6, LPF): max |out - input| = %g %s\n",
                 worst0, worst0 == 0.0 ? "(bit-exact)" : "");

    const Sig o100 = run (heavy (1.0f, false), lat);
    const Sig o50 = run (heavy (0.5f, false), lat);
    double worst50 = 0.0, peak100 = 0.0;
    for (size_t i = (size_t) lat; i < in.size(); ++i)
    {
        const float expect = 0.5f * o100.l[i] + 0.5f * in.l[i - (size_t) lat];
        worst50 = std::max (worst50, (double) std::abs (o50.l[i] - expect));
        peak100 = std::max (peak100, (double) std::abs (o100.l[i]));
    }
    std::printf ("MIX 50%% = half processed + half input: max deviation %g (processed peak %.3f)\n", worst50, peak100);

    const Sig d0 = run (heavy (0.0f, true), lat);
    double dpk = 0.0;
    for (size_t i = (size_t) lat + 4800; i < in.size(); ++i) dpk = std::max (dpk, (double) std::abs (d0.l[i]));
    std::printf ("DELTA at MIX 0%%: peak %g (silent)\n", dpk);
    return (worst0 == 0.0 && worst50 < 1.0e-4 && dpk == 0.0) ? 0 : 1;
}

int cmdAgLpfExtreme()
{
    const Sig ref = brightMix ((size_t) (kFs * 3), 11);
    std::printf ("AG with low-pass, extreme corners (out - in, dB)\n");
    double worst = 0.0;
    for (int mode : { 0, 2 })
        for (float in : { 12.0f, 18.0f })
            for (float hp : { 20.0f, 4000.0f })
            {
                std::printf ("  mode %d in %+3.0f hp %5.0f:", mode, in, hp);
                for (float lp : { 14000.0f, 16000.0f, 17000.0f, 18000.0f, 19000.0f, 19800.0f, 20000.0f })
                {
                    EngineParams p;
                    p.amount = 11.0f; p.mode = mode; p.inputDb = in; p.autoGain = true; p.osIndex = 2;
                    p.hpfHz = hp; p.hpfOn = true; p.lpfHz = lp; p.lpfOn = lp < 19990.0f;
                    int lat = 0;
                    const Sig o = runEngine (ref, p, 512, &lat);
                    Sig al (ref.size());
                    for (size_t i = 0; i + (size_t) lat < o.size(); ++i) { al.l[i] = o.l[i + (size_t) lat]; al.r[i] = o.r[i + (size_t) lat]; }
                    const double d = kLoudness (al, (size_t) kFs / 2) - kLoudness (ref, (size_t) kFs / 2);
                    worst = std::max (worst, std::abs (d));
                    std::printf ("  %4.1fk %+5.2f", lp / 1000.0f, d);
                }
                std::printf ("\n");
            }
    std::printf ("worst |error| %.2f dB\n", worst);
    return 0;
}

int cmdAgLpf()
{
    // AG with the FILTER low-pass edge in: the tables only know the high-pass edge, the rest is modelled
    const Sig ref = brightMix ((size_t) (kFs * 3), 11);
    struct Band { float hp, lp; };
    const Band bands[] = { { 4000.0f, 20000.0f }, { 4000.0f, 16000.0f }, { 4000.0f, 12000.0f }, { 4000.0f, 8000.0f },
                           { 2000.0f, 6000.0f }, { 1000.0f, 4000.0f }, { 400.0f, 3000.0f } };
    double worst = 0.0;
    std::printf ("AG with low-pass (K-weighted loudness out - in, dB; AG on)\n");
    for (int mode = 0; mode < 3; ++mode)
        for (float in : { -6.0f, 6.0f })
            for (const auto& b : bands)
            {
                std::printf ("  mode %d in %+4.0f  %5.0f-%5.0f Hz:", mode, in, b.hp, b.lp);
                for (float a : { 4.0f, 8.0f, 11.0f })
                {
                    EngineParams p;
                    p.amount = a; p.mode = mode; p.inputDb = in; p.autoGain = true; p.osIndex = 2;
                    p.hpfHz = b.hp; p.hpfOn = true; p.lpfHz = b.lp; p.lpfOn = b.lp < 19990.0f;
                    int lat = 0;
                    const Sig o = runEngine (ref, p, 512, &lat);
                    Sig al (ref.size());
                    for (size_t i = 0; i + (size_t) lat < o.size(); ++i) { al.l[i] = o.l[i + (size_t) lat]; al.r[i] = o.r[i + (size_t) lat]; }
                    const double d = kLoudness (al, (size_t) kFs / 2) - kLoudness (ref, (size_t) kFs / 2);
                    worst = std::max (worst, std::abs (d));
                    std::printf ("  a%-4.1f %+5.2f", a, d);
                }
                std::printf ("\n");
            }
    std::printf ("worst |error| %.2f dB\n", worst);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Aliasing: a pure tone exactly on an FFT bin, steady state, 7-term Blackman-Harris window.
// Everything that is not the tone, one of its harmonics below Nyquist, or DC is counted as
// aliasing (inharmonic energy). Reported relative to the output fundamental.
struct AliasResult { double fundDb, worstAliasDb, worstAliasHz, totalAliasDb, thdDb; };

static AliasResult measureAlias (const EngineParams& p, double f0Target, double levelDbfs)
{
    constexpr int order = 16, N = 1 << order;
    const double binHz = kFs / N;
    const int k0 = (int) std::round (f0Target / binHz);
    const double f0 = k0 * binHz;
    const size_t total = (size_t) (kFs * 2.0) + (size_t) N;
    Sig in (total);
    const double amp = std::pow (10.0, levelDbfs / 20.0) * std::sqrt (2.0) * 0.5 * std::sqrt (2.0); // peak = level dBFS (sine peak)
    for (size_t i = 0; i < total; ++i)
        in.l[i] = in.r[i] = (float) (std::pow (10.0, levelDbfs / 20.0) * std::sin (kTwoPi * f0 * (double) i / kFs));
    (void) amp;
    const Sig o = runEngine (in, p, 512, nullptr);

    static const double c[7] = { 0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
                                 0.01081174209837, 0.00077658482522, 0.00001388721735 };
    std::vector<float> buf ((size_t) (2 * N), 0.0f);
    double wsum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double x = kTwoPi * i / N;
        double w = 0.0;
        for (int m = 0; m < 7; ++m) w += (m % 2 ? -1.0 : 1.0) * c[m] * std::cos (m * x);
        wsum += w;
        buf[(size_t) i] = (float) (w * o.l[total - (size_t) N + (size_t) i]);
    }
    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
    std::vector<double> pw ((size_t) (N / 2));
    for (int k = 0; k < N / 2; ++k) { const double m = buf[(size_t) k] / (wsum * 0.5); pw[(size_t) k] = m * m; }

    auto bandPower = [&] (int centre, int halfWidth) {
        double s = 0.0;
        for (int k = std::max (0, centre - halfWidth); k <= std::min (N / 2 - 1, centre + halfWidth); ++k) s += pw[(size_t) k];
        return s;
    };
    const int guard = 12;
    std::vector<char> excluded ((size_t) (N / 2), 0);
    for (int k = 0; k <= guard; ++k) excluded[(size_t) k] = 1;                      // DC
    const double fund = bandPower (k0, guard);
    double harm = 0.0;
    for (int h = 1; h * k0 < N / 2; ++h)
    {
        if (h > 1) harm += bandPower (h * k0, guard);
        for (int k = h * k0 - guard; k <= h * k0 + guard; ++k)
            if (k >= 0 && k < N / 2) excluded[(size_t) k] = 1;
    }
    const int kMax = (int) (20000.0 / binHz);
    double alias = 0.0, worst = 0.0;
    int worstK = 0;
    for (int k = (int) (20.0 / binHz); k <= kMax; ++k)
    {
        if (excluded[(size_t) k]) continue;
        alias += pw[(size_t) k];
    }
    // worst single component: power summed over its window main lobe
    for (int k = (int) (20.0 / binHz); k <= kMax; ++k)
    {
        if (excluded[(size_t) k]) continue;
        if (pw[(size_t) k] > worst) { worst = pw[(size_t) k]; worstK = k; }
    }
    const double worstLobe = worstK > 0 ? bandPower (worstK, 7) : 0.0;
    if (std::getenv ("UVP_ALIAS_DETAIL"))
    {
        // list the strongest inharmonic peaks (local maxima)
        std::vector<std::pair<double, int>> peaks;
        for (int k = (int) (20.0 / binHz) + 1; k < kMax; ++k)
            if (! excluded[(size_t) k] && pw[(size_t) k] > pw[(size_t) k - 1] && pw[(size_t) k] >= pw[(size_t) k + 1])
                peaks.push_back ({ pw[(size_t) k], k });
        std::sort (peaks.begin(), peaks.end(), [] (auto& x, auto& y) { return x.first > y.first; });
        for (size_t i = 0; i < std::min<size_t> (8, peaks.size()); ++i)
            std::printf ("      %8.1f Hz  %6.1f dB\n", peaks[i].second * binHz, 10.0 * std::log10 (bandPower (peaks[i].second, 7) / fund));
    }
    AliasResult r;
    r.fundDb = 10.0 * std::log10 (fund + 1e-300);
    r.worstAliasDb = 10.0 * std::log10 (worstLobe / fund + 1e-30);
    r.worstAliasHz = worstK * binHz;
    r.totalAliasDb = 10.0 * std::log10 (alias / fund + 1e-30);
    r.thdDb = 10.0 * std::log10 (harm / fund + 1e-30);
    return r;
}

// J-A core alone at the oversampled rate: inharmonic energy below 20 kHz (no resampling filters)
int cmdJaAlias()
{
    const JilesAtherton::Params cores[] = { { 1, 1, 0, 0.05, 0.85 }, { 1, 1, 0, 0.20, 0.60 } };
    const char* names[] = { "ODD core", "HECTIC core" };
    for (int c = 0; c < 2; ++c)
        for (double amp : { 0.3, 0.6, 1.0, 1.5, 3.0 })
            for (int refine = 1; refine < 2; ++refine)
            {
                std::printf ("%-11s H %.1f %-8s |", names[c], amp, refine ? "refined" : "plain");
                for (int osf : { 1, 2, 4, 8, 16 })
                {
                    const double fs = kFs * osf;
                    const int order = 16 + (int) std::round (std::log2 ((double) osf)), N = 1 << order;
                    const double binHz = fs / N;
                    const int k0 = (int) std::round ((std::getenv ("UVP_F0") ? std::atof (std::getenv ("UVP_F0")) : 9000.0) / binHz);
                    const double f0 = k0 * binHz;
                    JilesAtherton ja; ja.setParams (cores[c]); ja.reset();
                    const int pre = N / 2;
                    std::vector<float> buf ((size_t) (2 * N), 0.0f);
                    static const double cw[7] = { 0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
                                                  0.01081174209837, 0.00077658482522, 0.00001388721735 };
                    double wsum = 0.0;
                    for (int i = 0; i < pre + N; ++i)
                    {
                        const double h = amp * std::sin (kTwoPi * f0 * (double) i / fs);
                        const double m = refine ? ja.processAA (h) : ja.process (h);
                        if (i >= pre)
                        {
                            const int j = i - pre;
                            const double x = kTwoPi * j / N;
                            double w = 0.0;
                            for (int q = 0; q < 7; ++q) w += (q % 2 ? -1.0 : 1.0) * cw[q] * std::cos (q * x);
                            wsum += w;
                            buf[(size_t) j] = (float) (w * m);
                        }
                    }
                    juce::dsp::FFT fft (order);
                    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
                    double fund = 0.0, inh = 0.0;
                    for (int k = 1; k < (int) (20000.0 / binHz); ++k)
                    {
                        const double pw = std::pow (buf[(size_t) k] / (wsum * 0.5), 2.0);
                        const int nearest = (int) std::round ((double) k / k0) * k0;
                        if (std::abs (k - k0) <= 12) fund += pw;
                        else if (std::abs (k - nearest) > 12 && k > 12) inh += pw;
                    }
                    std::printf (" %2dx %6.1f", osf, 10.0 * std::log10 (inh / fund + 1e-30));
                }
                std::printf ("\n");
            }
    return 0;
}

int cmdAliasOne (int argc, char** argv)
{
    // aliasone <mode> <amount> <f0> <levelDbfs> <os>
    if (argc < 7) return 1;
    EngineParams p;
    p.mode = std::atoi (argv[2]); p.amount = (float) std::atof (argv[3]); p.osIndex = std::atoi (argv[6]);
    setenv ("UVP_ALIAS_DETAIL", "1", 1);
    const auto r = measureAlias (p, std::atof (argv[4]), std::atof (argv[5]));
    std::printf ("worst %.1f dB at %.1f Hz, total %.1f dB, THD %.1f dB\n", r.worstAliasDb, r.worstAliasHz, r.totalAliasDb, r.thdDb);
    return 0;
}

int cmdAlias()
{
    const char* modeNames[] = { "EVEN", "ODD", "HECTIC" };
    const char* osNames[] = { "1x", "2x", "4x", "8x", "16x" };
    const double level = std::getenv ("UVP_LEVEL") ? std::atof (std::getenv ("UVP_LEVEL")) : -12.0;
    std::printf ("Aliasing in 20 Hz-20 kHz relative to the output fundamental (tone at %.0f dBFS, 48 kHz)\n", level);
    std::printf ("worst = strongest single inharmonic component, total = all inharmonic energy\n\n");
    {
        EngineParams p;   // amount 0: the measurement floor
        const auto r = measureAlias (p, 15000.0, level);
        std::printf ("baseline (amount 0, 15 kHz): worst %.1f dB, total %.1f dB, fundamental %.2f dBFS\n\n", r.worstAliasDb, r.totalAliasDb, r.fundDb);
    }
    for (double f0 : { 9000.0, 15000.0 })
        for (int mode = 0; mode < 3; ++mode)
            for (float a : { 8.0f, 11.0f })
            {
                std::printf ("%5.0f Hz %-6s amount %4.1f |", f0, modeNames[mode], a);
                for (int os = 0; os < 5; ++os)
                {
                    EngineParams p;
                    p.amount = a; p.mode = mode; p.osIndex = os;
                    const auto r = measureAlias (p, f0, level);
                    std::printf (" %s %6.1f/%6.1f", osNames[os], r.worstAliasDb, r.totalAliasDb);
                }
                std::printf ("\n");
            }
    std::printf ("\nTape alone (amount 0, transport frozen), 9 kHz:\n");
    for (int m = 0; m < kNumTapeModels; ++m)
        for (float d : { 0.0f, 12.0f })
        {
            std::printf ("  %-14s drive %+5.1f |", tapeModel (m).name, d);
            for (int os = 0; os < 5; ++os)
            {
                EngineParams p;
                p.tapeOn = true; p.tapeModel = m; p.tapeDriveDb = d; p.tapeTransport = false; p.osIndex = os;
                const auto r = measureAlias (p, 9000.0, level);
                std::printf (" %s %6.1f/%6.1f", osNames[os], r.worstAliasDb, r.totalAliasDb);
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
    // the FILTER low-pass edge adds a second convolution; measure it on its own at 4x
    for (int lpOn = 0; lpOn < 2; ++lpOn)
    {
        double best = 1.0e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            EngineParams p;
            p.amount = 6.0f; p.mode = 0; p.osIndex = 2; p.hpfHz = 4000.0f; p.lpfHz = 12000.0f; p.lpfOn = lpOn == 1;
            VintageEngine e;
            const auto t0 = std::chrono::high_resolution_clock::now();
            runEngine (in, p, 512, nullptr, &e);
            const auto t1 = std::chrono::high_resolution_clock::now();
            best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
        }
        std::printf ("  even 4x, low-pass %s: %5.2f%%\n", lpOn ? "at 12 kHz" : "off      ", 100.0 * best / 6.0);
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
    sp.setBand (4000.0f, true, 20000.0f, false);
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
    if (cmd == "alias") return cmdAlias();
    if (cmd == "aliasone") return cmdAliasOne (argc, argv);
    if (cmd == "jaalias") return cmdJaAlias();
    if (cmd == "agcheck") return cmdAgCheck();
    if (cmd == "aglpf") return cmdAgLpf();
    if (cmd == "aglpfx") return cmdAgLpfExtreme();
    if (cmd == "mix") return cmdMix();
    if (cmd == "calibratelpf") return cmdCalibrateLpf (argc > 2 ? argv[2] : "AutoGainLpfTables.h");
    if (cmd == "display") return cmdDisplay();
    if (cmd == "calibtape") return cmdCalibTape();
    if (cmd == "calibtape2") return cmdCalibTape2();
    if (cmd == "render") return cmdRender (argc > 2 ? argv[2] : "renders");
    if (cmd == "calibharm") return cmdCalibHarm (argc > 2 ? argv[2] : "HarmonicMakeup.h");
    if (cmd == "calibrate") return cmdCalibrate (argc > 2 ? argv[2] : "AutoGainTables.h");
    std::printf ("unknown command\n");
    return 2;
}
