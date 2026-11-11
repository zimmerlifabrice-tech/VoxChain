#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vox
{
namespace colours
{
    inline const juce::Colour background   { 0xff0f1015 };
    inline const juce::Colour panel        { 0xff181a22 };
    inline const juce::Colour panelLight   { 0xff22252f };
    inline const juce::Colour outline      { 0xff2c3040 };
    inline const juce::Colour text         { 0xffe8e9f0 };
    inline const juce::Colour textDim      { 0xff8a8fa3 };
    inline const juce::Colour accent       { 0xff9b6bff }; // violet
    inline const juce::Colour accent2      { 0xff34d3eb }; // cyan
    inline const juce::Colour warn         { 0xffff5c7a };
}

class VoxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VoxLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                       juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

} // namespace vox
