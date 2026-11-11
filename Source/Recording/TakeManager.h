#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vox
{
/**
    The list of recorded takes. Message thread only.

    The list lives in the plugin state (a "Takes" child of the parameter
    ValueTree), so FL Studio saves it with the .flp project. The audio
    itself is stored as WAV files in
        Documents/VoxChain/Takes/<session id>/Take_NN.wav
*/
class TakeManager : public juce::ChangeBroadcaster
{
public:
    struct Take
    {
        juce::String name;
        juce::File file;
        double seconds = 0.0;
        bool starred = false;
        bool missing = false;
    };

    /** (Re)connects to the plugin state; call after the state was replaced. */
    void attach (juce::ValueTree pluginState);

    juce::File getSessionFolder() const;
    juce::File createNextTakeFile() const;
    int getNextTakeNumber() const;

    void addTake (const juce::File& file, double seconds, const juce::String& name);
    void removeTake (int index, bool deleteFile = true);
    void renameTake (int index, const juce::String& newName);
    void setStarred (int index, bool starred);

    int getNumTakes() const;
    Take getTake (int index) const;

    static juce::File getRootFolder();

private:
    juce::ValueTree takes;
};

} // namespace vox
