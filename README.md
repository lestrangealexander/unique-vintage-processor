# Unique Vintage Processor

**Sound Aquarium Audio by Alex L’Estrange.** This is a top-end processor modelled on what a vinyl cut does to high frequencies. It has three stages:

1. A dynamic resonance suppressor that runs from broad lathe-limiter dips (BROAD) to surgical notches (NARROW).
2. Analog harmonics: EVEN (valve), ODD (transformer) or HECTIC (valve, then transformer, then clipper).
3. Optional MORE COLOUR tape machines: SILK, SOLID, HOME, GARAGE and CASSETTE.

The Mac build is AU and VST3, universal for Apple Silicon and Intel, and needs macOS 11 or later. The Windows build is a 64-bit VST3 for Windows 10 or 11. It is for personal use, so builds are not notarised or code-signed.

## Installing on a Mac

1. Download `UniqueVintageProcessor-mac.zip` from the latest build. You can get it from the repository's **Releases** page or from the chat. Double-click the zip to unzip it.
2. Copy the plugin files:
   - `Unique Vintage Processor.component` goes in `~/Library/Audio/Plug-Ins/Components/`.
   - `Unique Vintage Processor.vst3` goes in `~/Library/Audio/Plug-Ins/VST3/`.

   In Finder, use **Go › Go to Folder…** and paste the path. `~` is your home folder.
3. The plugins aren't notarised, so clear the download quarantine once. Paste this into Terminal:

   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Unique Vintage Processor.component" ~/Library/Audio/Plug-Ins/VST3/"Unique Vintage Processor.vst3"
   ```

4. Restart your DAW and rescan plugins. In Logic, AU plugins are validated automatically at launch. Your Pro Tools wrapper loads the VST3 or AU as usual.

When you update to a newer build, replace both files, run the same `xattr` line again and restart the DAW.

## Installing on Windows

1. Download `UniqueVintageProcessor-win64.zip`. Before unzipping, right-click it, choose **Properties**, tick **Unblock** if it's shown, then click **OK**.
2. Unzip it and copy the whole `Unique Vintage Processor.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows will ask for administrator permission; that's normal.
3. Restart your DAW and rescan plugins.

To update, replace the `.vst3` folder and rescan. Presets are stored in `%APPDATA%\Sound Aquarium\Unique Vintage Processor\Presets`.

## Controls

| Control | What it does |
|---|---|
| **AMOUNT** (0–11) | 0 is transparent. Up to 8 is heavy but musical: suppression, harmonics and RIAA emphasis all scale together. Above 8 it becomes an overt special effect. |
| **EVEN / ODD / HECTIC** | Harmonic character: 12AX7 triode stage, transformer core hysteresis, or both followed by a diode clipper. |
| **MC** (MORE COLOUR) | Turns tape on and opens the machine list. Pressing MC again turns tape off and remembers the machine. **DRIVE** trims the level into the tape (±12 dB, compensated). |
| **Q** | BROAD makes the whole band dip together, like a cutting-lathe HF limiter. NARROW makes surgical notches. The value shows while you adjust it. |
| **HPF** | Only content above this frequency is processed (24 dB/oct linear-phase split, 20 Hz–15 kHz, default 4 kHz). Turn the HPF button off to process the full band. |
| **INPUT / OUTPUT** | ±18 dB. OUTPUT is a plain trim. |
| **AG** | Auto gain: a calibrated loudness match with no level follower, so pumping can't happen. It is on by default. |
| **MIX** | Blend between dry and processed signal. |
| **DELTA** | Lets you hear only what the vintage section changes. |
| **BYPASS** | Smooth in/out switch with matched latency. |
| **OS** | Oversampling for the harmonics: 1X, 2X, 4X (default), 8X or 16X. Tape runs at up to 8X. EVEN is clean from 2X up; ODD and HECTIC at high AMOUNT with a bright source are cleanest at 8X or 16X. |
| **MENU / SAVE** | User presets (Mac: `~/Library/Audio/Presets/Sound Aquarium/Unique Vintage Processor`; Windows: see above). MENU also has the interface size setting. |
| **EXPAND** | Shows the analyser: input (yellow), output (line) and suppression (salmon). |

Double-click any knob or slider to reset it. The plugin has a constant latency of about 78 ms at 48 kHz, which your DAW compensates automatically.

## Building

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target UniqueVintageProcessor_AU UniqueVintageProcessor_VST3
```

The build fetches JUCE 9.0.6 automatically. Every push builds the Mac and Windows versions on GitHub Actions (`.github/workflows/build.yml`) and publishes both in one release. Add `-DUVP_BUILD_HARNESS=ON` to build the offline test harness in `Tests/`.

JUCE is licensed under the AGPLv3 or the JUCE commercial licence. The DM Sans font is used under the SIL Open Font License; see `Resources/DMSans-OFL.txt`.
