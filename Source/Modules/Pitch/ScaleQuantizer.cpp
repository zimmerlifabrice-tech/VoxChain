#include "ScaleQuantizer.h"
#include <cmath>

namespace vox
{
namespace
{
    constexpr int numIntervals[ScaleQuantizer::NumScales] = { 12, 7, 7, 7, 5, 5, 6 };
    constexpr int intervals[ScaleQuantizer::NumScales][12] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, // Chromatic
        { 0, 2, 4, 5, 7, 9, 11 },                 // Major
        { 0, 2, 3, 5, 7, 8, 10 },                 // Natural minor
        { 0, 2, 3, 5, 7, 8, 11 },                 // Harmonic minor
        { 0, 2, 4, 7, 9 },                        // Major pentatonic
        { 0, 3, 5, 7, 10 },                       // Minor pentatonic
        { 0, 3, 5, 6, 7, 10 },                    // Blues
    };

    int pitchClass (int note) { return ((note % 12) + 12) % 12; }
}

void ScaleQuantizer::setScale (int keyRoot, int scaleIndex)
{
    if (scaleIndex < 0 || scaleIndex >= NumScales)
        scaleIndex = Chromatic;

    if (keyRoot == key && scaleIndex == scale)
        return;

    key = pitchClass (keyRoot);
    scale = scaleIndex;
    allowed.fill (false);
    for (int i = 0; i < numIntervals[scale]; ++i)
        allowed[(size_t) pitchClass (key + intervals[scale][i])] = true;

    resetHysteresis();
}

bool ScaleQuantizer::isNoteInScale (int note) const noexcept
{
    return allowed[(size_t) pitchClass (note)];
}

float ScaleQuantizer::quantize (float midi)
{
    const int base = (int) std::floor (midi);
    int best = base;
    float bestDist = 1.0e9f;

    for (int n = base - 7; n <= base + 8; ++n)
    {
        if (! isNoteInScale (n))
            continue;
        const float d = std::abs (midi - (float) n);
        if (d < bestDist)
        {
            bestDist = d;
            best = n;
        }
    }

    // Hysteresis: stay on the previous note unless the new one is clearly closer.
    if (lastNote > -1000 && isNoteInScale (lastNote) && best != lastNote)
    {
        const float lastDist = std::abs (midi - (float) lastNote);
        if (lastDist < bestDist + hysteresis)
            best = lastNote;
    }

    lastNote = best;
    return (float) best;
}

} // namespace vox
