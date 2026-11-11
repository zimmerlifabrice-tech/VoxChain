#include "RecordPanel.h"
#include "LookAndFeel.h"
#include "Parameters/ParameterLayout.h"

namespace vox
{
// ============================================================================
class RecordPanel::RecordButton : public juce::Component
{
public:
    std::function<void()> onClick;
    VoxChainProcessor::RecState state = VoxChainProcessor::RecState::Idle;
    bool armed = false, blink = false;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre (d, d);

        g.setColour (colours::panelLight);
        g.fillEllipse (c);
        g.setColour (armed ? colours::warn.withAlpha (0.8f) : colours::outline);
        g.drawEllipse (c, 2.0f);

        const bool active = state == VoxChainProcessor::RecState::Recording
                         || (state == VoxChainProcessor::RecState::CountIn && blink);
        auto dot = c.reduced (d * 0.27f);
        g.setColour (active ? juce::Colour (0xffff3355) : colours::warn.withAlpha (armed ? 0.75f : 0.45f));
        if (state == VoxChainProcessor::RecState::Recording)
            g.fillRoundedRectangle (dot.reduced (d * 0.04f), 3.0f); // stop symbol while recording
        else
            g.fillEllipse (dot);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }
};

// ============================================================================
RecordPanel::RecordPanel (VoxChainProcessor& p)
    : processor (p), state (p.getState()),
      takeList (p.getTakes()), waveform (p.getPlayer())
{
    recordButton = std::make_unique<RecordButton>();
    recordButton->onClick = [this] { processor.toggleRecord(); updateStatus(); };
    addAndMakeVisible (*recordButton);

    statusLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    statusLabel.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (statusLabel);

    armButton.setTooltip ("Arm recording. While armed, the MONITOR settings decide what you hear.");
    armButton.onClick = [this] { processor.setArmed (armButton.getToggleState()); };
    addAndMakeVisible (armButton);

    auto fill = [this] (juce::ComboBox& box, const char* id)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
            box.addItemList (c->choices, 1);
        addAndMakeVisible (box);
    };
    fill (modeBox, ids::recMode);
    fill (monitorModeBox, ids::recMonitorMode);
    fill (countInBox, ids::recCountIn);
    modeBox.setTooltip ("Manual: the record button starts/stops. With FL Studio: arm, then FL's Play starts and Stop ends the take.");
    monitorModeBox.setTooltip ("What you hear while armed. Dry and FX add no latency; FX + Tune uses the fastest engine (~23 ms).");
    countInBox.setTooltip ("Count-in before recording starts (manual mode).");

    monitorButton.setTooltip ("Hear yourself while recording (on/off)");
    metronomeButton.setTooltip ("Metronome while recording (only in your headphones, never in the take)");
    addAndMakeVisible (monitorButton);
    addAndMakeVisible (metronomeButton);

    latencyHint.setFont (juce::FontOptions (11.0f));
    latencyHint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (latencyHint);

    offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
    offsetSlider.setTooltip ("Shifts takes earlier to compensate the audio interface's input latency.");
    offsetSlider.setColour (juce::Slider::trackColourId, colours::accent);
    offsetSlider.setColour (juce::Slider::backgroundColourId, colours::outline);
    offsetSlider.setColour (juce::Slider::thumbColourId, colours::text);
    addAndMakeVisible (offsetSlider);
    offsetLabel.setText ("OFFSET", juce::dontSendNotification);
    addAndMakeVisible (offsetLabel);

    monitorAtt     = std::make_unique<ButtonAtt> (state, ids::recMonitorOn, monitorButton);
    metronomeAtt   = std::make_unique<ButtonAtt> (state, ids::recMetronome, metronomeButton);
    modeAtt        = std::make_unique<ComboAtt>  (state, ids::recMode, modeBox);
    monitorModeAtt = std::make_unique<ComboAtt>  (state, ids::recMonitorMode, monitorModeBox);
    countInAtt     = std::make_unique<ComboAtt>  (state, ids::recCountIn, countInBox);
    offsetAtt      = std::make_unique<SliderAtt> (state, ids::recOffsetMs, offsetSlider);

    takeList.onSelectionChanged = [this] (int i) { selectionChanged (i); };
    takeList.onRenameRequested = [this] (int i) { renameTake (i); };
    addAndMakeVisible (takeList);
    addAndMakeVisible (waveform);

    for (auto* b : { &playButton, &deleteButton, &exportDryButton, &exportFxButton })
    {
        b->setColour (juce::TextButton::buttonColourId, colours::panelLight);
        b->setColour (juce::TextButton::textColourOffId, colours::text);
        addAndMakeVisible (b);
    }
    playButton.onClick = [this] { togglePreview(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    exportDryButton.onClick = [this] { exportSelected (false); };
    exportFxButton.onClick = [this] { exportSelected (true); };
    exportDryButton.setTooltip ("Export the raw take (for tuning later)");
    exportFxButton.setTooltip ("Export the take with the whole VoxChain chain applied (current settings)");

    selectionChanged (processor.getTakes().getNumTakes() - 1);
    if (processor.getTakes().getNumTakes() > 0)
        takeList.selectTake (processor.getTakes().getNumTakes() - 1);

    startTimerHz (15);
    updateStatus();
}

RecordPanel::~RecordPanel()
{
    stopTimer();
}

void RecordPanel::timerCallback()
{
    updateStatus();
}

void RecordPanel::updateStatus()
{
    using RS = VoxChainProcessor::RecState;
    const auto st = processor.getRecState();
    const bool armed = processor.isArmed();
    const bool withFl = state.getRawParameterValue (ids::recMode)->load() > 0.5f;

    armButton.setToggleState (armed, juce::dontSendNotification);
    recordButton->state = st;
    recordButton->armed = armed;
    recordButton->blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    recordButton->repaint();

    juce::String text;
    auto colour = colours::text;
    const double secs = processor.getRecordedSeconds();
    const juce::String time = juce::String ((int) secs / 60) + ":" + juce::String ((int) secs % 60).paddedLeft ('0', 2);
    switch (st)
    {
        case RS::CountIn:   text = "Count-in ... " + juce::String (juce::jmax (1, processor.getCountInBeatsLeft() - 1)); colour = juce::Colour (0xffffc94d); break;
        case RS::Recording: text = "REC  " + time; colour = juce::Colour (0xffff3355); break;
        case RS::Finishing: text = "Saving take ..."; break;
        case RS::Idle:
        default:
            text = ! armed ? "Ready" : (withFl ? "Armed - press Play in FL Studio" : "Armed - press record");
            colour = armed ? juce::Colour (0xffff7a8f) : colours::textDim;
            break;
    }

    if (processor.getAndResetClip())
        clipHold = 30;
    if (clipHold > 0)
    {
        --clipHold;
        text << "   CLIP!";
        colour = colours::warn;
    }
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, colour);

    // Monitoring latency hint
    const bool monOn = state.getRawParameterValue (ids::recMonitorOn)->load() > 0.5f;
    const int monMode = (int) state.getRawParameterValue (ids::recMonitorMode)->load();
    latencyHint.setText (! monOn ? "you hear nothing of your voice"
                                 : (monMode == 2 ? "+" + juce::String (juce::roundToInt (processor.getLatencyMs())) + " ms (fastest tuning)"
                                                 : "+0 ms (direct)"),
                         juce::dontSendNotification);
    latencyHint.setColour (juce::Label::textColourId, colours::textDim);

    playButton.setButtonText (processor.getPlayer().isPlaying() ? "STOP" : "PLAY");
    exportFxButton.setEnabled (! processor.getExporter().isBusy());
    if (processor.getExporter().isBusy())
        exportFxButton.setButtonText ("EXPORT " + juce::String ((int) (processor.getExporter().getProgress() * 100)) + "%");
    else
        exportFxButton.setButtonText ("EXPORT FX");

    const bool hasSel = takeList.getSelectedTake() >= 0;
    for (auto* b : { &playButton, &deleteButton, &exportDryButton })
        b->setEnabled (hasSel);
    if (! hasSel)
        exportFxButton.setEnabled (false);
}

void RecordPanel::selectionChanged (int index)
{
    if (index < 0 || index >= processor.getTakes().getNumTakes())
    {
        waveform.setFile ({});
        return;
    }
    waveform.setFile (processor.getTakes().getTake (index).file);
}

void RecordPanel::togglePreview()
{
    auto& player = processor.getPlayer();
    if (player.isPlaying())
    {
        player.stop();
        return;
    }
    const int i = takeList.getSelectedTake();
    if (i >= 0)
        player.play (processor.getTakes().getTake (i).file, processor.getSampleRateForTakes());
}

void RecordPanel::renameTake (int index)
{
    auto take = processor.getTakes().getTake (index);
    auto* w = new juce::AlertWindow ("Rename take", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", take.name);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, index] (int result)
    {
        if (result == 1)
            processor.getTakes().renameTake (index, w->getTextEditorContents ("name").trim());
    }), true);
}

void RecordPanel::deleteSelected()
{
    const int i = takeList.getSelectedTake();
    if (i < 0)
        return;
    const auto take = processor.getTakes().getTake (i);
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon,
        "Delete take?", "\"" + take.name + "\" will be deleted from disk.", "Delete", "Cancel", this);
    juce::AlertWindow::showAsync (options, [this, i] (int result)
    {
        if (result == 1)
        {
            processor.getPlayer().stop();
            processor.getTakes().removeTake (i, true);
        }
    });
}

void RecordPanel::exportSelected (bool withEffects)
{
    const int i = takeList.getSelectedTake();
    if (i < 0)
        return;
    const auto take = processor.getTakes().getTake (i);
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                               .getChildFile (juce::File::createLegalFileName (take.name + (withEffects ? " (VoxChain FX)" : " (dry)")) + ".wav");

    chooser = std::make_unique<juce::FileChooser> ("Export take", suggested, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, take, withEffects] (const juce::FileChooser& fc)
        {
            auto dest = fc.getResult();
            if (dest == juce::File())
                return;
            dest = dest.withFileExtension ("wav");
            auto report = [this] (bool ok, juce::String msg)
            {
                juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                    ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                    ok ? "Export finished" : "Export failed", msg, "OK", this), nullptr);
            };
            if (! withEffects)
                report (TakeExporter::exportDry (take.file, dest), dest.getFullPathName());
            else
                processor.getExporter().startProcessedExport (take.file, dest, report);
        });
}

void RecordPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
}

void RecordPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);

    // ---- left: controls ----
    auto left = r.removeFromLeft (330);
    r.removeFromLeft (12);
    {
        auto top = left.removeFromTop (60);
        recordButton->setBounds (top.removeFromLeft (60));
        top.removeFromLeft (10);
        statusLabel.setBounds (top);
        left.removeFromTop (6);

        auto row = [&left] { auto x = left.removeFromTop (28); left.removeFromTop (6); return x; };
        auto r1 = row();
        armButton.setBounds (r1.removeFromLeft (90).reduced (0, 3));
        r1.removeFromLeft (8);
        modeBox.setBounds (r1);

        auto r2 = row();
        monitorButton.setBounds (r2.removeFromLeft (90).reduced (0, 3));
        r2.removeFromLeft (8);
        monitorModeBox.setBounds (r2.removeFromLeft (120));
        r2.removeFromLeft (6);
        latencyHint.setBounds (r2);

        auto r3 = row();
        metronomeButton.setBounds (r3.removeFromLeft (90).reduced (0, 3));
        r3.removeFromLeft (8);
        countInBox.setBounds (r3.removeFromLeft (120));

        auto r4 = row();
        offsetLabel.setBounds (r4.removeFromLeft (60));
        offsetSlider.setBounds (r4);
    }

    // ---- middle: take list + actions ----
    auto mid = r.removeFromLeft (juce::jmax (260, r.getWidth() * 45 / 100));
    r.removeFromLeft (12);
    auto actions = mid.removeFromBottom (30);
    mid.removeFromBottom (6);
    takeList.setBounds (mid);
    const int bw = (actions.getWidth() - 18) / 4;
    playButton.setBounds (actions.removeFromLeft (bw));      actions.removeFromLeft (6);
    deleteButton.setBounds (actions.removeFromLeft (bw));    actions.removeFromLeft (6);
    exportDryButton.setBounds (actions.removeFromLeft (bw)); actions.removeFromLeft (6);
    exportFxButton.setBounds (actions);

    // ---- right: waveform ----
    waveform.setBounds (r);
}

} // namespace vox
