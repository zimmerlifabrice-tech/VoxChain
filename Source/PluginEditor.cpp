#include "PluginEditor.h"
#include "Parameters/ParameterLayout.h"

using namespace vox;

static constexpr int headerH = 84;

// ============================================================================
void LevelMeter::setLevel (float db)
{
    shownDb = db > shownDb ? db : shownDb - 1.5f; // peak hold with fall-off
    shownDb = juce::jlimit (-60.0f, 6.0f, shownDb);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 3.0f);

    const float frac = juce::jlimit (0.0f, 1.0f, (shownDb + 60.0f) / 66.0f);
    auto bar = r.reduced (1.5f);
    bar = bar.removeFromLeft (bar.getWidth() * frac);
    g.setColour (shownDb > -0.5f ? colours::warn : colours::accent2);
    g.fillRoundedRectangle (bar, 2.0f);
}

// ============================================================================
VoxChainEditor::VoxChainEditor (VoxChainProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      inputKnob    (p.getState(), ids::inputGain,  "Input",  "Input gain before the chain"),
      outputKnob   (p.getState(), ids::outputGain, "Output", "Output gain after the chain"),
      keyChoice    (p.getState(), ids::pitchKey,   "Key"),
      scaleChoice  (p.getState(), ids::pitchScale, "Scale"),
      engineChoice (p.getState(), ids::pitchEngine, "Engine"),
      retuneKnob   (p.getState(), ids::pitchRetune,   "Retune",   "0 ms = hard, robotic tuning. Higher = smoother, more natural."),
      humanizeKnob (p.getState(), ids::pitchHumanize, "Humanize", "Lets part of the natural pitch movement through."),
      transitionKnob (p.getState(), ids::pitchTransition, "Transition", "Length of the glide between two notes (0 % = instant jump, 100 % = 120 ms smooth glide). Held notes always stay flat."),
      formantKnob  (p.getState(), ids::pitchFormant,  "Formant",  "Shifts the voice character darker (-) or brighter (+) without changing pitch."),
      pitchMixKnob (p.getState(), ids::pitchMix,      "Mix",      "Blend between the original and the tuned voice."),
      pitchDisplay (*p.getPitchModule(), p.getState()),
      hpfStrip (p.getState(), "HPF", ids::hpfOn,
                { { ids::hpfFreq, "Freq", "Removes rumble below this frequency" } }, 1),
      eqStrip (p.getState(), "EQ", ids::eqOn,
               { { ids::eqLowFreq,  "Low Hz",  "Low shelf frequency" },
                 { ids::eqMidFreq,  "Mid Hz",  "Mid bell frequency" },
                 { ids::eqHighFreq, "High Hz", "High shelf frequency (air)" },
                 { ids::eqLowGain,  "Low",     "Low shelf gain (warmth / mud)" },
                 { ids::eqMidGain,  "Mid",     "Mid bell gain (presence)" },
                 { ids::eqHighGain, "High",    "High shelf gain (air)" },
                 { ids::eqMidQ,     "Mid Q",   "Width of the mid bell" } }, 4),
      compStrip (p.getState(), "Compressor", ids::compOn,
                 { { ids::compThreshold, "Thresh",  "Level where compression starts" },
                   { ids::compRatio,     "Ratio",   "How strongly levels above the threshold are reduced" },
                   { ids::compAttack,    "Attack",  "How fast the compressor reacts" },
                   { ids::compRelease,   "Release", "How fast it lets go" },
                   { ids::compMakeup,    "Makeup",  "Gain after compression" },
                   { ids::compMix,       "Mix",     "Parallel (New York) compression blend" } }, 3,
                 p.findModule ("comp")),
      deessStrip (p.getState(), "De-Esser", ids::deessOn,
                  { { ids::deessFreq,      "Freq",   "Sibilance split frequency (S sounds: 5-9 kHz)" },
                    { ids::deessThreshold, "Thresh", "Level where de-essing starts" },
                    { ids::deessRange,     "Range",  "Maximum reduction of the S sounds" } }, 3,
                  p.findModule ("deess")),
      recordPanel (p)
{
    setLookAndFeel (&lnf);

    // Header
    if (auto* q = dynamic_cast<juce::AudioParameterChoice*> (p.getState().getParameter (ids::quality)))
        qualityBox.addItemList (q->choices, 1);
    qualityBox.setTooltip ("Live = low latency for recording. High Quality = best tracking for mixing.");
    addAndMakeVisible (qualityBox);
    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.getState(), ids::quality, qualityBox);

    latencyLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (latencyLabel);
    addAndMakeVisible (inputKnob);
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    // Pitch
    addAndMakeVisible (pitchPower);
    pitchPowerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.getState(), ids::pitchOn, pitchPower);
    for (auto* c : { (juce::Component*) &keyChoice, (juce::Component*) &scaleChoice, (juce::Component*) &engineChoice,
                     (juce::Component*) &retuneKnob, (juce::Component*) &humanizeKnob,
                     (juce::Component*) &transitionKnob, (juce::Component*) &formantKnob, (juce::Component*) &pitchMixKnob,
                     (juce::Component*) &pitchDisplay })
        addAndMakeVisible (c);

    // Chain
    hpfStrip.addHeaderChoice (ids::hpfSlope);
    deessStrip.addHeaderToggle (ids::deessListen, "LISTEN", "Hear only the sibilance band to find the S sounds");
    for (auto* s : { &hpfStrip, &eqStrip, &compStrip, &deessStrip })
        addAndMakeVisible (s);

    for (auto* t : { &chainTab, &recordTab })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (4711);
        t->setColour (juce::TextButton::buttonColourId, colours::panel);
        t->setColour (juce::TextButton::buttonOnColourId, colours::accent);
        t->setColour (juce::TextButton::textColourOffId, colours::textDim);
        t->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (t);
    }
    chainTab.onClick = [this] { showTab (false); };
    recordTab.onClick = [this] { showTab (true); };
    addChildComponent (recordPanel);
    showTab (p.isArmed());

    setResizable (true, true);
    setResizeLimits (980, 680, 1960, 1360);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (1100.0 / 760.0);
    setSize (1100, 760);

    startTimerHz (30);
}

VoxChainEditor::~VoxChainEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VoxChainEditor::timerCallback()
{
    inMeter.setLevel (processor.getInputLevelDb());
    outMeter.setLevel (processor.getOutputLevelDb());
    latencyLabel.setText ("Latency " + juce::String (processor.getLatencyMs(), 1) + " ms",
                          juce::dontSendNotification);
}

void VoxChainEditor::showTab (bool record)
{
    chainTab.setToggleState (! record, juce::dontSendNotification);
    recordTab.setToggleState (record, juce::dontSendNotification);
    recordPanel.setVisible (record);
    for (auto* s : { &hpfStrip, &eqStrip, &compStrip, &deessStrip })
        s->setVisible (! record);
}

void VoxChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Header
    auto header = getLocalBounds().removeFromTop (headerH).reduced (18, 0);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("VOXCHAIN", header.removeFromLeft (150), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Vocal Suite  |  Phase 2", header.removeFromLeft (180), juce::Justification::centredLeft);

    // Pitch panel
    auto pr = pitchPanel.toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (pr, 10.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (pr.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("PITCH CORRECTION", pitchPanel.reduced (14, 0).removeFromTop (38), juce::Justification::centredLeft);

    // Meter captions
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("IN",  inMeter.getBounds().translated (-22, 0).withWidth (20),  juce::Justification::centredRight);
    g.drawText ("OUT", outMeter.getBounds().translated (-26, 0).withWidth (24), juce::Justification::centredRight);
}

void VoxChainEditor::resized()
{
    auto r = getLocalBounds();

    // ---- Header ----
    auto header = r.removeFromTop (headerH).reduced (18, 4);
    header.removeFromLeft (340);
    outputKnob.setBounds (header.removeFromRight (70));
    header.removeFromRight (4);
    inputKnob.setBounds (header.removeFromRight (70));
    header.removeFromRight (12);
    {
        auto meters = header.removeFromRight (150).withSizeKeepingCentre (150, 28);
        auto top = meters.removeFromTop (meters.getHeight() / 2).reduced (0, 2);
        inMeter.setBounds (top.withTrimmedLeft (30));
        outMeter.setBounds (meters.reduced (0, 2).withTrimmedLeft (30));
    }
    header.removeFromRight (16);
    qualityBox.setBounds (header.removeFromRight (170).withSizeKeepingCentre (170, 28));
    header.removeFromRight (10);
    latencyLabel.setBounds (header.removeFromRight (120));

    r.reduce (14, 0);
    r.removeFromBottom (14);

    // ---- Pitch panel ----
    pitchPanel = r.removeFromTop ((int) (r.getHeight() * 0.52f));
    {
        auto p = pitchPanel.reduced (14, 8);
        auto top = p.removeFromTop (30);
        pitchPower.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 20));
        p.removeFromTop (4);

        auto controls = p.removeFromLeft (juce::jmax (320, (int) (p.getWidth() * 0.36f)));
        p.removeFromLeft (12);
        pitchDisplay.setBounds (p);

        auto choices = controls.removeFromTop (48);
        const int cw = (choices.getWidth() - 16) / 3;
        keyChoice.setBounds (choices.removeFromLeft (cw - 14));
        choices.removeFromLeft (8);
        scaleChoice.setBounds (choices.removeFromLeft (cw + 14));
        choices.removeFromLeft (8);
        engineChoice.setBounds (choices);
        controls.removeFromTop (8);

        const int halfH = controls.getHeight() / 2;
        auto row1 = controls.removeFromTop (halfH);
        auto row2 = controls;
        const int third = row1.getWidth() / 3;
        retuneKnob.setBounds     (row1.removeFromLeft (third));
        humanizeKnob.setBounds   (row1.removeFromLeft (third));
        transitionKnob.setBounds (row1);
        formantKnob.setBounds    (row2.removeFromLeft (third));
        pitchMixKnob.setBounds   (row2.removeFromLeft (third));
    }

    // ---- Tabs ----
    r.removeFromTop (10);
    auto tabs = r.removeFromTop (28);
    chainTab.setBounds (tabs.removeFromLeft (110));
    tabs.removeFromLeft (6);
    recordTab.setBounds (tabs.removeFromLeft (110));
    r.removeFromTop (8);
    recordPanel.setBounds (r);

    // ---- Module chain ----
    const int gap = 10;
    const int total = r.getWidth() - 3 * gap;
    // Proportions: HPF narrow, EQ widest.
    const int wHpf = (int) (total * 0.18f), wEq = (int) (total * 0.34f), wComp = (int) (total * 0.26f);
    const int wDe = total - wHpf - wEq - wComp;
    hpfStrip.setBounds (r.removeFromLeft (wHpf));   r.removeFromLeft (gap);
    eqStrip.setBounds (r.removeFromLeft (wEq));     r.removeFromLeft (gap);
    compStrip.setBounds (r.removeFromLeft (wComp)); r.removeFromLeft (gap);
    deessStrip.setBounds (r.removeFromLeft (wDe));
}
