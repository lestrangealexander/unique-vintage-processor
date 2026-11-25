// Unique Vintage Processor — editor (faceplate v4)
//
// Everything is laid out on a fixed 1000 x 660 faceplate (+300 when the analyser is expanded),
// in the same coordinates as the approved mockup, then scaled as a whole for the UI size option.
// Type-in values open inside the plugin window. Saving and loading preset files use the system's
// own file windows: they sit above any floating plugin window and get the keyboard whatever the
// DAW does with keystrokes in plugin windows.
#include "PluginEditor.h"
#include "ParamText.h"

using namespace uvp::ui;

namespace
{
    const char* const kTapeNames[] = { "TAPE SILK", "TAPE SOLID", "TAPE HOME", "TAPE GARAGE", "TAPE CASSETTE" };
    const char* const kOsNames[] = { "1X", "2X", "4X", "8X", "16X" };
    constexpr float kUiScales[] = { 0.75f, 0.9f, 1.0f, 1.25f, 1.5f, 2.0f };

    juce::RangedAudioParameter& param (UniqueVintageProcessor& p, const char* id) { return *p.apvts.getParameter (id); }

    /** Alt/Option-click returns a slider to its default (double-click is type-in). */
    void setDefaultOnAltClick (juce::Slider& s, juce::RangedAudioParameter& p)
    {
        s.setDoubleClickReturnValue (true, (double) p.convertFrom0to1 (p.getDefaultValue()));
    }

    void setFromTyped (juce::RangedAudioParameter& p, float value)
    {
        value = p.getNormalisableRange().snapToLegalValue (value);
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (value));
        p.endChangeGesture();
    }
} // namespace

//==============================================================================
class MoreColourPanel final : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    MoreColourPanel()
    {
        for (int i = 0; i < 5; ++i)
        {
            auto* b = items.add (new ListItemButton (kTapeNames[i]));
            b->onClick = [this, i] { if (onSelect) onSelect (i); };
            addAndMakeVisible (b);
        }
    }

    void setSelected (int index)
    {
        for (int i = 0; i < items.size(); ++i)
            items[i]->setToggleState (i == index, juce::dontSendNotification);
    }

    void resized() override
    {
        for (int i = 0; i < items.size(); ++i)
            items[i]->setBounds (6, 31 + i * 22, getWidth() - 12, 21);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::white);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
        drawText (g, "MORE COLOUR", 14.0f, 21.0f, font (Weight::Bold, 12.5f, 1.6f), col::ink, Anchor::Start);
    }

private:
    juce::OwnedArray<ListItemButton> items;
};

//==============================================================================
class UVPEditor::Faceplate final : public juce::Component
{
public:
    Faceplate (UniqueVintageProcessor& p, UVPEditor& e)
        : proc (p), editor (e),
          filter (param (p, ParamIDs::hpfFreq), param (p, ParamIDs::lpfFreq), UniqueVintageProcessor::kLpfTopHz)
    {
        logo = pegasus (col::white);

        // ---- top strip
        for (auto* b : { &bypass, &delta })
        {
            b->setClickingTogglesState (true);
            addAndMakeVisible (b);
        }
        bypass.setBounds (18, 9, 84, 24);
        delta.setBounds (110, 9, 70, 24);
        bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::bypass, bypass);
        deltaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::delta, delta);

        addAndMakeVisible (osPill);
        osPill.setBounds (680, 9, 56, 24);
        osPill.onClick = [this] { editor.showOsMenu(); };

        addAndMakeVisible (menuZone);
        addAndMakeVisible (saveZone);
        menuZone.setBounds (752, 9, 148, 24);
        saveZone.setBounds (900, 9, 82, 24);
        menuZone.onClick = [this] { editor.showPresetMenu(); };
        saveZone.onClick = [this] { editor.chooseAndSavePreset(); };

        // ---- INPUT / AG / OUTPUT / MIX
        for (auto* k : { &input, &output, &mix, &drive, &amount })
        {
            k->setInterceptsMouseClicks (false, true);
            addAndMakeVisible (k);
        }
        input.placeAt (700.0f, 88.0f);
        output.placeAt (848.0f, 88.0f);
        mix.placeAt (930.0f, 88.0f);
        drive.placeAt (150.0f, 357.0f);
        amount.placeAt (420.0f, 352.0f);
        attach (input, ParamIDs::input);
        attach (output, ParamIDs::output);
        attach (mix, ParamIDs::mix);
        attach (drive, ParamIDs::tapeDrive);
        attach (amount, ParamIDs::amount);

        ag.setClickingTogglesState (true);
        addAndMakeVisible (ag);
        ag.setBounds (754, 68, 40, 40);
        agAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::autoGain, ag);

        // ---- harmonics modes (one choice parameter) and MORE COLOUR
        ModeButton* modes[] = { &even, &odd, &hectic };
        for (int i = 0; i < 3; ++i)
        {
            addAndMakeVisible (modes[i]);
            modes[i]->setBounds (28, 170 + 52 * i, 128, 42);
            modes[i]->onClick = [this, i] { modeAtt->setValueAsCompleteGesture ((float) i); };
        }
        modeAtt = std::make_unique<juce::ParameterAttachment> (param (proc, ParamIDs::mode), [this] (float v)
        {
            const int m = juce::roundToInt (v);
            even.setToggleState (m == 0, juce::dontSendNotification);
            odd.setToggleState (m == 1, juce::dontSendNotification);
            hectic.setToggleState (m == 2, juce::dontSendNotification);
        });

        addAndMakeVisible (mc);
        mc.setBounds (28, 338, 92, 42);
        mc.onClick = [this] { mcAtt->setValueAsCompleteGesture (mc.getToggleState() ? 0.0f : 1.0f); };
        mcAtt = std::make_unique<juce::ParameterAttachment> (param (proc, ParamIDs::moreColour), [this] (float v)
        {
            const bool on = v > 0.5f;
            mc.setToggleState (on, juce::dontSendNotification);
            mcPanel.setVisible (on);
            drive.setVisible (on);
        });

        addChildComponent (mcPanel);
        mcPanel.setBounds (28, 400, 172, 148);
        mcPanel.onSelect = [this] (int i) { tapeAtt->setValueAsCompleteGesture ((float) i); };
        tapeAtt = std::make_unique<juce::ParameterAttachment> (param (proc, ParamIDs::tapeModel),
                                                               [this] (float v) { mcPanel.setSelected (juce::roundToInt (v)); });

        osAtt = std::make_unique<juce::ParameterAttachment> (param (proc, ParamIDs::oversampling), [this] (float v)
        {
            osPill.setLabel (kOsNames[juce::jlimit (0, 4, juce::roundToInt (v))]);
        });

        // ---- suppression meter
        addAndMakeVisible (vu);
        vu.setBounds (760 - 126, 340 - 126, 252, 252);

        // ---- Q slider
        q.setSliderStyle (juce::Slider::LinearHorizontal);
        q.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        q.setSliderSnapsToMousePosition (false);
        q.setScrollWheelEnabled (true);
        q.getProperties().set ("fill", true);
        q.getProperties().set ("valueOnHover", true);
        q.getProperties().set ("decimals", 1);
        addAndMakeVisible (q);
        q.setBounds (66 - 11, 589 - 33, (400 - 66) + 22, 44);
        qAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ParamIDs::q, q);
        setDefaultOnAltClick (q, param (proc, ParamIDs::q));
        q.onDoubleClick = [this]
        {
            const float x = (float) q.getX() + (float) q.getPositionOfValue (q.getValue());
            openEntry (param (proc, ParamIDs::q), { x, 589.0f - 22.0f }, 76, false);
        };

        // ---- FILTER: high-pass and low-pass edges on the analyser's frequency axis
        addAndMakeVisible (filter);
        filter.setBounds (0, 632 - juce::roundToInt (FilterRange::kTrackY), kWidth, 46);
        filter.onDoubleClick = [this] (int handle, juce::Point<float> at)
        {
            auto& p = filter.paramFor (handle);
            openEntry (p, at.translated ((float) filter.getX(), (float) filter.getY() - 22.0f), 92, true,
                       handle == 1 ? std::optional<float> (UniqueVintageProcessor::kLpfTopHz) : std::nullopt,
                       [this, handle] (float hz) { return filter.clampFor (handle, hz); });
        };

        addAndMakeVisible (filterPill);
        const int pillW = juce::roundToInt (textWidth (font (Weight::Bold, 10.0f, 0.9f), "FILTER") + 15.0f);
        filterPill.setBounds (57 - pillW, 620, pillW, 24);
        filterPill.onClick = [this] { filterOnAtt->setValueAsCompleteGesture (filterPill.getToggleState() ? 0.0f : 1.0f); };
        filterOnAtt = std::make_unique<juce::ParameterAttachment> (param (proc, ParamIDs::hpfOn), [this] (float v)
        {
            const bool on = v > 0.5f;
            filterPill.setToggleState (on, juce::dontSendNotification);
            filter.setAlpha (on ? 1.0f : 0.45f);
        });

        // ---- analyser toggle
        addAndMakeVisible (expand);
        expand.setBounds (930, 560, 64, 40);
        expand.onClick = [this] { editor.setExpanded (! expand.getToggleState()); };
        addChildComponent (analyser);
        analyser.setBounds (0, kMainHeight, kWidth, kAnalyserHeight);

        // ---- type-in box, on top of everything
        addChildComponent (entry);
        addMouseListener (&clickAway, true); // a click anywhere else applies an open type-in box

        for (auto* a : { modeAtt.get(), mcAtt.get(), tapeAtt.get(), osAtt.get(), filterOnAtt.get() })
            a->sendInitialUpdate();
    }

    ~Faceplate() override { removeMouseListener (&clickAway); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::salmon);
        g.setColour (col::yellow);
        g.fillRect (0, 0, kWidth, 42);
        drawText (g, "OS", 662.0f, 26.0f, font (Weight::SemiBold, 13.0f, 1.6f), col::ink);
        g.setColour (col::white);
        g.fillRoundedRectangle (752.0f, 9.0f, 230.0f, 24.0f, 12.0f);
        juce::Path divider;
        divider.startNewSubPath (900.0f, 14.0f);
        divider.lineTo (900.0f, 28.0f);
        g.setColour (col::ink.withAlpha (0.3f));
        strokeRound (g, divider, 1.5f);

        if (logo != nullptr)
            logo->drawWithin (g, { 24.0f, 58.0f, 66.0f * 1218.0f / 1249.0f, 66.0f }, juce::RectanglePlacement::centred, 1.0f);
        drawText (g, "UNIQUE VINTAGE PROCESSOR", 104.0f, 89.0f, font (Weight::Bold, 27.0f, 2.6f), col::white, Anchor::Start);
        drawText (g, "Sound Aquarium Recording", 105.0f, 114.0f, font (Weight::Medium, 15.5f, 0.3f), col::white, Anchor::Start);

        const auto p0 = polar (420.0f, 352.0f, 154.0f, -135.0f), p1 = polar (420.0f, 352.0f, 154.0f, 135.0f);
        drawText (g, "0", p0.x - 4.0f, p0.y + 8.0f, font (Weight::SemiBold, 17.0f), col::ink);
        drawText (g, "11", p1.x + 4.0f, p1.y + 8.0f, font (Weight::SemiBold, 17.0f), col::ink);
        drawText (g, "SUPPRESSION VU", 760.0f, 528.0f, font (Weight::Bold, 15.0f, 3.2f), col::ink);

        drawText (g, "Q", 30.0f, 595.0f, font (Weight::Bold, 16.0f), col::ink, Anchor::Start);
        const auto small = font (Weight::SemiBold, 9.5f, 1.4f);
        drawText (g, "BROAD", 66.0f, 613.0f, small, col::ink.withAlpha (0.6f), Anchor::Start);
        drawText (g, "NARROW", 400.0f, 613.0f, small, col::ink.withAlpha (0.6f), Anchor::End);
        const auto tiny = font (Weight::SemiBold, 9.5f);
        drawText (g, "20", Analyser::freqToX (20.0f), 655.0f, tiny, col::ink.withAlpha (0.5f));
        drawText (g, "20K", Analyser::freqToX (20000.0f), 655.0f, tiny, col::ink.withAlpha (0.5f));
    }

    void attach (ParamKnob& k, const char* id)
    {
        sliderAtts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, k.slider));
        auto& p = param (proc, id);
        setDefaultOnAltClick (k.slider, p);
        k.slider.onDoubleClick = [this, &k, &p]
        {
            const bool big = k.getKnobRadius() > 60.0f;
            openEntry (p, k.getKnobCentre(), big ? 100 : 80, false);
        };
    }

    /** Type-in box centred on `at` (faceplate coordinates); the typed value is clamped to the parameter. */
    void openEntry (juce::RangedAudioParameter& p, juce::Point<float> at, int width, bool isFrequency,
                    std::optional<float> offValue = std::nullopt, std::function<float (float)> constrain = {})
    {
        constexpr int h = 30;
        auto area = juce::Rectangle<int> (width, h).withCentre (at.roundToInt());
        area = area.constrainedWithin ({ 4, 4, kWidth - 8, kMainHeight - 8 });
        entry.open (area, p.getCurrentValueAsText(), [&p, isFrequency, offValue, constrain] (const juce::String& text)
        {
            if (auto v = uvp::parseTypedValue (text, isFrequency, offValue))
                setFromTyped (p, constrain != nullptr ? constrain (*v) : *v);
        });
    }

    UniqueVintageProcessor& proc;
    UVPEditor& editor;
    std::unique_ptr<juce::Drawable> logo;

    PillButton bypass { "BYPASS" }, delta { "DELTA" }, osPill { "4X", 13.0f, 0.6f, Weight::Bold },
               filterPill { "FILTER", 10.0f, 0.9f, Weight::Bold };
    ZoneButton menuZone { "MENU", true, true }, saveZone { "SAVE", false, false };
    ParamKnob input { "INPUT", 21.0f, 4.0f, true, 53.0f, 11.0f, 1.8f };
    ParamKnob output { "OUTPUT", 21.0f, 4.0f, true, 53.0f, 11.0f, 1.8f };
    ParamKnob mix { "MIX", 21.0f, 4.0f, false, 53.0f, 11.0f, 1.8f };
    ParamKnob drive { "DRIVE", 13.0f, 3.0f, true, 34.0f, 9.5f, 1.4f };
    ParamKnob amount { "AMOUNT", 116.0f, 10.0f, false, 176.0f, 15.0f, 3.2f, Weight::Bold, true };
    RoundButton ag { "AG" };
    ModeButton even { "EVEN" }, odd { "ODD" }, hectic { "HECTIC" }, mc { "MC" };
    MoreColourPanel mcPanel;
    VuMeter vu;
    ValueSlider q;
    FilterRange filter;
    ExpandButton expand;
    Analyser analyser;
    ValueEntry entry;

    struct ClickAway final : juce::MouseListener
    {
        explicit ClickAway (ValueEntry& e) : entry (e) {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (entry.isVisible() && e.eventComponent != &entry && ! entry.isParentOf (e.eventComponent))
                entry.dismiss (true);
        }
        ValueEntry& entry;
    } clickAway { entry };

    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> qAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt, deltaAtt, agAtt;
    std::unique_ptr<juce::ParameterAttachment> modeAtt, mcAtt, tapeAtt, osAtt, filterOnAtt;
};

//==============================================================================
UVPEditor::UVPEditor (UniqueVintageProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    face = std::make_unique<Faceplate> (p, *this);
    addAndMakeVisible (*face);
    face->setBounds (0, 0, kWidth, kMainHeight + kAnalyserHeight);
    applySize();
    startTimerHz (30);
}

UVPEditor::~UVPEditor()
{
    stopTimer();
    fileChooser.reset(); // closes an open Save / Load window without calling back
    face.reset();
    setLookAndFeel (nullptr);
}

void UVPEditor::applySize()
{
    const float s = juce::jlimit (0.5f, 3.0f, proc.uiScale.load());
    face->setTransform (juce::AffineTransform::scale (s));
    setSize (juce::roundToInt ((float) kWidth * s), juce::roundToInt ((float) (kMainHeight + (expanded ? kAnalyserHeight : 0)) * s));
}

void UVPEditor::resized() {}

void UVPEditor::setExpanded (bool shouldExpand)
{
    expanded = shouldExpand;
    face->expand.setToggleState (expanded, juce::dontSendNotification);
    face->analyser.setVisible (expanded);
    applySize();
}

void UVPEditor::timerCallback()
{
    // VU ballistics (~300 ms) on the suppressor's peak gain reduction
    const float target = proc.getEngine().getSuppressionMeterDb();
    vuValue += (target - vuValue) * (1.0f - std::exp (-1.0f / (30.0f * 0.3f)));
    face->vu.setReduction (vuValue < 0.02f ? 0.0f : vuValue);

    if (savedFlashTicks > 0 && --savedFlashTicks == 0)
        face->saveZone.setButtonText ("SAVE");
    refreshPresetLabel();

    if (expanded)
    {
        const uvp::DisplayFrame* frame = nullptr;
        if (proc.getEngine().getDisplay().read (frame) && frame != nullptr)
            face->analyser.setFrame (*frame);
        const float lpHz = proc.apvts.getRawParameterValue (ParamIDs::lpfFreq)->load();
        face->analyser.setBand (proc.apvts.getRawParameterValue (ParamIDs::hpfFreq)->load(), lpHz,
                                UniqueVintageProcessor::lpfEngaged (lpHz),
                                proc.apvts.getRawParameterValue (ParamIDs::hpfOn)->load() > 0.5f);
    }
}

void UVPEditor::showOsMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("OVERSAMPLING");
    const int current = juce::roundToInt (proc.apvts.getRawParameterValue (ParamIDs::oversampling)->load());
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, kOsNames[i], true, i == current);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&face->osPill),
                     [this] (int r)
                     {
                         if (r <= 0) return;
                         auto& p = param (proc, ParamIDs::oversampling);
                         p.beginChangeGesture();
                         p.setValueNotifyingHost (p.convertTo0to1 ((float) (r - 1)));
                         p.endChangeGesture();
                     });
}

void UVPEditor::showPresetMenu()
{
    const auto files = proc.presets.list();
    const auto current = proc.presets.currentFile;
    juce::PopupMenu m;
    m.addSectionHeader ("UNIQUE VINTAGE PROCESSOR " UVP_VERSION_STRING);
    m.addSectionHeader ("PRESETS");
    if (files.isEmpty())
        m.addItem (-1, "No presets saved yet", false, false);
    for (int i = 0; i < files.size(); ++i)
        m.addItem (1000 + i, PresetManager::displayName (files[i]), true, files[i] == current);
    m.addSeparator();
    m.addItem (1, "Previous preset", ! files.isEmpty());
    m.addItem (2, "Next preset", ! files.isEmpty());
    m.addItem (3, "Save preset...");
    m.addItem (7, "Load preset file...");
    m.addItem (4, "Delete \"" + proc.presets.currentName + "\"", current.existsAsFile());
    m.addItem (5, "Reset to default");
    m.addItem (6, "Show presets folder");
    m.addSeparator();
    juce::PopupMenu sizes;
    for (int i = 0; i < (int) std::size (kUiScales); ++i)
        sizes.addItem (200 + i, juce::String (juce::roundToInt (kUiScales[i] * 100.0f)) + " %", true,
                       std::abs (proc.uiScale.load() - kUiScales[i]) < 0.01f);
    m.addSubMenu ("Interface size", sizes);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&face->menuZone),
                     [this, files, current] (int r)
                     {
                         if (r >= 1000 && r - 1000 < files.size()) proc.presets.load (files[r - 1000]);
                         else if (r >= 200 && r < 200 + (int) std::size (kUiScales)) { proc.uiScale.store (kUiScales[r - 200]); applySize(); }
                         else if (r == 1) proc.presets.step (-1);
                         else if (r == 2) proc.presets.step (1);
                         else if (r == 3) chooseAndSavePreset();
                         else if (r == 4) proc.presets.remove (current);
                         else if (r == 5) proc.presets.resetToDefault();
                         else if (r == 6) PresetManager::getFolder().startAsProcess();
                         else if (r == 7) chooseAndLoadPreset();
                         refreshPresetLabel();
                     });
}

void UVPEditor::chooseAndSavePreset()
{
    const auto current = proc.presets.currentFile;
    const auto start = current.existsAsFile() ? current
                                              : PresetManager::getFolder().getChildFile (PresetManager::suggestName() + PresetManager::kExtension);
    fileChooser = std::make_unique<juce::FileChooser> ("Save preset", start, juce::String ("*") + PresetManager::kExtension);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<UVPEditor> (this)] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (safe != nullptr && file != juce::File())
                                      safe->savePresetTo (file);
                              });
}

void UVPEditor::chooseAndLoadPreset()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load preset", PresetManager::getFolder(), juce::String ("*") + PresetManager::kExtension);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<UVPEditor> (this)] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (safe == nullptr || file == juce::File()) return;
                                  if (! safe->proc.presets.load (file))
                                      safe->showProblem ("Preset not loaded", file.getFileName() + " isn't a Unique Vintage Processor preset.");
                                  safe->refreshPresetLabel();
                              });
}

bool UVPEditor::savePresetTo (const juce::File& file)
{
    const auto result = proc.presets.saveTo (file);
    if (result.failed())
    {
        showProblem ("Preset not saved", result.getErrorMessage());
        return false;
    }
    face->saveZone.setButtonText ("SAVED");
    savedFlashTicks = 45; // 1.5 s
    refreshPresetLabel();
    return true;
}

void UVPEditor::showProblem (const juce::String& title, const juce::String& message)
{
    juce::NativeMessageBox::showForOptionsAsync (juce::MessageBoxOptions()
                                                     .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                     .withTitle (title)
                                                     .withMessage (message)
                                                     .withButton ("OK", 1)
                                                     .withAssociatedComponent (this),
                                                 std::function<void (int)> {});
}

void UVPEditor::refreshPresetLabel()
{
    // the top bar names the preset in use, so a save or load shows straight away
    const auto& f = proc.presets.currentFile;
    const auto label = f != juce::File() ? f.getFileNameWithoutExtension().toUpperCase() : juce::String ("MENU");
    if (face->menuZone.getButtonText() != label)
        face->menuZone.setButtonText (label);
}
