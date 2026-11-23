// Unique Vintage Processor — physically informed nonlinear models
//  * TriodeStage   : 12AX7 common-cathode stage (Koren plate model, grid conduction through a
//                    grid stopper, coupling-capacitor blocking and cathode-bypass bias drift)
//  * JilesAtherton : magnetic hysteresis (transformer cores / tape), integrated in H with RK
//  * DiodeClipper  : asymmetric silicon clipper (1 diode vs 2 in series), solved exactly
#pragma once

#include "DspCommon.h"

namespace uvp
{

//==============================================================================
/** Catmull-Rom interpolation in a table with one guard point at the top (t[n + 1]): smooth
    (continuous slope) so the curve has no corners at the table points. */
inline double catmullRom (const double* t, int i, int lastIndex, double fr) noexcept
{
    const double ym1 = t[i > 0 ? i - 1 : 0], y0 = t[i], y1 = t[i + 1], y2 = t[std::min (i + 2, lastIndex)];
    return y0 + 0.5 * fr * ((y1 - ym1) + fr * ((2.0 * ym1 - 5.0 * y0 + 4.0 * y1 - y2) + fr * (3.0 * (y0 - y1) + y2 - ym1)));
}

class TriodeStage
{
public:
    /** Builds the static plate transfer once; fsOversampled drives the RC dynamics. */
    void prepare (double fsOversampled)
    {
        buildTablesOnce();
        dt = 1.0 / fsOversampled;
        reset();
    }

    void reset() noexcept
    {
        vc = 0.0;            // coupling-capacitor voltage
        vk = table().vkQ;    // cathode voltage (bypassed, drifts slowly)
        wPrev = 0.0;
    }

    /** vin: grid drive in volts (AC). Returns the stage output referred back to the input,
        i.e. unity gain for small signals (inverted polarity removed). */
    inline double process (double vin) noexcept
    {
        const auto& T = table();
        // node A behind the coupling cap, relative to the cathode
        const double vA = vin - vc;
        const double u = vA - vk;
        // grid conduction through the grid stopper: w + Rs*G*w^1.5 = u  (w = v_gk)
        double w = u;
        if (u > 0.0)
        {
            w = std::max (0.0, std::min (u, wPrev > 0.0 ? wPrev : 0.5 * u));
            for (int it = 0; it < 4; ++it)
            {
                const double sw = std::sqrt (std::max (w, 0.0));
                const double fx = w + kRsG * w * sw - u;
                const double d = 1.0 + 1.5 * kRsG * sw;
                w -= fx / d;
                if (w < 0.0) w = 0.0;
            }
        }
        wPrev = w;
        const double ig = u > 0.0 ? kG * w * std::sqrt (w) : 0.0;
        // coupling cap charges with grid current + leak current
        vc += dt * (ig + vA / kRg) / kCc;
        // plate
        const double vp = T.plateAt (w);
        const double ip = (kBplus - vp) / kRp;
        vk += dt * (ip * kRk - vk) / (kRk * kCk);
        return (T.vpQ - vp) / T.gainQ;
    }

private:
    static constexpr double kBplus = 300.0, kRp = 100.0e3, kRk = 1.5e3, kCk = 22.0e-6;
    static constexpr double kRg = 1.0e6, kCc = 22.0e-9, kRs = 68.0e3, kG = 3.0e-4;
    static constexpr double kRsG = kRs * kG;

    struct Tables
    {
        static constexpr int kSize = 4096;
        static constexpr double kLo = -30.0, kHi = 6.0;
        double vp[kSize + 2] {};
        double vpQ = 0, vkQ = 0, gainQ = 1;

        static double korenIp (double vpk, double vgk)
        {
            constexpr double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
            vpk = std::max (vpk, 1.0e-3);
            const double arg = kp * (1.0 / mu + vgk / std::sqrt (kvb + vpk * vpk));
            const double soft = arg > 30.0 ? arg : std::log1p (std::exp (arg));
            const double e1 = vpk / kp * soft;
            return e1 > 0.0 ? 2.0 * std::pow (e1, ex) / kg1 : 0.0;
        }
        // plate voltage for a given grid-cathode voltage, cathode at vk
        static double solvePlate (double vgk, double vk)
        {
            double lo = vk, hi = kBplus;
            for (int i = 0; i < 80; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                const double f = mid - (kBplus - kRp * korenIp (mid - vk, vgk));
                if (f > 0.0) hi = mid; else lo = mid;
            }
            return 0.5 * (lo + hi);
        }
        void build()
        {
            // quiescent point: cathode self-bias vk = Ip*Rk with vgk = -vk
            double vk = 1.0;
            for (int i = 0; i < 60; ++i)
            {
                const double vp = solvePlate (-vk, vk);
                const double ip = (kBplus - vp) / kRp;
                vk = 0.5 * vk + 0.5 * ip * kRk;
            }
            vkQ = vk;
            for (int i = 0; i <= kSize + 1; ++i)
            {
                const double w = kLo + (kHi - kLo) * std::min (i, kSize) / kSize;
                vp[i] = solvePlate (w - vkQ, vkQ); // table indexed by v_gk + vkQ (grid voltage re ground)
            }
            // at rest the grid sits at 0 V and the cathode at +vkQ, so v_gk = -vkQ
            vpQ = plateAt (-vkQ);
            const double e = 1.0e-3;
            gainQ = (plateAt (-vkQ - e) - plateAt (-vkQ + e)) / (2.0 * e);
        }
        /** Plate voltage as a function of w = v_gk (relative to the quiescent cathode). */
        inline double plateAt (double w) const noexcept
        {
            // tables are indexed by grid voltage relative to ground == w + vkQ in [kLo, kHi]
            double x = (w + vkQ - kLo) * (kSize / (kHi - kLo));
            if (x <= 0.0) return vp[0];
            if (x >= kSize) return vp[kSize];
            const int i = (int) x;
            return catmullRom (vp, i, kSize + 1, x - i);
        }
    };

    static const Tables& table()
    {
        static const Tables t = [] { Tables tt; tt.build(); return tt; }();
        return t;
    }
    static void buildTablesOnce() { (void) table(); }

    double dt = 1.0 / 192000.0, vc = 0, vk = 1.0, wPrev = 0;
};

//==============================================================================
#ifdef UVP_JA_STATS
struct JaStats { unsigned long long calls = 0, subs = 0; };
inline JaStats& jaStats() { static JaStats s; return s; }
#endif

class JilesAtherton
{
public:
    struct Params
    {
        double Ms = 1.0, a = 1.0, alpha = 0.0, k = 0.3, c = 0.5;
    };

    void setParams (const Params& p) noexcept { P = p; }
    void reset() noexcept
    {
        M = 0.0; Hprev = 0.0; ManPrev = 0.0; dLPrev = 1.0 / 3.0;
        hist0 = hist1 = 0.0;
        saved = save();
    }

    /** process() with turning-point refinement, for oversampled audio.

        The field is only known at sample instants, so a peak or trough of the signal normally
        lands on the nearest sample and the loop tip is clipped by an amount that depends on
        where the true peak fell between samples. That changes the branch the core then follows,
        differently from cycle to cycle, which is heard as aliasing at a level that does not drop
        with signal level. Here the true extremum is located with a parabola through the three
        samples around it and the core is driven through it: when the peak fell before the
        middle sample the path from the previous sample is re-integrated through the peak (no
        latency; only that one middle sample keeps the old value). */
    inline double processAA (double H) noexcept
    {
        const double hIn = hist1 - hist0, hOut = H - hist1;
        if (hIn * hOut < 0.0)
        {
            const double den = hist0 - 2.0 * hist1 + H;
            if (den != 0.0)
            {
                const double p = std::max (-0.5, std::min (0.5, 0.5 * (hist0 - H) / den));
                const double hPeak = hist1 - 0.25 * (hist0 - H) * p;
                if (p < 0.0)
                {
                    restore (saved);            // state before the middle sample
                    process (hPeak);
                    process (hist1);
                }
                else
                    process (hPeak);
            }
        }
        saved = save();
        hist0 = hist1;
        hist1 = H;
        return process (H);
    }

    /** Advance to field H (H moves linearly over the sample) and return the magnetisation.

        For the alpha = 0 model used throughout, dM/dH = dM_irr·(Man − M)/(δk) + c·dMan/dH.
        With the anhysteretic curve taken as piecewise linear in H, that equation is linear in M
        and is solved exactly (exponential integrator), so any field step, however large, stays
        stable and accurate; sub-steps are only spent where the Langevin curve bends. */
    inline double process (double H) noexcept
    {
        const double h = H - Hprev;
        if (h == 0.0) return M;
        if (P.alpha != 0.0) return processRK (H, 0.2 * P.a);

        double lEnd, dlEnd;
        langevin (H / P.a, lEnd, dlEnd);
        const double manEnd = P.Ms * lEnd;
        // sub-steps where the Langevin curve bends: its rise across the step, plus a chord-error
        // estimate from the change of its slope (|L''|·step²)
        const double bend = std::abs (dlEnd - dLPrev) * std::abs (h) / P.a;
        const int nSub = std::min (16, 1 + (int) (std::abs (manEnd - ManPrev) * (8.0 / P.Ms) + std::sqrt (bend * 800.0)));
       #ifdef UVP_JA_STATS
        jaStats().calls++; jaStats().subs += (unsigned long long) nSub;
       #endif
        const double s = std::abs (h) / (double) nSub;     // path length per sub-step
        const double invK = 1.0 / P.k;
        const double ek = std::exp (-s * invK);
        const double oneMinusC = 1.0 - P.c;
        const double delta = h > 0.0 ? 1.0 : -1.0;
        double D = M - ManPrev;                              // distance from the anhysteretic
        double man0 = ManPrev;
        for (int i = 1; i <= nSub; ++i)
        {
            const double man1 = (i == nSub) ? manEnd : anhysteretic (Hprev + h * ((double) i / (double) nSub));
            const double drift = oneMinusC * (man1 - man0) / s;    // rate Man runs away along the path
            if (D * delta < 0.0)
            {
                // behind the anhysteretic: irreversible relaxation towards it
                D = D * ek - drift * P.k * (1.0 - ek);
            }
            else
            {
                // ahead (just after a reversal): reversible motion only, until it is caught up
                const double vStar = drift != 0.0 ? D / drift : 1.0e300;
                if (vStar >= s)
                    D -= drift * s;
                else
                    D = -drift * P.k * (1.0 - std::exp (-(s - std::max (0.0, vStar)) * invK));
            }
            man0 = man1;
        }
        M = std::max (-P.Ms, std::min (P.Ms, manEnd + D));
        Hprev = H;
        ManPrev = manEnd;
        dLPrev = dlEnd;
        return M;
    }

    /** Reference solver (RK4, fixed maximum step) — general alpha, used for verification. */
    inline double processRK (double H, double maxStep) noexcept
    {
        const double h = H - Hprev;
        if (h != 0.0)
        {
            const int nSub = std::min (4096, std::max (1, (int) std::ceil (std::abs (h) / maxStep)));
            const double hs = h / nSub;
            const double delta = h > 0.0 ? 1.0 : -1.0;
            double Hc = Hprev;
            for (int s = 0; s < nSub; ++s)
            {
                const double k1 = dMdH (Hc, M, delta);
                const double k2 = dMdH (Hc + 0.5 * hs, M + 0.5 * hs * k1, delta);
                const double k3 = dMdH (Hc + 0.5 * hs, M + 0.5 * hs * k2, delta);
                const double k4 = dMdH (Hc + hs, M + hs * k3, delta);
                M += hs * (k1 + 2.0 * k2 + 2.0 * k3 + k4) / 6.0;
                M = std::max (-P.Ms, std::min (P.Ms, M));
                Hc += hs;
            }
        }
        Hprev = H;
        double l, dl;
        langevin (H / P.a, l, dl);
        ManPrev = P.Ms * l;
        dLPrev = dl;
        return M;
    }

    double getM() const noexcept { return M; }

    /** Small-signal gain (fundamental of a low-level 1 kHz sine), used for unity normalisation. */
    static double measureSmallSignalGain (const Params& p, double amp = 0.02)
    {
        JilesAtherton ja;
        ja.setParams (p);
        ja.reset();
        const double fsT = 192000.0, f0 = 1000.0;
        const int per = (int) (fsT / f0), warm = per * 8, meas = per * 16;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < warm + meas; ++i)
        {
            const double ph = kTwoPi * f0 * i / fsT;
            const double m = ja.processAA (amp * std::sin (ph));
            if (i >= warm) { re += m * std::sin (ph); im += m * std::cos (ph); }
        }
        const double a1 = 2.0 * std::sqrt (re * re + im * im) / meas;
        return std::max (1.0e-6, a1 / amp);
    }

private:
    static inline void langevinExact (double q, double& L, double& dL) noexcept
    {
        const double aq = std::abs (q);
        if (aq < 1.0e-3)
        {
            L = q / 3.0 - q * q * q / 45.0;
            dL = 1.0 / 3.0 - q * q / 15.0;
            return;
        }
        const double e = std::exp (-2.0 * aq);
        const double coth = (1.0 + e) / (1.0 - e);
        L = (q > 0 ? 1.0 : -1.0) * (coth - 1.0 / aq);
        const double invSinh2 = 4.0 * e / ((1.0 - e) * (1.0 - e));
        dL = 1.0 / (q * q) - invSinh2;
    }

    /** Langevin function and its derivative from a table (|q| < 16, 256 points per unit,
        linear interpolation: < 1e-6 relative error), asymptotic form beyond. */
    struct LangevinTable
    {
        static constexpr int kSize = 4096;
        static constexpr double kQMax = 16.0, kScale = kSize / kQMax;
        float L[kSize + 2], dL[kSize + 2];
        LangevinTable()
        {
            for (int i = 0; i < kSize + 2; ++i)
            {
                double l, d;
                langevinExact ((double) i / kScale, l, d);
                L[i] = (float) l;
                dL[i] = (float) d;
            }
        }
        static const LangevinTable& get() { static const LangevinTable t; return t; }
    };

    static inline void langevin (double q, double& L, double& dL) noexcept
    {
        static const LangevinTable& T = LangevinTable::get();
        const double aq = std::abs (q);
        const double sgn = q < 0.0 ? -1.0 : 1.0;
        if (aq >= LangevinTable::kQMax)
        {
            const double inv = 1.0 / aq;
            L = sgn * (1.0 - inv);
            dL = inv * inv;
            return;
        }
        const double x = aq * LangevinTable::kScale;
        const int i = (int) x;
        const double f = x - (double) i;
        // cubic Hermite with the exact slopes: continuous slope, so no tiny kinks at the table
        // points for fast signals to turn into aliasing
        const double h = 1.0 / LangevinTable::kScale;
        const double y0 = T.L[i], y1 = T.L[i + 1], m0 = (double) T.dL[i] * h, m1 = (double) T.dL[i + 1] * h;
        const double f2 = f * f, f3 = f2 * f;
        L = sgn * ((2.0 * f3 - 3.0 * f2 + 1.0) * y0 + (f3 - 2.0 * f2 + f) * m0 + (-2.0 * f3 + 3.0 * f2) * y1 + (f3 - f2) * m1);
        dL = (double) T.dL[i] + f * (double) (T.dL[i + 1] - T.dL[i]);
    }

    inline double dMdH (double H, double m, double delta) const noexcept
    {
        const double q = (H + P.alpha * m) / P.a;
        double L, dL;
        langevin (q, L, dL);
        const double Man = P.Ms * L;
        const double dMan = P.Ms / P.a * dL;
        const double kappa = Man - m;
        const double deltaM = (kappa * delta > 0.0) ? 1.0 : 0.0;
        double denom = (1.0 - P.c) * delta * P.k - P.alpha * kappa;
        if (std::abs (denom) < 1.0e-9) denom = denom < 0 ? -1.0e-9 : 1.0e-9;
        const double t1 = deltaM * (1.0 - P.c) * kappa / denom;
        const double num = t1 + P.c * dMan;
        const double den2 = 1.0 - P.c * P.alpha * dMan;
        return num / den2;
    }

    inline double anhysteretic (double H) const noexcept
    {
        double L, dL;
        langevin (H / P.a, L, dL);
        return P.Ms * L;
    }

    struct State { double M, Hprev, ManPrev, dLPrev; };
    State save() const noexcept { return { M, Hprev, ManPrev, dLPrev }; }
    void restore (const State& s) noexcept { M = s.M; Hprev = s.Hprev; ManPrev = s.ManPrev; dLPrev = s.dLPrev; }

    Params P;
    double M = 0.0, Hprev = 0.0, ManPrev = 0.0, dLPrev = 1.0 / 3.0;
    double hist0 = 0.0, hist1 = 0.0;
    State saved { 0.0, 0.0, 0.0, 1.0 / 3.0 };
};

//==============================================================================
/** Static asymmetric diode clipper: (x - y)/R = Is(e^{y/Vt1} - 1) - Is(e^{-y/Vt2} - 1),
    tabulated once. Unity small-signal gain. Input/output in volts. */
class DiodeClipper
{
public:
    static inline double process (double x) noexcept { return table().at (x); }

private:
    struct Table
    {
        static constexpr int kSize = 8192;
        static constexpr double kRange = 40.0;
        double y[kSize + 2] {};
        void build()
        {
            constexpr double R = 2.2e3, Is = 2.52e-9, vt1 = 1.752 * 0.02585, vt2 = 2.0 * 1.752 * 0.02585;
            double guess = 0.0;
            for (int i = 0; i <= kSize + 1; ++i)
            {
                const double x = -kRange + 2.0 * kRange * std::min (i, kSize) / kSize;
                double yy = std::max (-2.0, std::min (2.0, guess));
                for (int it = 0; it < 100; ++it)
                {
                    const double ep = std::exp (std::min (60.0, yy / vt1)), en = std::exp (std::min (60.0, -yy / vt2));
                    const double f = (x - yy) / R - Is * (ep - 1.0) + Is * (en - 1.0);
                    const double df = -1.0 / R - Is * ep / vt1 - Is * en / vt2;
                    const double step = f / df;
                    yy -= std::max (-0.2, std::min (0.2, step));
                    if (std::abs (step) < 1.0e-12) break;
                }
                y[i] = yy;
                guess = yy;
            }
        }
        inline double at (double x) const noexcept
        {
            double p = (x + kRange) * (kSize / (2.0 * kRange));
            if (p <= 0.0) return y[0] + (x + kRange) * 1.0e-4;
            if (p >= kSize) return y[kSize] + (x - kRange) * 1.0e-4;
            const int i = (int) p;
            return catmullRom (y, i, kSize + 1, p - i);
        }
    };
    static const Table& table()
    {
        static const Table t = [] { Table tt; tt.build(); return tt; }();
        return t;
    }
};

} // namespace uvp
