// Developer probe: J-A solver statistics and accuracy against a fine-step RK4 reference.
#include "dsp/AnalogModels.h"
#include "dsp/HarmonicStage.h"
#include "dsp/TapeMachine.h"
#include <cstdio>
#include <random>
using namespace uvp;

static void accuracy (const char* name, JilesAtherton::Params p, double fsT, double amp, bool noise)
{
    JilesAtherton fast, ref;
    fast.setParams (p); ref.setParams (p); fast.reset(); ref.reset();
    std::mt19937 rng (7); std::normal_distribution<double> nd (0.0, 1.0);
    double lp = 0, e2 = 0, s2 = 0;
    const int n = (int) fsT; // 1 s
    for (int i = 0; i < n; ++i)
    {
        double x;
        if (noise) { lp += 0.5 * (nd (rng) - lp); x = amp * lp; }
        else x = amp * std::sin (2.0 * 3.141592653589793 * 1000.0 * i / fsT);
        const double a = fast.process (x), b = ref.processRK (x, 0.002 * p.a);
        if (i > n / 10) { e2 += (a - b) * (a - b); s2 += b * b; }
    }
    std::printf ("  %-10s fs %6.0f amp %6.2f %-5s  error %7.1f dB re signal\n", name, fsT, amp, noise ? "noise" : "sine", 10.0 * std::log10 (e2 / std::max (1e-30, s2) + 1e-30));
}

int main()
{
    std::printf ("Accuracy (exponential integrator vs RK4 at 0.002a):\n");
    const JilesAtherton::Params cores[] = { { 1, 1, 0, 0.05, 0.85 }, { 1, 1, 0, 0.20, 0.60 }, { 1, 1, 0, 0.22, 0.82 } };
    const char* names[] = { "odd-core", "hectic", "cassette" };
    for (int c = 0; c < 3; ++c)
        for (double fsT : { 48000.0, 192000.0 })
            for (double amp : { 0.1, 1.0, 5.0, 30.0 })
                for (bool nz : { false, true })
                    accuracy (names[c], cores[c], fsT, amp, nz);

    const double fs = 48000; const int n = 512, blocks = 400;
    std::mt19937 rng (1); std::normal_distribution<float> nd (0.0f, 1.0f);
    std::printf ("Sub-steps on white noise at -20 dBFS RMS, amount 6:\n");
    for (int os = 0; os < 5; ++os)
    for (int which = 0; which < 3; ++which)
    {
        HarmonicStage hs; TapeMachine tm;
        hs.prepare (fs, 2, n); tm.prepare (fs, 2, n);
        hs.setOversampling (os); tm.setOversampling (os);
        hs.setParams (6.0f, which == 0 ? HarmonicStage::Odd : HarmonicStage::Hectic);
        tm.setParams (true, 4, 0.0f);
        hs.reset(); tm.reset();
        jaStats() = {};
        std::vector<float> L (n), R (n); float* ch[2] = { L.data(), R.data() };
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < n; ++i) { L[(size_t) i] = 0.1f * nd (rng); R[(size_t) i] = 0.1f * nd (rng); }
            if (which < 2) hs.process (ch, 2, n); else tm.process (ch, 2, n);
        }
        std::printf ("  os %d %-7s calls %9llu  mean substeps %.2f\n", os, which == 0 ? "odd" : which == 1 ? "hectic" : "tape",
                     jaStats().calls, (double) jaStats().subs / (double) std::max (1ull, jaStats().calls));
    }
}
