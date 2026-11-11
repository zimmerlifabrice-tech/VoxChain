#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Recording/TakeManager.h"
#include <functional>

namespace vox
{
/**
    List of recorded takes. Click selects, double-click renames, the star
    marks the best take, and a row can be dragged straight into the
    FL Studio Playlist (external file drag).
*/
class TakeList : public juce::Component,
                 private juce::ListBoxModel,
                 private juce::ChangeListener
{
public:
    explicit TakeList (TakeManager& takes);
    ~TakeList() override;

    int getSelectedTake() const { return list.getSelectedRow(); }
    void selectTake (int index) { list.selectRow (index); }

    std::function<void (int)> onSelectionChanged;
    std::function<void (int)> onRenameRequested;

    void resized() override { list.setBounds (getLocalBounds()); }

private:
    class Row;

    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    TakeManager& takes;
    juce::ListBox list;
    int lastCount = 0;
};

} // namespace vox
