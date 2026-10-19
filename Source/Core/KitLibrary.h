#pragma once

#include "SampleRef.h"
#include "Tracks.h"

namespace bf
{

constexpr int kNumRoles = 8;

/** A kit: a named set of samples organised by drum role. */
struct KitInfo
{
    juce::String name;
    bool builtIn = false;
    juce::File folder;
    std::array<juce::Array<SampleRef>, kNumRoles> samples;

    /** Samples usable for a role, falling back to related roles (ghost -> snare, open -> closed hat...). */
    juce::Array<SampleRef> samplesFor (Role) const;
    int totalSamples() const;
};

namespace KitLibrary
{
    /** Built-in kits followed by every sub-folder of `kitsFolder` that contains audio. Does file
        I/O: call it from a background thread for large libraries. */
    std::vector<KitInfo> scan (const juce::File& kitsFolder);

    KitInfo scanFolder (const juce::File& folder);

    /** Classifies a sample by its sub-folder names and file name, e.g. "Kicks/BD_01.wav" -> Kick.
        Returns false if nothing matches. */
    bool classify (const juce::File& file, const juce::File& kitRoot, Role& role);

    const char* roleFolderName (Role);
}

} // namespace bf
