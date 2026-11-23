// Developer tool: render the plugin editor to PNG with live meter/analyser data.
#include "PluginEditor.h"
#include <cmath>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    UniqueVintageProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    auto set = [&proc] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set (ParamIDs::amount, 4.2f);

    // bright programme: pink-ish noise with a few resonances around 5.6 k / 8.4 k / 12.5 k
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (3);
    float b0 = 0, b1 = 0, b2 = 0;
    double ph[3] = { 0, 0, 0 };
    const double fr[3] = { 5600.0, 8400.0, 12500.0 };
    auto runAudio = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float w = rng.nextFloat() * 2.0f - 1.0f;
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                float x = 0.06f * (b0 + b1 + b2 + w * 0.1848f);
                for (int k = 0; k < 3; ++k) { ph[k] += 2.0 * juce::MathConstants<double>::pi * fr[k] / 48000.0; x += 0.02f * (float) std::sin (ph[k]); }
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            proc.processBlock (buf, midi);
        }
    };

    auto* ed = dynamic_cast<UVPEditor*> (proc.createEditor());
    ed->setVisible (true);
    auto shoot = [&] (const char* name)
    {
        for (int i = 0; i < 40; ++i) { runAudio (4); juce::MessageManager::getInstance()->runDispatchLoopUntil (15); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };
    shoot ("editor_main.png");
    set (ParamIDs::moreColour, 1.0f);
    ed->showAnalyser (true);
    shoot ("editor_expanded.png");
    delete ed;
    return 0;
}
