#pragma once

#include "PitchDetector.h"
#include <cmath>

namespace vox
{
/**
    Smooths the voiced/unvoiced decision over time.

    A rough, raspy or breathy voice makes single analysis frames look
    "unvoiced" for a few milliseconds. Switching the pitch shifter on and off
    that fast creates crackle that makes the rasp sound worse. This tracker
    keeps a note "voiced" through short dropouts (up to ~100 ms) as long as
    the level is there and it is not a sibilant (S/F/T), holding the last
    pitch or following a plausible pitch candidate.
*/
class VoicingTracker
{
public:
    struct Output { bool voiced = false; float f0 = 0.0f; };

    void prepare (double hopSeconds, double maxHoldSeconds = 0.10)
    {
        maxHoldHops = (int) std::lround (maxHoldSeconds / hopSeconds);
        if (maxHoldHops < 1)
            maxHoldHops = 1;
        reset();
    }

    void reset() noexcept
    {
        voiced = false;
        holdHops = 0;
        lastF0 = 0.0f;
    }

    Output update (const PitchDetector::Result& r, float levelGate, float maxZeroCrossRate) noexcept
    {
        if (r.voiced && r.f0 > 0.0f)
        {
            voiced = true;
            holdHops = 0;
            lastF0 = r.f0;
            return { true, r.f0 };
        }

        const bool canHold = voiced && lastF0 > 0.0f && holdHops < maxHoldHops
                          && r.rms >= levelGate && r.zeroCrossRate <= maxZeroCrossRate;
        if (canHold)
        {
            ++holdHops;
            // Follow the candidate if it is close to the held note (within 1 semitone).
            if (r.candidateF0 > 0.0f && std::abs (12.0f * std::log2 (r.candidateF0 / lastF0)) < 1.0f)
                lastF0 = r.candidateF0;
            return { true, lastF0 };
        }

        voiced = false;
        holdHops = 0;
        return { false, 0.0f };
    }

private:
    bool voiced = false;
    int holdHops = 0;
    int maxHoldHops = 15;
    float lastF0 = 0.0f;
};

} // namespace vox
