#include "ReferenceLibrary.h"

namespace benchmark
{
    namespace IDs
    {
        static const juce::Identifier reference { "Reference" };
        static const juce::Identifier active { "active" };
        static const juce::Identifier id { "id" };
        static const juce::Identifier path { "path" };
        static const juce::Identifier name { "name" };
    }

    const juce::Identifier ReferenceLibrary::stateType { "References" };

    ReferenceLibrary::ReferenceLibrary (RealtimeHandoff<ReferenceAudio>& audioHandoff)
        : handoff (audioHandoff)
    {
        snapshot = createState();
        startTimerHz (2);
    }

    ReferenceLibrary::~ReferenceLibrary()
    {
        stopTimer();

        for (auto& e : entries)
            if (e.ticket != nullptr)
                e.ticket->cancelled = true;
    }

    //==============================================================================
    ReferenceLibrary::AddResult ReferenceLibrary::addFiles (const juce::Array<juce::File>& files)
    {
        AddResult result;
        int firstNew = -1;

        for (const auto& file : files)
        {
            if (! ReferenceDecoder::isSupportedFile (file))
            {
                ++result.unsupported;
                continue;
            }

            if (std::any_of (entries.begin(), entries.end(), [&] (const Entry& e) { return e.file == file; }))
            {
                ++result.duplicates;
                continue;
            }

            if (size() >= maxReferences)
            {
                ++result.overLimit;
                continue;
            }

            Entry entry;
            entry.file = file;
            entry.name = file.getFileNameWithoutExtension();
            entries.push_back (std::move (entry));
            startLoad (entries.back());

            if (firstNew < 0)
                firstNew = size() - 1;
            ++result.added;
        }

        if (activeIndex < 0 && firstNew >= 0)
            setActive (firstNew);

        juce::StringArray problems;
        if (result.overLimit > 0)   problems.add (juce::String (result.overLimit) + " skipped (limit is " + juce::String (maxReferences) + " references)");
        if (result.unsupported > 0) problems.add (juce::String (result.unsupported) + " unsupported (use WAV, AIFF, FLAC or MP3)");
        if (result.duplicates > 0)  problems.add (juce::String (result.duplicates) + " already in the list");

        if (! problems.isEmpty())
            notify (problems.joinIntoString ("; "));

        stateChanged();
        return result;
    }

    void ReferenceLibrary::remove (int index)
    {
        if (! juce::isPositiveAndBelow (index, size()))
            return;

        if (auto& t = entries[(size_t) index].ticket)
            t->cancelled = true;

        entries.erase (entries.begin() + index);

        if (index == activeIndex)
        {
            activeIndex = -1;
            setActive (entries.empty() ? -1 : std::min (index, size() - 1));
        }
        else if (index < activeIndex)
        {
            --activeIndex;
        }

        stateChanged();
    }

    void ReferenceLibrary::relocate (int index, const juce::File& newFile)
    {
        if (! juce::isPositiveAndBelow (index, size()) || ! ReferenceDecoder::isSupportedFile (newFile))
            return;

        auto& entry = entries[(size_t) index];
        entry.file = newFile;
        entry.name = newFile.getFileNameWithoutExtension();
        entry.audio = nullptr;
        entry.hasInfo = false;
        startLoad (entry);

        if (index == activeIndex)
            publishActive();

        stateChanged();
    }

    void ReferenceLibrary::setActive (int index)
    {
        if (! juce::isPositiveAndBelow (index, size()))
            index = -1;

        activeIndex = index;

        if (auto* entry = index >= 0 ? &entries[(size_t) index] : nullptr)
        {
            entry->lastUsed = ++useCounter;

            // Reload audio that was unloaded to save memory.
            if (entry->status == Status::ready && entry->audio == nullptr)
                startLoad (*entry);
        }

        publishActive();
        stateChanged();
    }

    const ReferenceLibrary::Entry* ReferenceLibrary::getActive() const noexcept
    {
        return activeIndex >= 0 ? &entries[(size_t) activeIndex] : nullptr;
    }

    bool ReferenceLibrary::isAnyLoading() const noexcept
    {
        return std::any_of (entries.begin(), entries.end(), [] (const Entry& e) { return e.status == Status::loading; });
    }

    //==============================================================================
    void ReferenceLibrary::setTargetSampleRate (double newRate)
    {
        if (newRate <= 0.0 || std::abs (newRate - targetSampleRate) < 1e-6)
            return;

        targetSampleRate = newRate;

        for (auto& entry : entries)
        {
            const auto needsAudio = entry.audio != nullptr || entry.status == Status::waiting || entry.status == Status::loading;

            if (needsAudio)
            {
                entry.audio = nullptr;
                startLoad (entry);
            }
        }

        publishActive();
        sendChangeMessage();
    }

    void ReferenceLibrary::startLoad (Entry& entry)
    {
        if (entry.ticket != nullptr)
            entry.ticket->cancelled = true;

        entry.ticket = nullptr;
        entry.message = {};

        if (! entry.file.existsAsFile())
        {
            entry.status = Status::missing;
            entry.message = "File not found";
            return;
        }

        if (targetSampleRate <= 0.0)
        {
            entry.status = Status::waiting;
            return;
        }

        entry.status = Status::loading;

        juce::WeakReference<ReferenceLibrary> weakThis (this);
        entry.ticket = loader.load (entry.file, targetSampleRate,
                                    [weakThis, id = entry.id] (const std::shared_ptr<ReferenceLoader::Ticket>& ticket, ReferenceDecoder::Result result)
                                    {
                                        if (auto* self = weakThis.get())
                                            self->loadFinished (id, ticket, std::move (result));
                                    });
    }

    void ReferenceLibrary::loadFinished (const juce::Uuid& id, const std::shared_ptr<ReferenceLoader::Ticket>& ticket, ReferenceDecoder::Result result)
    {
        auto* entry = findEntry (id);

        if (entry == nullptr || entry->ticket != ticket)
            return; // removed or superseded

        entry->ticket = nullptr;

        if (result.audio != nullptr && std::abs (result.audio->sampleRate - targetSampleRate) < 1e-6)
        {
            entry->status = Status::ready;
            entry->info = result.audio->info;
            entry->hasInfo = true;
            entry->audio = std::move (result.audio);

            if (entry == getActive())
                publishActive();

            enforceMemoryBudget();
        }
        else
        {
            entry->status = entry->file.existsAsFile() ? Status::failed : Status::missing;
            entry->message = result.error.isNotEmpty() ? result.error : juce::String ("Could not load file");
            notify (entry->name + ": " + entry->message);
        }

        sendChangeMessage();
    }

    void ReferenceLibrary::publishActive()
    {
        const auto* active = getActive();
        handoff.publish (active != nullptr ? active->audio : nullptr);
    }

    void ReferenceLibrary::setMemoryBudget (size_t bytes)
    {
        memoryBudget = bytes;
        enforceMemoryBudget();
    }

    size_t ReferenceLibrary::getLoadedBytes() const noexcept
    {
        size_t total = 0;
        for (const auto& e : entries)
            if (e.audio != nullptr)
                total += e.audio->getMemoryBytes();
        return total;
    }

    void ReferenceLibrary::enforceMemoryBudget()
    {
        auto total = getLoadedBytes();

        while (total > memoryBudget)
        {
            // Unload the least recently used reference that isn't active.
            Entry* victim = nullptr;

            for (auto& e : entries)
                if (e.audio != nullptr && &e != getActive() && (victim == nullptr || e.lastUsed < victim->lastUsed))
                    victim = &e;

            if (victim == nullptr)
                break;

            total -= victim->audio->getMemoryBytes();
            victim->audio = nullptr;
        }
    }

    ReferenceLibrary::Entry* ReferenceLibrary::findEntry (const juce::Uuid& id) noexcept
    {
        for (auto& e : entries)
            if (e.id == id)
                return &e;
        return nullptr;
    }

    void ReferenceLibrary::timerCallback()
    {
        handoff.collectGarbage();

        if (isAnyLoading())
            sendChangeMessage(); // progress updates
    }

    void ReferenceLibrary::notify (const juce::String& text)
    {
        if (onMessage != nullptr)
            onMessage (text);
    }

    //==============================================================================
    juce::ValueTree ReferenceLibrary::createState() const
    {
        juce::ValueTree state (stateType);
        state.setProperty (IDs::active, activeIndex, nullptr);

        for (const auto& e : entries)
        {
            juce::ValueTree child (IDs::reference);
            child.setProperty (IDs::id, e.id.toString(), nullptr);
            child.setProperty (IDs::path, e.file.getFullPathName(), nullptr);
            child.setProperty (IDs::name, e.name, nullptr);
            state.appendChild (child, nullptr);
        }

        return state;
    }

    void ReferenceLibrary::restoreState (const juce::ValueTree& state)
    {
        for (auto& e : entries)
            if (e.ticket != nullptr)
                e.ticket->cancelled = true;

        entries.clear();
        activeIndex = -1;

        if (state.hasType (stateType))
        {
            for (const auto& child : state)
            {
                if (! child.hasType (IDs::reference) || size() >= maxReferences)
                    continue;

                const auto path = child[IDs::path].toString();

                if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
                    continue;

                Entry entry;
                if (const auto idString = child[IDs::id].toString(); idString.isNotEmpty())
                    entry.id = juce::Uuid (idString);

                entry.file = juce::File (path);
                entry.name = child[IDs::name].toString();
                if (entry.name.isEmpty())
                    entry.name = entry.file.getFileNameWithoutExtension();

                entries.push_back (std::move (entry));
                startLoad (entries.back());
            }

            const auto savedActive = (int) state.getProperty (IDs::active, -1);
            activeIndex = juce::isPositiveAndBelow (savedActive, size()) ? savedActive : -1;
        }

        if (activeIndex >= 0)
            entries[(size_t) activeIndex].lastUsed = ++useCounter;

        publishActive();
        stateChanged();
    }

    juce::ValueTree ReferenceLibrary::getStateSnapshot() const
    {
        const juce::ScopedLock sl (snapshotLock);
        return snapshot.createCopy();
    }

    void ReferenceLibrary::stateChanged()
    {
        auto newSnapshot = createState();

        {
            const juce::ScopedLock sl (snapshotLock);
            snapshot = std::move (newSnapshot);
        }

        sendChangeMessage();
    }

    bool ReferenceLibrary::waitUntilLoaded (int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (juce::Time::getMillisecondCounter() < end)
        {
            loader.waitForAll (timeoutMs);

           #if JUCE_MODAL_LOOPS_PERMITTED
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
           #endif

            if (! isAnyLoading())
                return true;
        }

        return ! isAnyLoading();
    }
}
