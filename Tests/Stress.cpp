// Developer tool: plugin-level robustness checks (what a validator would exercise):
// sample rates, odd/oversized blocks, mono, random automation, constant latency, state round trip.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void check (bool ok, const char* what) { if (! ok) { ++failures; std::printf ("  FAIL: %s\n", what); } }

void randomise (UniqueVintageProcessor& p, juce::Random& r)
{
    for (auto* prm : p.getParameters())
        if (prm != p.getBypassParameter() || r.nextInt (8) == 0)
            prm->setValueNotifyingHost (r.nextFloat());
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::Random rng (12345);

    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int chans : { 2, 1 })
        {
            UniqueVintageProcessor proc;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (chans == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono());
            layout.outputBuses.add (chans == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono());
            check (proc.setBusesLayout (layout), "bus layout accepted");
            proc.setPlayConfigDetails (chans, chans, sr, 512);
            proc.prepareToPlay (sr, 512);
            const int lat0 = proc.getLatencySamples();
            float peak = 0.0f;
            bool finite = true;
            double ph = 0.0;
            juce::MidiBuffer midi;
            const int sizes[] = { 1, 17, 64, 333, 512, 2048, 4096 };
            int processed = 0, nextChange = 0;
            while (processed < (int) (sr * 4.0))
            {
                const int n = sizes[rng.nextInt ((int) std::size (sizes))];
                juce::AudioBuffer<float> buf (chans, n);
                for (int i = 0; i < n; ++i)
                {
                    ph += 2.0 * juce::MathConstants<double>::pi * 3150.0 / sr;
                    const float x = 0.25f * (rng.nextFloat() * 2.0f - 1.0f) + 0.3f * (float) std::sin (ph);
                    for (int c = 0; c < chans; ++c) buf.setSample (c, i, x);
                }
                if (processed >= nextChange) { randomise (proc, rng); nextChange = processed + (int) (sr * 0.05); }
                proc.processBlock (buf, midi);
                for (int c = 0; c < chans; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        const float y = buf.getSample (c, i);
                        if (! std::isfinite (y)) finite = false;
                        peak = std::max (peak, std::abs (y));
                    }
                processed += n;
            }
            check (finite, "output finite");
            check (peak < 40.0f, "output bounded");
            check (proc.getLatencySamples() == lat0, "latency constant across settings");
            std::printf ("%6.0f Hz %s: latency %d (%.1f ms), peak %.2f, %s\n", sr, chans == 2 ? "stereo" : "mono  ", lat0,
                         1000.0 * lat0 / sr, peak, finite ? "finite" : "NON-FINITE");
        }

    // state round trip
    {
        UniqueVintageProcessor a, b;
        randomise (a, rng);
        a.uiScale.store (1.25f);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        b.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (int i = 0; i < a.getParameters().size(); ++i)
            // compare what the parameter means (a bool set to 0.64 is simply "on")
            if (a.getParameters()[i]->getCurrentValueAsText() != b.getParameters()[i]->getCurrentValueAsText())
            {
                same = false;
                std::printf ("  %s: %f vs %f\n", a.getParameters()[i]->getName (64).toRawUTF8(), a.getParameters()[i]->getValue(), b.getParameters()[i]->getValue());
            }
        check (same, "state round trip restores every parameter");
        check (std::abs (b.uiScale.load() - 1.25f) < 1.0e-6f, "state round trip restores UI size");
        std::printf ("state round trip: %s\n", same ? "ok" : "MISMATCH");
    }

    // MIX 0 % through the whole plug-in, from the very first sample after prepareToPlay:
    // the output is the input, delayed by the reported latency, at the heaviest settings
    {
        UniqueVintageProcessor p;
        auto setv = [&p] (const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        setv (ParamIDs::mode, 2.0f);
        setv (ParamIDs::amount, 11.0f);
        setv (ParamIDs::input, 6.0f);
        setv (ParamIDs::output, -4.0f);
        setv (ParamIDs::moreColour, 1.0f);
        setv (ParamIDs::tapeModel, 3.0f);
        setv (ParamIDs::tapeDrive, 6.0f);
        setv (ParamIDs::lpfFreq, 12000.0f);
        setv (ParamIDs::mix, 0.0f);
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        const int lat = p.getLatencySamples();
        const int total = 48000 * 2;
        juce::Random r2 (9);
        std::vector<float> inL ((size_t) total), inR ((size_t) total), outL ((size_t) total), outR ((size_t) total);
        for (int i = 0; i < total; ++i) { inL[(size_t) i] = 0.5f * (r2.nextFloat() * 2.0f - 1.0f); inR[(size_t) i] = 0.5f * (r2.nextFloat() * 2.0f - 1.0f); }
        juce::MidiBuffer midi;
        for (int pos = 0; pos < total;)
        {
            const int n = std::min (total - pos, 1 + r2.nextInt (700));
            juce::AudioBuffer<float> b (2, n);
            b.copyFrom (0, 0, inL.data() + pos, n);
            b.copyFrom (1, 0, inR.data() + pos, n);
            p.processBlock (b, midi);
            std::copy (b.getReadPointer (0), b.getReadPointer (0) + n, outL.begin() + pos);
            std::copy (b.getReadPointer (1), b.getReadPointer (1) + n, outR.begin() + pos);
            pos += n;
        }
        double worst = 0.0;
        for (int i = lat; i < total; ++i)
            worst = std::max ({ worst, (double) std::abs (outL[(size_t) i] - inL[(size_t) (i - lat)]),
                                (double) std::abs (outR[(size_t) i] - inR[(size_t) (i - lat)]) });
        check (worst == 0.0, "MIX 0 % gives back the input exactly (whole plug-in, from the first sample)");
        std::printf ("MIX 0%% through the plug-in (HECTIC 11, IN +6, OUT -4, tape GARAGE +6): max |out - input| = %g\n", worst);
    }

    // editor opens and closes cleanly
    {
        UniqueVintageProcessor p;
        p.prepareToPlay (48000.0, 512);
        for (int i = 0; i < 3; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            check (ed != nullptr && ed->getWidth() > 0, "editor created");
        }
        std::printf ("editor open/close x3: ok\n");
    }

    std::printf (failures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
