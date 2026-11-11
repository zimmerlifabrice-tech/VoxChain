#include "WaveformView.h"
#include "LookAndFeel.h"

namespace vox
{
WaveformView::WaveformView (const TakePlayer& p) : player (p)
{
    formats.registerBasicFormats();
    thumbnail.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    startTimerHz (30);
}

WaveformView::~WaveformView()
{
    thumbnail.removeChangeListener (this);
}

void WaveformView::setFile (const juce::File& f)
{
    if (f == file)
        return;
    file = f;
    if (file.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (file));
    else
        thumbnail.clear();
    repaint();
}

void WaveformView::timerCallback()
{
    if (player.isPlaying() || dragStarted)
        repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 6.0f);

    auto area = getLocalBounds().reduced (8);
    if (thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (file == juce::File() ? "No take selected" : "Take file not found", area,
                          juce::Justification::centred, 2);
        return;
    }

    auto wave = area.withTrimmedBottom (16);
    g.setColour (colours::accent.withAlpha (0.85f));
    thumbnail.drawChannels (g, wave, 0.0, thumbnail.getTotalLength(), 1.0f);

    if (player.isPlaying())
    {
        const float x = (float) wave.getX() + (float) wave.getWidth() * (float) player.getProgress();
        g.setColour (colours::accent2);
        g.drawLine (x, (float) wave.getY(), x, (float) wave.getBottom(), 2.0f);
    }

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Drag into the FL Studio Playlist", area.removeFromBottom (14), juce::Justification::centredRight);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6 || ! file.existsAsFile())
        return;
    dragStarted = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                               [this] { dragStarted = false; });
}

} // namespace vox
