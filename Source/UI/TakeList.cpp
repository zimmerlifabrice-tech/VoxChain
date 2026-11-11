#include "TakeList.h"
#include "LookAndFeel.h"

namespace vox
{
namespace
{
    juce::String formatTime (double seconds)
    {
        const int s = (int) std::round (seconds);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

// ============================================================================
class TakeList::Row : public juce::Component
{
public:
    explicit Row (TakeList& o) : owner (o) {}

    void update (int r, bool sel)
    {
        row = r;
        selected = sel;
        take = owner.takes.getTake (row);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().reduced (2, 1).toFloat();
        g.setColour (selected ? colours::accent.withAlpha (0.25f) : colours::panelLight.withAlpha (0.5f));
        g.fillRoundedRectangle (b, 4.0f);

        auto r = getLocalBounds().reduced (8, 0);
        g.setFont (juce::FontOptions (15.0f));
        g.setColour (take.starred ? juce::Colour (0xffffc94d) : colours::textDim.withAlpha (0.6f));
        g.drawText (take.starred ? juce::String::fromUTF8 ("\xe2\x98\x85") : juce::String::fromUTF8 ("\xe2\x98\x86"),
                    r.removeFromLeft (20), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (12.0f));
        g.setColour (colours::textDim);
        g.drawText (formatTime (take.seconds), r.removeFromRight (44), juce::Justification::centredRight);

        g.setColour (take.missing ? colours::warn : colours::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (take.missing ? take.name + "  (file missing)" : take.name, r, juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        if (e.x < 28) // star
        {
            owner.takes.setStarred (row, ! take.starred);
            return;
        }
        owner.list.selectRow (row);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (owner.onRenameRequested)
            owner.onRenameRequested (row);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStarted || e.getDistanceFromDragStart() < 6 || take.missing)
            return;
        dragStarted = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ take.file.getFullPathName() }, false, this);
    }

private:
    TakeList& owner;
    int row = -1;
    bool selected = false, dragStarted = false;
    TakeManager::Take take;
};

// ============================================================================
TakeList::TakeList (TakeManager& t) : takes (t)
{
    list.setModel (this);
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, colours::background);
    list.setColour (juce::ListBox::outlineColourId, colours::outline);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);
    takes.addChangeListener (this);
    lastCount = takes.getNumTakes();
}

TakeList::~TakeList()
{
    takes.removeChangeListener (this);
}

int TakeList::getNumRows()
{
    return takes.getNumTakes();
}

juce::Component* TakeList::refreshComponentForRow (int row, bool selected, juce::Component* existing)
{
    auto* r = dynamic_cast<Row*> (existing);
    if (row >= getNumRows())
    {
        delete existing;
        return nullptr;
    }
    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }
    r->update (row, selected);
    return r;
}

void TakeList::selectedRowsChanged (int lastRowSelected)
{
    if (onSelectionChanged)
        onSelectionChanged (lastRowSelected);
}

void TakeList::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const int count = takes.getNumTakes();
    list.updateContent();
    list.repaint();
    if (count > lastCount)
        list.selectRow (count - 1); // select the newest take
    else if (list.getSelectedRow() >= count)
        list.selectRow (count - 1);
    lastCount = count;
    if (onSelectionChanged)
        onSelectionChanged (list.getSelectedRow());
}

} // namespace vox
