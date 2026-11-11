#include "TakeManager.h"

namespace vox
{
namespace
{
    const juce::Identifier takesId ("Takes"), takeId ("Take"), sessionId ("sessionId"),
                           nameId ("name"), fileId ("file"), secondsId ("seconds"),
                           starredId ("starred"), counterId ("counter");
}

juce::File TakeManager::getRootFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("VoxChain").getChildFile ("Takes");
}

void TakeManager::attach (juce::ValueTree pluginState)
{
    auto child = pluginState.getChildWithName (takesId);
    if (! child.isValid())
    {
        child = juce::ValueTree (takesId);
        pluginState.appendChild (child, nullptr);
    }
    if (child.getProperty (sessionId).toString().isEmpty())
    {
        // One folder per plugin instance/project, e.g. "2026-11-11_0832_ab12".
        const auto now = juce::Time::getCurrentTime();
        child.setProperty (sessionId, now.formatted ("%Y-%m-%d_%H%M%S_")
                                          + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()).substring (0, 4),
                           nullptr);
    }
    takes = child;
    sendChangeMessage();
}

juce::File TakeManager::getSessionFolder() const
{
    return getRootFolder().getChildFile (takes.getProperty (sessionId).toString());
}

int TakeManager::getNextTakeNumber() const
{
    return (int) takes.getProperty (counterId, 0) + 1;
}

juce::File TakeManager::createNextTakeFile() const
{
    auto folder = getSessionFolder();
    folder.createDirectory();
    return folder.getNonexistentChildFile ("Take_" + juce::String (getNextTakeNumber()).paddedLeft ('0', 2), ".wav", false);
}

void TakeManager::addTake (const juce::File& file, double seconds, const juce::String& name)
{
    juce::ValueTree t (takeId);
    t.setProperty (nameId, name, nullptr);
    t.setProperty (fileId, file.getFullPathName(), nullptr);
    t.setProperty (secondsId, seconds, nullptr);
    t.setProperty (starredId, false, nullptr);
    takes.appendChild (t, nullptr);
    takes.setProperty (counterId, getNextTakeNumber(), nullptr);
    sendChangeMessage();
}

void TakeManager::removeTake (int index, bool deleteFile)
{
    auto t = takes.getChild (index);
    if (! t.isValid())
        return;
    if (deleteFile)
        juce::File (t.getProperty (fileId).toString()).deleteFile();
    takes.removeChild (t, nullptr);
    sendChangeMessage();
}

void TakeManager::renameTake (int index, const juce::String& newName)
{
    auto t = takes.getChild (index);
    if (t.isValid() && newName.isNotEmpty())
    {
        t.setProperty (nameId, newName, nullptr);
        sendChangeMessage();
    }
}

void TakeManager::setStarred (int index, bool starred)
{
    auto t = takes.getChild (index);
    if (t.isValid())
    {
        t.setProperty (starredId, starred, nullptr);
        sendChangeMessage();
    }
}

int TakeManager::getNumTakes() const
{
    return takes.getNumChildren();
}

TakeManager::Take TakeManager::getTake (int index) const
{
    Take r;
    auto t = takes.getChild (index);
    if (! t.isValid())
        return r;
    r.name = t.getProperty (nameId).toString();
    r.file = juce::File (t.getProperty (fileId).toString());
    r.seconds = (double) t.getProperty (secondsId, 0.0);
    r.starred = (bool) t.getProperty (starredId, false);
    r.missing = ! r.file.existsAsFile();
    return r;
}

} // namespace vox
