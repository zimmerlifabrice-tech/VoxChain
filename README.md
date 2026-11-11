# VoxChain – Vocal-Plugin für FL Studio (Phase 1)

All-in-One-Vocal-Plugin: Tonhöhenkorrektur (Hard-Tune-Effekt) plus Vocal-Kette, gebaut für **FL Studio Producer Edition (oder höher)** auf **Windows**.

## Was Phase 1 kann

| Modul | Funktionen |
|---|---|
| **Pitch Correction** | Tonart + Skala (Chromatic, Major, Minor, Harmonic Minor, Pentatonik, Blues), Retune Speed (0 ms = harter Robo-Effekt), Humanize, **Transition** (Gleiter zwischen Tönen laufen natürlich statt als flatternde Treppe; 0 % = klassische harte Stufen), Formant-Shift, Mix. S-, F- und T-Laute sowie Atmer werden nicht korrigiert. Formanten bleiben erhalten (kein Micky-Maus-Effekt). |
| **HPF** | Hochpass 12 oder 24 dB/Okt. gegen Rumpeln |
| **EQ** | Low Shelf (Wärme/Mulm), Mitten-Glocke (Präsenz), High Shelf (Air) |
| **Compressor** | Threshold, Ratio, Attack, Release, Makeup, Mix (Parallel-Kompression), GR-Anzeige |
| **De-Esser** | Split-Band, Frequenz, Threshold, Range, **Listen** (nur die S-Laute hören), GR-Anzeige |

Dazu: Live-Pitch-Kurve (grau = gesungen, violett = korrigiert), Input/Output-Gain mit Pegelanzeige, Quality-Umschalter.

**Quality / Latenz (bei 44.1 kHz):**
- *Live (low latency)*: ca. 23 ms, erkennt Töne ab ca. 90 Hz. Zum Aufnehmen mit Effekt auf dem Kopfhörer.
- *High Quality*: ca. 39 ms, erkennt Töne ab 60 Hz (sehr tiefe Stimmen), stabilere Erkennung. Zum Mixen.

FL Studio gleicht die Latenz bei der Wiedergabe automatisch aus (Plugin Delay Compensation).

## Plugin herunterladen (ohne selbst zu bauen)

1. Im GitHub-Repository oben auf **Actions** klicken.
2. Den neuesten grünen Durchlauf **„Build Windows VST3“** öffnen.
3. Unten bei **Artifacts** auf **VoxChain-Windows-VST3** klicken und die ZIP-Datei herunterladen.
4. ZIP entpacken (eventuell ist darin nochmals ein ZIP, das auch entpacken).

Bei Versionen mit Tag (z. B. `v0.1.0`) liegt die ZIP-Datei zusätzlich unter **Releases**.

## In FL Studio installieren

1. Den Ordner **`VoxChain.vst3`** kopieren nach:
   `C:\Program Files\Common Files\VST3\`
2. FL Studio öffnen → **Options → Manage plugins** → **Find plugins** (oder „Find more plugins“).
3. In der Liste **VoxChain** suchen und den Stern ★ setzen (Favorit).
4. Mixer öffnen → einen freien Insert wählen → bei **Input** dein Mikrofon auswählen → in einem leeren Effekt-Slot **VoxChain** laden.

## Die wichtigsten Einstellungen

- **Key / Scale**: Tonart des Beats (z. B. A Minor). Wenn du sie nicht weisst: *Chromatic* korrigiert auf jeden Halbton.
- **Retune**: wie schnell die Stimme zum *nächsten* Ton gleitet. 0 ms = sofortiger Sprung (harter T-Pain-Sound), 20–40 ms = fliessend, 60+ ms = sehr weich. Gehaltene Töne bleiben bei jedem Wert voll getunt.
- **Engine**: *Classic* = sehr präzise, stabil, wenig Latenz (Live 23 ms). *Smooth* = Phase Vocoder (wie kommerzielle Auto-Tuner), natürlicher bei rauer/kratziger Stimme, aber 60 ms Latenz und eine leichte Schwebung auf verschobenen Tönen. Zum Einsingen *Classic + Live* nehmen.
- **Humanize**: lässt einen Teil der natürlichen Tonhöhenbewegung durch.
- **Transition**: 80 % (Standard) = schnelle Gleiter zwischen Tönen klingen sauber, gehaltene Töne bleiben hart getunt. 0 % = klassischer T-Pain-Treppen-Effekt.
- **Formant**: Stimme dunkler (–) oder heller (+), ohne die Tonhöhe zu ändern.
- **Quality**: Zum Aufnehmen *Live*, zum Mixen *High Quality*.

## Selbst bauen (optional)

Benötigt: Visual Studio 2022 (oder Build Tools) mit „Desktopentwicklung mit C++“, CMake, Git.

```
build.bat
```

Oder manuell:

```
cmake -B build -A x64
cmake --build build --config Release --target VoxChain_VST3 VoxChainTests
```

Ergebnis: `build\VoxChain_artefacts\Release\VST3\VoxChain.vst3`

macOS (später, Phase 7): `cmake -B build -G Xcode && cmake --build build --config Release`

## Tests

- **DSP-Unit-Tests** (`VoxChainTests`): Pitch-Erkennung auf weniger als 1 Cent genau, Skalen, Pitch-Shift von –5 bis +5 Halbtönen mit über 99,8 % Klangreinheit, Lautstärke bleibt gleich, Formant-Shift ändert die Tonhöhe nicht, Hard-Tune rastet auf den Skalenton ein.
- **pluginval** auf Strenge 10 (höchste Stufe): bestanden.
- Beides läuft bei jedem Push automatisch auf GitHub.

## Projektstruktur

```
Source/
  PluginProcessor.*      Audio rein → Modul-Kette → Audio raus, Latenz/State für FL Studio
  PluginEditor.*         Oberfläche
  Parameters/            alle Parameter mit festen IDs (nie umbenennen → FL-Automation)
  Engine/                ModuleBase, ModuleChain, ModuleFactory (Basis für den Chain Builder)
  Modules/Pitch/         PitchDetector (YIN/pYIN), ScaleQuantizer, PsolaShifter, PitchModule
  Modules/               HighPass, EQ, Compressor, DeEsser
  DSP/                   Biquad-Filter
  UI/                    LookAndFeel, PitchDisplay, ModuleStrip
Tests/                   DSP-Unit-Tests
```

## FL-Studio-Testliste (nach jeder Phase)

- [ ] Auf Mixer-Insert laden, Mikrofon als Input, einsingen
- [ ] Parameter automatisieren (Automation Clip, Link to controller)
- [ ] Projekt speichern und neu laden: alle Einstellungen bleiben erhalten
- [ ] Exportieren/Rendern (WAV)
- [ ] Smart Disable an: Ende wird nicht abgeschnitten
- [ ] Im Patcher laden
- [ ] Mehrere Instanzen gleichzeitig
- [ ] Buffergrösse in den Audio-Einstellungen ändern
- [ ] Quality zwischen Live und High Quality umschalten

## Roadmap

1. ✅ **Phase 1**: Pitch-Korrektur, HPF, EQ, Kompressor, De-Esser, modulare Engine
2. Phase 2: Aufnahme im Plugin (Takes, Export, Drag & Drop in die Playlist)
3. Phase 3: Cleanup (Rauschen, Brummen, Plosive, Mouth Clicks, Atmer)
4. Phase 4: restliche Mixing-Kette + **Custom Vocal Chain Builder** + Presets
5. Phase 5: Pro-Oberfläche, Analyzer, Preset-Browser
6. Phase 6: grafischer Pitch-Editor, Auto-Mix, Referenz-Matching
7. Phase 7: macOS, AU, CLAP, Installer, Code-Signing

Lizenzen: siehe [LICENSES.md](LICENSES.md).
