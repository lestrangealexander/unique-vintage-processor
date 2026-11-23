// Unique Vintage Processor — user presets (saved as XML files the user owns)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : state (s) {}

    static juce::File getFolder()
    {
       #if JUCE_MAC
        auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile ("Library/Audio/Presets/Sound Aquarium/Unique Vintage Processor");
       #else
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Sound Aquarium/Unique Vintage Processor/Presets");
       #endif
        if (! dir.exists()) dir.createDirectory();
        return dir;
    }
    static constexpr const char* kExtension = ".uvpreset";

    juce::Array<juce::File> list() const
    {
        auto files = getFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension);
        files.sort();
        return files;
    }

    bool save (const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName.trim());
        if (name.isEmpty()) return false;
        auto xml = state.copyState().createXml();
        if (xml == nullptr) return false;
        xml->setAttribute ("presetName", name);
        const auto file = getFolder().getChildFile (name + kExtension);
        if (! xml->writeTo (file)) return false;
        currentName = name;
        return true;
    }

    bool load (const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType())) return false;
        state.replaceState (juce::ValueTree::fromXml (*xml));
        currentName = file.getFileNameWithoutExtension();
        return true;
    }

    bool remove (const juce::File& file)
    {
        const bool ok = file.deleteFile();
        if (ok && file.getFileNameWithoutExtension() == currentName) currentName = "default";
        return ok;
    }

    /** Step through the user's presets (wraps around). */
    bool step (int direction)
    {
        const auto files = list();
        if (files.isEmpty()) return false;
        int idx = -1;
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileNameWithoutExtension() == currentName) idx = i;
        idx = idx < 0 ? (direction > 0 ? 0 : files.size() - 1) : (idx + direction + files.size()) % files.size();
        return load (files[idx]);
    }

    void resetToDefault()
    {
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rp->getDefaultValue());
        currentName = "default";
    }

    juce::String currentName { "default" };

private:
    juce::AudioProcessorValueTreeState& state;
};
