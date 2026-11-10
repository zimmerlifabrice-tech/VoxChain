#pragma once

#include <cmath>

namespace vox
{
/**
    Detects fast slides/glides between notes (the "Transition" / flex-tune idea).

    With hard tuning, a slide from E down to C becomes a rapid staircase
    E -> D# -> D -> C, which sounds like fluttering. During a detected slide the
    correction is faded out so the slide passes naturally; as soon as the voice
    settles on a note, hard tuning takes over again.

    Vibrato is NOT treated as a slide: it swings around a centre (stays within
    ~0.6 semitones of it), while a slide moves away from the centre quickly.
*/
class TransitionDetector
{
public:
    void prepare (double hopSeconds)
    {
        hop = (float) hopSeconds;
        centreCoef = coef (0.150f);  // slow pitch centre (ignores vibrato)
        fastCoef   = coef (0.008f);  // lightly smoothed pitch
        velCoef    = coef (0.012f);  // velocity smoothing
        attackCoef = coef (0.008f);  // how fast "slide mode" engages
        releaseCoef = coef (0.035f); // how fast hard tuning returns
        reset();
    }

    void reset() noexcept
    {
        valid = false;
        weight = 0.0f;
        velocity = 0.0f;
    }

    /** Call once per hop with the detected pitch (MIDI). Returns slide weight 0..1. */
    float update (float midi) noexcept
    {
        if (! valid)
        {
            centre = fast = midi;
            velocity = 0.0f;
            valid = true;
            return weight = 0.0f;
        }

        const float prevFast = fast;
        fast   += fastCoef * (midi - fast);
        centre += centreCoef * (midi - centre);
        velocity += velCoef * ((fast - prevFast) / hop - velocity);

        const float distance = std::abs (fast - centre);
        const bool sliding = distance > 0.6f && std::abs (velocity) > 5.0f; // semitones, semitones/s
        const float target = sliding ? 1.0f : 0.0f;
        weight += (target > weight ? attackCoef : releaseCoef) * (target - weight);
        return weight;
    }

    /** Call when the voice becomes unvoiced. */
    void unvoiced() noexcept { valid = false; weight *= 0.5f; }

private:
    float coef (float seconds) const { return 1.0f - std::exp (-hop / seconds); }

    float hop = 0.003f;
    float centreCoef = 0.02f, fastCoef = 0.3f, velCoef = 0.2f, attackCoef = 0.3f, releaseCoef = 0.1f;
    bool valid = false;
    float centre = 0.0f, fast = 0.0f, velocity = 0.0f, weight = 0.0f;
};

} // namespace vox
