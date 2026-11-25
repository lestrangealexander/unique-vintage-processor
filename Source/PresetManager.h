// Unique Vintage Processor — user presets (XML files the user owns)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : state (s) {}

    static constexpr const char* kExtension = ".uvpreset";

    /** The usual presets folder:
          Mac      ~/Library/Audio/Presets/Sound Aquarium/Unique Vintage Processor
          Windows  %APPDATA%\Sound Aquarium\Unique Vintage Processor\Presets */
    static juce::File primaryFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Presets/Sound Aquarium/Unique Vintage Processor");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Sound Aquarium/Unique Vintage Processor/Presets");
       #endif
    }

    /** Used when the usual folder can't be written (for instance when an installer has left
        ~/Library/Audio/Presets owned by the system). */
    static juce::File fallbackFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Application Support/Sound Aquarium/Unique Vintage Processor/Presets");
       #else
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Sound Aquarium/Unique Vintage Processor Presets");
       #endif
    }

    /** Creates the folder if needed and proves it takes a file. */
    static bool canWriteTo (const juce::File& dir)
    {
        if (! dir.isDirectory() && dir.createDirectory().failed())
            return false;
        const auto probe = dir.getNonexistentChildFile (".uvp-write-check", ".tmp", false);
        const bool ok = probe.replaceWithText ("ok");
        probe.deleteFile();
        return ok;
    }

    /** Where new presets go: the usual folder, or the fallback if that one can't be written. */
    static juce::File getFolder()
    {
        static const juce::File folder = []
        {
            const auto primary = primaryFolder();
            if (canWriteTo (primary)) return primary;
            const auto fallback = fallbackFolder();
            if (canWriteTo (fallback)) return fallback;
            return primary;
        }();
        return folder;
    }

    /** Every preset in the presets folder(s), subfolders included, sorted by name. */
    juce::Array<juce::File> list() const
    {
        juce::Array<juce::File> files;
        for (const auto& dir : { primaryFolder(), fallbackFolder() })
            if (dir.isDirectory())
                for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, juce::String ("*") + kExtension))
                    files.addIfNotAlreadyThere (f);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return displayName (a).compareNatural (displayName (b)) < 0; });
        return files;
    }

    /** Name for menus: the file name, with its subfolder in front if it has one ("Vocals / Air"). */
    static juce::String displayName (const juce::File& f)
    {
        for (const auto& dir : { primaryFolder(), fallbackFolder() })
            if (f.isAChildOf (dir))
                return f.getRelativePathFrom (dir).upToLastOccurrenceOf (".", false, false)
                        .replace ("\\", " / ").replace ("/", " / ");
        return f.getFileNameWithoutExtension();
    }

    static juce::File fileFor (const juce::String& rawName)
    {
        return getFolder().getChildFile (juce::File::createLegalFileName (rawName.trim()) + kExtension);
    }

    /** "Preset 1", "Preset 2", ... whichever is the first not taken. */
    static juce::String suggestName()
    {
        for (int n = 1;; ++n)
            if (! fileFor ("Preset " + juce::String (n)).existsAsFile())
                return "Preset " + juce::String (n);
    }

    /** Writes the current settings to `file` (".uvpreset" is added if missing). */
    juce::Result saveTo (juce::File file)
    {
        if (file == juce::File())
            return juce::Result::fail ("No file was chosen.");
        if (! file.hasFileExtension (kExtension))
            file = file.withFileExtension (kExtension);
        auto xml = state.copyState().createXml();
        if (xml == nullptr)
            return juce::Result::fail ("Couldn't read the plug-in's settings.");
        xml->setAttribute ("presetName", file.getFileNameWithoutExtension());
        if (const auto made = file.getParentDirectory().createDirectory(); made.failed())
            return juce::Result::fail ("Couldn't create the folder " + file.getParentDirectory().getFullPathName() + ".");
        if (! xml->writeTo (file))
            return juce::Result::fail ("Couldn't write " + file.getFullPathName() + ". Check that the folder isn't read-only.");
        currentFile = file;
        currentName = file.getFileNameWithoutExtension();
        return juce::Result::ok();
    }

    /** Saves into the presets folder under a typed name. */
    bool save (const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName.trim());
        return name.isNotEmpty() && saveTo (getFolder().getChildFile (name + kExtension)).wasOk();
    }

    bool load (const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType())) return false;
        state.replaceState (juce::ValueTree::fromXml (*xml));
        currentFile = file;
        currentName = file.getFileNameWithoutExtension();
        return true;
    }

    bool remove (const juce::File& file)
    {
        const bool ok = file.deleteFile();
        if (ok && file == currentFile) forgetCurrent();
        return ok;
    }

    /** Step through the user's presets (wraps around). */
    bool step (int direction)
    {
        const auto files = list();
        if (files.isEmpty()) return false;
        const int idx = files.indexOf (currentFile);
        const int next = idx < 0 ? (direction > 0 ? 0 : files.size() - 1) : (idx + direction + files.size()) % files.size();
        return load (files[next]);
    }

    void resetToDefault()
    {
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rp->getDefaultValue());
        forgetCurrent();
    }

    void forgetCurrent()
    {
        currentFile = juce::File();
        currentName = "default";
    }

    juce::File currentFile;                 // the preset loaded or saved last (it can live anywhere)
    juce::String currentName { "default" };

private:
    juce::AudioProcessorValueTreeState& state;
};
