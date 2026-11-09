#include "ModuleStrip.h"
#include "LookAndFeel.h"

namespace vox
{
// ============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
            const juce::String& caption, const juce::String& tooltip)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    slider.setTooltip (tooltip);
    slider.setDoubleClickReturnValue (true, (double) state.getParameter (paramId)->convertFrom0to1 (
                                                state.getParameter (paramId)->getDefaultValue()));
    addAndMakeVisible (slider);

    label.setText (caption.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
}

void Knob::resized()
{
    constexpr int labelH = 16, textH = 18, maxKnob = 84;
    const auto r = getLocalBounds();
    const int knobSize = juce::jmax (16, juce::jmin (r.getWidth() - 4, r.getHeight() - labelH - textH, maxKnob));

    auto area = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), labelH + knobSize + textH));
    label.setBounds (area.removeFromTop (labelH));
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, r.getWidth(), textH);
    slider.setBounds (area);
}

// ============================================================================
Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);

    label.setText (caption.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    box.setBounds (r.removeFromTop (juce::jmin (28, r.getHeight())));
}

// ============================================================================
void GainReductionMeter::update()
{
    if (source == nullptr)
        return;
    const float gr = source->getGainReductionDb();
    // Fast attack, slower fall for readability.
    shown = gr > shown ? gr : shown * 0.85f + gr * 0.15f;
    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 3.0f);

    const float maxDb = 24.0f;
    const float frac = juce::jlimit (0.0f, 1.0f, shown / maxDb);
    auto bar = r.reduced (2.0f);
    bar = bar.removeFromTop (bar.getHeight() * frac);
    g.setColour (colours::accent2);
    g.fillRoundedRectangle (bar, 2.0f);

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("GR", getLocalBounds().removeFromBottom (12), juce::Justification::centred);
}

// ============================================================================
ModuleStrip::ModuleStrip (juce::AudioProcessorValueTreeState& s, const juce::String& t,
                          const juce::String& enabledParamId, const std::vector<KnobSpec>& specs,
                          int knobColumns, const ModuleBase* meterSource)
    : state (s), title (t), columns (juce::jmax (1, knobColumns))
{
    power.setTooltip ("Module on/off");
    addAndMakeVisible (power);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, enabledParamId, power);
    power.onStateChange = [this] { updateEnabledLook(); };

    for (auto& k : specs)
        addAndMakeVisible (knobs.add (new Knob (state, k.paramId, k.caption, k.tooltip)));

    if (meterSource != nullptr)
    {
        meter = std::make_unique<GainReductionMeter> (meterSource);
        addAndMakeVisible (*meter);
        startTimerHz (30);
    }

    updateEnabledLook();
}

void ModuleStrip::addHeaderChoice (const juce::String& paramId)
{
    auto* box = headerCombos.add (new juce::ComboBox());
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        box->addItemList (p->choices, 1);
    addAndMakeVisible (box);
    comboAttachments.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (state, paramId, *box));
    resized();
}

void ModuleStrip::addHeaderToggle (const juce::String& paramId, const juce::String& text, const juce::String& tooltip)
{
    auto* b = headerToggles.add (new juce::ToggleButton (text));
    b->setTooltip (tooltip);
    addAndMakeVisible (b);
    toggleAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (state, paramId, *b));
    resized();
}

void ModuleStrip::updateEnabledLook()
{
    const bool on = power.getToggleState();
    for (auto* k : knobs)
        k->setAlpha (on ? 1.0f : 0.45f);
    repaint();
}

void ModuleStrip::timerCallback()
{
    if (meter != nullptr)
        meter->update();
}

void ModuleStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (power.getToggleState() ? colours::text : colours::textDim);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title.toUpperCase(), getLocalBounds().reduced (12, 0).removeFromTop (34),
                juce::Justification::centredLeft);
}

void ModuleStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto header = r.removeFromTop (28);
    power.setBounds (header.removeFromRight (40).withSizeKeepingCentre (40, 20));

    for (auto* b : headerToggles)
    {
        header.removeFromRight (6);
        b->setBounds (header.removeFromRight (56).withSizeKeepingCentre (56, 20));
    }
    for (auto* c : headerCombos)
    {
        header.removeFromRight (6);
        c->setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 22));
    }

    r.removeFromTop (4);
    if (meter != nullptr)
    {
        meter->setBounds (r.removeFromRight (14).reduced (0, 6));
        r.removeFromRight (6);
    }

    const int n = knobs.size();
    if (n == 0)
        return;

    const int rows = (n + columns - 1) / columns;
    const int cellW = r.getWidth() / columns;
    const int cellH = r.getHeight() / rows;
    for (int i = 0; i < n; ++i)
    {
        const int row = i / columns, col = i % columns;
        knobs[i]->setBounds (r.getX() + col * cellW, r.getY() + row * cellH, cellW, cellH);
    }
}

} // namespace vox
