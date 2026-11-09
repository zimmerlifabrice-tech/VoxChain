#pragma once

#include <array>

namespace vox
{
/**
    Maps a detected pitch (as fractional MIDI note) to the nearest note of the
    selected key + scale. Uses hysteresis so the target does not flicker
    between two notes when the singer is right in between.
*/
class ScaleQuantizer
{
public:
    enum Scale { Chromatic = 0, Major, Minor, HarmonicMinor, MajorPentatonic, MinorPentatonic, Blues, NumScales };

    void setScale (int keyRootPitchClass, int scaleIndex);

    /** Returns the target MIDI note (integer value). */
    float quantize (float midiNote);

    bool isNoteInScale (int midiNote) const noexcept;
    void resetHysteresis() noexcept { lastNote = -1000; }

    float hysteresis = 0.15f; // semitones

private:
    std::array<bool, 12> allowed {};
    int lastNote = -1000;
    int key = 0, scale = -1;
};

} // namespace vox
