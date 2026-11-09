#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    All parameter IDs live here.

    IMPORTANT: these IDs must NEVER be renamed or re-used. FL Studio stores
    automation clips and "Link to controller" assignments by parameter ID, so
    changing one would break existing projects. New parameters get new IDs.
*/
namespace vox::ids
{
    // Global
    inline constexpr const char* inputGain   = "global_input_gain";
    inline constexpr const char* outputGain  = "global_output_gain";
    inline constexpr const char* quality     = "global_quality";

    // Pitch correction
    inline constexpr const char* pitchOn       = "pitch_enabled";
    inline constexpr const char* pitchKey      = "pitch_key";
    inline constexpr const char* pitchScale    = "pitch_scale";
    inline constexpr const char* pitchRetune   = "pitch_retune_ms";
    inline constexpr const char* pitchHumanize = "pitch_humanize";
    inline constexpr const char* pitchFormant  = "pitch_formant";
    inline constexpr const char* pitchMix      = "pitch_mix";

    // High-pass
    inline constexpr const char* hpfOn    = "hpf_enabled";
    inline constexpr const char* hpfFreq  = "hpf_freq";
    inline constexpr const char* hpfSlope = "hpf_slope";

    // EQ
    inline constexpr const char* eqOn       = "eq_enabled";
    inline constexpr const char* eqLowFreq  = "eq_low_freq";
    inline constexpr const char* eqLowGain  = "eq_low_gain";
    inline constexpr const char* eqMidFreq  = "eq_mid_freq";
    inline constexpr const char* eqMidGain  = "eq_mid_gain";
    inline constexpr const char* eqMidQ     = "eq_mid_q";
    inline constexpr const char* eqHighFreq = "eq_high_freq";
    inline constexpr const char* eqHighGain = "eq_high_gain";

    // Compressor
    inline constexpr const char* compOn        = "comp_enabled";
    inline constexpr const char* compThreshold = "comp_threshold";
    inline constexpr const char* compRatio     = "comp_ratio";
    inline constexpr const char* compAttack    = "comp_attack";
    inline constexpr const char* compRelease   = "comp_release";
    inline constexpr const char* compMakeup    = "comp_makeup";
    inline constexpr const char* compMix       = "comp_mix";

    // De-esser
    inline constexpr const char* deessOn        = "deess_enabled";
    inline constexpr const char* deessFreq      = "deess_freq";
    inline constexpr const char* deessThreshold = "deess_threshold";
    inline constexpr const char* deessRange     = "deess_range";
    inline constexpr const char* deessListen    = "deess_listen";
}

namespace vox
{
    juce::StringArray getKeyNames();
    juce::StringArray getScaleNames();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
