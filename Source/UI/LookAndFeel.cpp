#include "LookAndFeel.h"

namespace vox
{
VoxLookAndFeel::VoxLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, colours::textDim);
    setColour (juce::ComboBox::backgroundColourId, colours::panelLight);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::ComboBox::arrowColourId, colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelLight);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::outline);
}

void VoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.14f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    const bool enabled = slider.isEnabled();
    const auto accent = (slider.getProperties().contains ("accent2") ? colours::accent2 : colours::accent)
                            .withMultipliedAlpha (enabled ? 1.0f : 0.35f);

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::outline);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc (bipolar parameters draw from the centre)
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float from = startAngle + zeroPos * (endAngle - startAngle);
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body
    const float bodyR = arcR - lineW * 1.3f;
    g.setGradientFill (juce::ColourGradient (colours::panelLight.brighter (0.15f), centre.x, centre.y - bodyR,
                                             colours::panel, centre.x, centre.y + bodyR, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    // Pointer
    juce::Path p;
    const float pl = bodyR * 0.75f;
    p.addRoundedRectangle (-lineW * 0.35f, -bodyR + 2.0f, lineW * 0.7f, pl * 0.55f, lineW * 0.3f);
    g.setColour (colours::text.withMultipliedAlpha (enabled ? 1.0f : 0.4f));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (centre));
}

void VoxLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const float corner = r.getHeight() * 0.5f;

    g.setColour (on ? colours::accent.withAlpha (0.9f) : colours::panelLight);
    g.fillRoundedRectangle (r, corner);
    g.setColour (highlighted ? colours::text.withAlpha (0.5f) : colours::outline);
    g.drawRoundedRectangle (r, corner, 1.0f);

    g.setColour (on ? juce::Colours::white : colours::textDim);
    g.setFont (juce::FontOptions (juce::jmin (13.0f, r.getHeight() * 0.6f), juce::Font::bold));
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1);
}

void VoxLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font VoxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::FontOptions (juce::jmin (14.0f, (float) box.getHeight() * 0.55f));
}

juce::Font VoxLookAndFeel::getLabelFont (juce::Label& l)
{
    return juce::FontOptions (juce::jmin (13.0f, juce::jmax (10.0f, (float) l.getHeight() * 0.8f)));
}

void VoxLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited())
        return;
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (getLabelFont (label));
    g.drawFittedText (label.getText(), label.getLocalBounds(), label.getJustificationType(), 1);
}

} // namespace vox
