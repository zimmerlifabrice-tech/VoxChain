#include "ParameterLayout.h"

namespace vox
{
namespace
{
    // Version hint 1 for all Phase-1 parameters (required for stable VST3 IDs).
    juce::ParameterID pid (const char* id) { return { id, 1 }; }

    juce::NormalisableRange<float> freqRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }

    juce::String hzToText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }

    using Float  = juce::AudioParameterFloat;
    using Bool   = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    using Attr   = juce::AudioParameterFloatAttributes;

    std::unique_ptr<Float> makeFloat (const char* id, const juce::String& name,
                                      juce::NormalisableRange<float> range, float def,
                                      const juce::String& unit, int decimals = 1)
    {
        return std::make_unique<Float> (pid (id), name, range, def,
            Attr().withLabel (unit)
                  .withStringFromValueFunction ([decimals, unit] (float v, int)
                  {
                      return juce::String (v, decimals) + (unit.isEmpty() ? "" : (unit.startsWith (":") ? unit : " " + unit));
                  }));
    }

    std::unique_ptr<Float> makeFreq (const char* id, const juce::String& name, float lo, float hi, float def)
    {
        return std::make_unique<Float> (pid (id), name, freqRange (lo, hi), def,
                                        Attr().withLabel ("Hz").withStringFromValueFunction (hzToText));
    }
}

juce::StringArray getKeyNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray getScaleNames()
{
    return { "Chromatic", "Major", "Minor", "Harmonic Minor", "Major Pentatonic", "Minor Pentatonic", "Blues" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Global ------------------------------------------------------------
    layout.add (makeFloat (ids::inputGain,  "Input Gain",  { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));
    layout.add (makeFloat (ids::outputGain, "Output Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));
    layout.add (std::make_unique<Choice> (pid (ids::quality), "Quality",
                                          juce::StringArray { "Live (low latency)", "High Quality" }, 0));

    // ---- Pitch -------------------------------------------------------------
    layout.add (std::make_unique<Bool>   (pid (ids::pitchOn),    "Pitch On", true));
    layout.add (std::make_unique<Choice> (pid (ids::pitchKey),   "Key",   getKeyNames(), 0));
    layout.add (std::make_unique<Choice> (pid (ids::pitchScale), "Scale", getScaleNames(), 2));

    {
        juce::NormalisableRange<float> r (0.0f, 400.0f, 0.1f);
        r.setSkewForCentre (50.0f);
        layout.add (makeFloat (ids::pitchRetune, "Retune Speed", r, 0.0f, "ms", 0));
    }
    layout.add (makeFloat (ids::pitchHumanize, "Humanize",     { 0.0f, 100.0f, 0.1f }, 0.0f,   "%", 0));
    layout.add (makeFloat (ids::pitchFormant,  "Formant",      { -6.0f, 6.0f, 0.01f }, 0.0f,  "st", 2));
    layout.add (makeFloat (ids::pitchMix,      "Pitch Mix",    { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
    layout.add (makeFloat (ids::pitchTransition, "Transition", { 0.0f, 100.0f, 0.1f }, 80.0f,  "%", 0));

    // ---- High-pass ---------------------------------------------------------
    layout.add (std::make_unique<Bool>   (pid (ids::hpfOn), "HPF On", true));
    layout.add (makeFreq (ids::hpfFreq, "HPF Freq", 20.0f, 400.0f, 90.0f));
    layout.add (std::make_unique<Choice> (pid (ids::hpfSlope), "HPF Slope",
                                          juce::StringArray { "12 dB/oct", "24 dB/oct" }, 0));

    // ---- EQ ----------------------------------------------------------------
    layout.add (std::make_unique<Bool> (pid (ids::eqOn), "EQ On", true));
    layout.add (makeFreq  (ids::eqLowFreq,  "EQ Low Freq",  40.0f,   500.0f,  200.0f));
    layout.add (makeFloat (ids::eqLowGain,  "EQ Low Gain",  { -12.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    layout.add (makeFreq  (ids::eqMidFreq,  "EQ Mid Freq",  200.0f,  8000.0f, 3000.0f));
    layout.add (makeFloat (ids::eqMidGain,  "EQ Mid Gain",  { -12.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    {
        juce::NormalisableRange<float> r (0.3f, 6.0f, 0.01f);
        r.setSkewForCentre (1.0f);
        layout.add (makeFloat (ids::eqMidQ, "EQ Mid Q", r, 1.0f, "", 2));
    }
    layout.add (makeFreq  (ids::eqHighFreq, "EQ High Freq", 2000.0f, 16000.0f, 10000.0f));
    layout.add (makeFloat (ids::eqHighGain, "EQ High Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB"));

    // ---- Compressor --------------------------------------------------------
    layout.add (std::make_unique<Bool> (pid (ids::compOn), "Comp On", true));
    layout.add (makeFloat (ids::compThreshold, "Comp Threshold", { -60.0f, 0.0f, 0.1f }, -18.0f, "dB"));
    {
        juce::NormalisableRange<float> r (1.0f, 20.0f, 0.01f);
        r.setSkewForCentre (4.0f);
        layout.add (makeFloat (ids::compRatio, "Comp Ratio", r, 4.0f, ":1", 1));
    }
    {
        juce::NormalisableRange<float> r (0.1f, 100.0f, 0.01f);
        r.setSkewForCentre (10.0f);
        layout.add (makeFloat (ids::compAttack, "Comp Attack", r, 5.0f, "ms", 1));
    }
    {
        juce::NormalisableRange<float> r (10.0f, 1000.0f, 0.1f);
        r.setSkewForCentre (120.0f);
        layout.add (makeFloat (ids::compRelease, "Comp Release", r, 100.0f, "ms", 0));
    }
    layout.add (makeFloat (ids::compMakeup, "Comp Makeup", { 0.0f, 24.0f, 0.1f }, 3.0f, "dB"));
    layout.add (makeFloat (ids::compMix,    "Comp Mix",    { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));

    // ---- De-esser ----------------------------------------------------------
    layout.add (std::make_unique<Bool> (pid (ids::deessOn), "De-Ess On", true));
    layout.add (makeFreq  (ids::deessFreq,      "De-Ess Freq",      2000.0f, 12000.0f, 6000.0f));
    layout.add (makeFloat (ids::deessThreshold, "De-Ess Threshold", { -60.0f, 0.0f, 0.1f }, -30.0f, "dB"));
    layout.add (makeFloat (ids::deessRange,     "De-Ess Range",     { 0.0f, 24.0f, 0.1f }, 8.0f, "dB"));
    layout.add (std::make_unique<Bool> (pid (ids::deessListen), "De-Ess Listen", false));

    return layout;
}

} // namespace vox
