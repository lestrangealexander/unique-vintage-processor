// Developer tool: render the plugin editor to PNG with live meter/analyser data, and drive the
// in-window dialogs the way a user would (type-in values, saving a preset).
#include "PluginEditor.h"
#include <cmath>

namespace
{
template <typename T>
T* findShowing (juce::Component& root, std::function<bool (T&)> pred = {})
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c))
            if (c->isVisible() && (pred == nullptr || pred (*t)))
                return t;
        if (auto* deeper = findShowing<T> (*c, pred))
            return deeper;
    }
    return nullptr;
}

int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

void pressReturn (juce::TextEditor& te) { te.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    UniqueVintageProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    auto set = [&proc] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    auto get = [&proc] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
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
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    auto* ed = dynamic_cast<UVPEditor*> (proc.createEditor());
    ed->setVisible (true);
    auto shoot = [&] (const char* name, int rounds = 40)
    {
        for (int i = 0; i < rounds; ++i) { runAudio (4); pump (15); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };
    shoot ("editor_main.png");

    std::printf ("type-in values:\n");
    using uvp::ui::ParamKnob;
    using uvp::ui::ValueEntry;
    using uvp::ui::FilterRange;
    using uvp::ui::Analyser;
    auto* amountKnob = findShowing<ParamKnob> (*ed, [] (ParamKnob& k) { return k.getKnobRadius() > 60.0f; });
    check (amountKnob != nullptr, "found AMOUNT");
    if (amountKnob != nullptr)
    {
        amountKnob->slider.onDoubleClick();
        auto* entry = findShowing<ValueEntry> (*ed);
        check (entry != nullptr, "double-click opens a type-in box");
        if (entry != nullptr)
        {
            check (entry->getText() == "4.2", "box starts with the current value");
            shoot ("editor_typein.png", 4);
            entry->setText ("7.5", false);
            pressReturn (*entry);
            pump (50);
            check (std::abs (get (ParamIDs::amount) - 7.5f) < 0.001f, "AMOUNT 7.5 + Enter -> 7.5");
            check (! entry->isVisible(), "box closes after Enter");

            amountKnob->slider.onDoubleClick();
            entry->setText ("42", false);
            pressReturn (*entry);
            pump (50);
            check (std::abs (get (ParamIDs::amount) - 11.0f) < 0.001f, "AMOUNT 42 is held at 11");

            amountKnob->slider.onDoubleClick();
            entry->setText ("3", false);
            entry->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            pump (50);
            check (std::abs (get (ParamIDs::amount) - 11.0f) < 0.001f && ! entry->isVisible(), "Esc cancels");

            // real mouse events through a window: double-click AMOUNT, type, then click elsewhere
            ed->addToDesktop (0);
            if (auto* peer = ed->getPeer())
            {
                auto t = juce::Time::currentTimeMillis();
                auto press = [&] (juce::Point<float> at, bool down)
                {
                    peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at,
                                            down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
                                            juce::MouseInputSource::defaultPressure, 0.0f, t += 25);
                };
                auto clickAt = [&] (juce::Point<float> at) { press (at, true); press (at, false); };
                clickAt ({ 420.0f, 352.0f });
                clickAt ({ 420.0f, 352.0f });
                pump (50);
                check (entry->isVisible(), "a real double-click on AMOUNT opens the box");
                entry->setText ("6", false);
                t += 1000;
                clickAt ({ 500.0f, 560.0f });
                pump (50);
                check (std::abs (get (ParamIDs::amount) - 6.0f) < 0.001f && ! entry->isVisible(), "clicking elsewhere applies");
                t += 1000;
                clickAt ({ Analyser::freqToX (20000.0f), 632.0f });
                clickAt ({ Analyser::freqToX (20000.0f), 632.0f });
                pump (50);
                check (entry->isVisible() && entry->getText() == "Off", "a real double-click on the low-pass handle opens its box");
                entry->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
                pump (50);
            }
            else
                check (false, "editor window");
            ed->removeFromDesktop();
        }
    }
    auto* filter = findShowing<FilterRange> (*ed);
    check (filter != nullptr, "found the FILTER slider");
    if (filter != nullptr && filter->onDoubleClick != nullptr)
    {
        auto typeInto = [&] (int handle, const char* text)
        {
            filter->onDoubleClick (handle, { filter->handleX (handle), FilterRange::kTrackY });
            if (auto* entry = findShowing<ValueEntry> (*ed))
            {
                entry->setText (text, false);
                pressReturn (*entry);
            }
            pump (50);
        };
        check (! UniqueVintageProcessor::lpfEngaged (get (ParamIDs::lpfFreq)), "low-pass starts off (at the top)");
        typeInto (1, "9k");
        check (std::abs (get (ParamIDs::lpfFreq) - 9000.0f) < 1.0f, "low-pass '9k' -> 9 kHz");
        typeInto (0, "2.5");
        check (std::abs (get (ParamIDs::hpfFreq) - 2500.0f) < 1.0f, "high-pass '2.5' -> 2.5 kHz");
        typeInto (0, "12k");
        check (std::abs (get (ParamIDs::hpfFreq) - 9000.0f) < 1.0f, "high-pass can't pass the low-pass (12k -> 9k)");
        typeInto (0, "250 Hz");
        check (std::abs (get (ParamIDs::hpfFreq) - 250.0f) < 0.5f, "high-pass '250 Hz' -> 250 Hz");
        typeInto (1, "off");
        check (! UniqueVintageProcessor::lpfEngaged (get (ParamIDs::lpfFreq)), "low-pass 'off' -> off");
        set (ParamIDs::hpfFreq, 4000.0f);
    }

    std::printf ("saving a preset:\n");
    const auto presetFile = PresetManager::fileFor ("Snapshot Test Preset");
    presetFile.deleteFile();
    if (auto* saveZone = findShowing<uvp::ui::ZoneButton> (*ed, [] (uvp::ui::ZoneButton& b) { return b.getButtonText() == "SAVE"; }))
    {
        saveZone->triggerClick();
        pump (100);
        auto* field = findShowing<juce::TextEditor> (*ed, [] (juce::TextEditor& t) { return dynamic_cast<ValueEntry*> (&t) == nullptr; });
        check (field != nullptr, "SAVE opens the name box inside the plugin window");
        if (field != nullptr)
        {
            check (field->getText().startsWith ("Preset "), "suggests a name");
            field->setText ("Snapshot Test Preset", false);
            shoot ("editor_save.png", 4);
            set (ParamIDs::amount, 5.5f);
            pressReturn (*field);
            pump (100);
            check (presetFile.existsAsFile(), "Enter writes the preset file");
            check (! field->isShowing(), "dialog closes");
            check (saveZone->getButtonText() == "SAVED", "SAVE shows SAVED");
            set (ParamIDs::amount, 1.0f);
            check (proc.presets.load (presetFile) && std::abs (get (ParamIDs::amount) - 5.5f) < 0.001f, "preset loads back");
        }
    }
    else
        check (false, "found SAVE");
    presetFile.deleteFile();

    // the look with the low-pass in, ODD, MORE COLOUR and the analyser open
    set (ParamIDs::amount, 4.2f);
    set (ParamIDs::lpfFreq, 12000.0f);
    set (ParamIDs::mode, 1.0f);
    set (ParamIDs::moreColour, 1.0f);
    ed->showAnalyser (true);
    shoot ("editor_expanded.png");
    set (ParamIDs::hpfOn, 0.0f);
    shoot ("editor_filter_off.png", 4);
    delete ed;
    std::printf (failures == 0 ? "all checks passed\n" : "%d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
