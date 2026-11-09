#include "PitchDisplay.h"
#include "LookAndFeel.h"
#include "Parameters/ParameterLayout.h"

namespace vox
{
PitchDisplay::PitchDisplay (const PitchModule& m, juce::AudioProcessorValueTreeState& state)
    : module (m)
{
    pKey   = state.getRawParameterValue (ids::pitchKey);
    pScale = state.getRawParameterValue (ids::pitchScale);
    detected.fill (-1.0f);
    corrected.fill (-1.0f);
    startTimerHz (30);
}

PitchDisplay::~PitchDisplay()
{
    stopTimer();
}

void PitchDisplay::timerCallback()
{
    const float d = module.getDetectedMidi();
    const float c = module.getCorrectedMidi();
    detected[(size_t) writeIndex] = d;
    corrected[(size_t) writeIndex] = c;
    writeIndex = (writeIndex + 1) % historySize;
    lastTarget = module.getTargetMidi();

    // Slowly follow the singer so the curve stays in view.
    if (d > 0.0f)
        viewCentre += (d - viewCentre) * 0.04f;

    repaint();
}

float PitchDisplay::midiToY (float midi, juce::Rectangle<float> area) const
{
    const float top = viewCentre + viewRange;
    return area.getY() + (top - midi) / (2.0f * viewRange) * area.getHeight();
}

void PitchDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (bounds, 8.0f);

    auto area = bounds.reduced (8.0f);
    const auto noteArea = area.removeFromLeft (34.0f);

    scaleInfo.setScale ((int) pKey->load(), (int) pScale->load());
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    // Note grid
    const int lo = (int) std::floor (viewCentre - viewRange);
    const int hi = (int) std::ceil (viewCentre + viewRange);
    for (int n = lo; n <= hi; ++n)
    {
        const float y = midiToY ((float) n, area);
        if (y < area.getY() || y > area.getBottom())
            continue;

        const bool inScale = scaleInfo.isNoteInScale (n);
        const bool isTarget = juce::roundToInt (lastTarget) == n && lastTarget > 0.0f;
        g.setColour (isTarget ? colours::accent.withAlpha (0.35f)
                              : (inScale ? colours::outline : colours::outline.withAlpha (0.35f)));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());

        if (inScale)
        {
            g.setColour (isTarget ? colours::accent : colours::textDim);
            g.setFont (juce::FontOptions (10.0f));
            const int octave = n / 12 - 1;
            g.drawText (juce::String (names[((n % 12) + 12) % 12]) + juce::String (octave),
                        juce::Rectangle<float> (noteArea.getX(), y - 7.0f, noteArea.getWidth(), 14.0f),
                        juce::Justification::centredLeft);
        }
    }

    // Curves
    auto drawCurve = [&] (const std::array<float, historySize>& data, juce::Colour colour, float width)
    {
        juce::Path p;
        bool started = false;
        for (int i = 0; i < historySize; ++i)
        {
            const float v = data[(size_t) ((writeIndex + i) % historySize)];
            const float x = area.getX() + area.getWidth() * (float) i / (float) (historySize - 1);
            if (v <= 0.0f)
            {
                started = false;
                continue;
            }
            const float y = juce::jlimit (area.getY(), area.getBottom(), midiToY (v, area));
            if (! started)
            {
                p.startNewSubPath (x, y);
                started = true;
            }
            else
                p.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    drawCurve (detected, colours::textDim.withAlpha (0.7f), 1.5f);
    drawCurve (corrected, colours::accent, 2.5f);

    // Current note readout
    if (lastTarget > 0.0f)
    {
        const int n = juce::roundToInt (lastTarget);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        g.drawText (juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1),
                    bounds.reduced (14.0f).removeFromTop (34.0f), juce::Justification::topRight);
    }

    // Legend
    g.setFont (juce::FontOptions (11.0f));
    auto legend = bounds.reduced (12.0f).removeFromBottom (14.0f);
    g.setColour (colours::textDim);
    g.drawText ("sung", legend.removeFromRight (40.0f), juce::Justification::centredRight);
    g.setColour (colours::accent);
    g.drawText ("tuned", legend.removeFromRight (50.0f), juce::Justification::centredRight);
}

} // namespace vox
