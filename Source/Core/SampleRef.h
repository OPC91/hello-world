#pragma once

#include <juce_core/juce_core.h>

namespace bf
{

/** Where a track's sample comes from: a built-in synthesized sound or a file on disk. */
struct SampleRef
{
    juce::String builtInId;  // "<Kit>/<Sound>", e.g. "Clean/Kick A"
    juce::File file;

    static SampleRef builtIn (const juce::String& id) { SampleRef r; r.builtInId = id; return r; }
    static SampleRef fromFile (const juce::File& f)   { SampleRef r; r.file = f; return r; }

    bool isBuiltIn() const noexcept { return builtInId.isNotEmpty(); }
    bool isFile() const noexcept    { return ! isBuiltIn() && file != juce::File(); }
    bool isEmpty() const noexcept   { return ! isBuiltIn() && ! isFile(); }

    juce::String getDisplayName() const
    {
        if (isBuiltIn()) return builtInId.fromLastOccurrenceOf ("/", false, false);
        if (isFile())    return file.getFileNameWithoutExtension();
        return {};
    }

    juce::String toString() const
    {
        if (isBuiltIn()) return "builtin:" + builtInId;
        if (isFile())    return "file:" + file.getFullPathName();
        return {};
    }

    static SampleRef fromString (const juce::String& s)
    {
        if (s.startsWith ("builtin:")) return builtIn (s.fromFirstOccurrenceOf ("builtin:", false, false));
        if (s.startsWith ("file:"))
        {
            const auto path = s.fromFirstOccurrenceOf ("file:", false, false);
            if (juce::File::isAbsolutePath (path))
                return fromFile (juce::File (path));
        }
        return {};
    }

    bool operator== (const SampleRef& o) const { return builtInId == o.builtInId && file == o.file; }
    bool operator!= (const SampleRef& o) const { return ! operator== (o); }
};

} // namespace bf
