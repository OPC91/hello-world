#include "KitLibrary.h"
#include "AudioFileLoader.h"
#include "BuiltInKits.h"

namespace bf
{

juce::Array<SampleRef> KitInfo::samplesFor (Role role) const
{
    auto list = samples[(size_t) role];

    if (! list.isEmpty())
        return list;

    // Related roles, so a kit without (say) ghost snares still gives the track something sensible.
    switch (role)
    {
        case Role::GhostSnare: return samples[(size_t) Role::Snare];
        case Role::OpenHat:    return samples[(size_t) Role::ClosedHat];
        case Role::ClosedHat:  return samples[(size_t) Role::OpenHat];
        case Role::Ride:       return ! samples[(size_t) Role::OpenHat].isEmpty() ? samples[(size_t) Role::OpenHat] : samples[(size_t) Role::FX];
        case Role::Perc:       return samples[(size_t) Role::Snare];
        case Role::FX:         return samples[(size_t) Role::Ride];
        case Role::Kick:
        case Role::Snare:
        default:               return {};
    }
}

int KitInfo::totalSamples() const
{
    int n = 0;
    for (const auto& s : samples)
        n += s.size();
    return n;
}

namespace KitLibrary
{

const char* roleFolderName (Role r)
{
    switch (r)
    {
        case Role::Kick:       return "Kick";
        case Role::Snare:      return "Snare";
        case Role::GhostSnare: return "Ghost";
        case Role::ClosedHat:  return "ClosedHat";
        case Role::OpenHat:    return "OpenHat";
        case Role::Ride:       return "Ride";
        case Role::Perc:       return "Perc";
        case Role::FX:         return "FX";
    }

    return "";
}

namespace
{
    /** Splits "Closed Hat_02-tight" into lower-case alphabetic tokens: closed, hat, tight. */
    juce::StringArray tokens (const juce::String& text)
    {
        juce::StringArray out;
        juce::String current;

        const auto flush = [&] {
            if (current.isNotEmpty())
                out.add (current);
            current.clear();
        };

        for (auto c : text.toLowerCase())
        {
            if (juce::CharacterFunctions::isLetter (c))
                current += c;
            else
                flush();
        }

        flush();
        return out;
    }

    bool matchRole (const juce::StringArray& t, const juce::String& joined, Role& role)
    {
        const auto has = [&] (std::initializer_list<const char*> words) {
            for (auto* w : words)
                if (t.contains (w))
                    return true;
            return false;
        };
        const auto contains = [&] (std::initializer_list<const char*> words) {
            for (auto* w : words)
                if (joined.contains (w))
                    return true;
            return false;
        };

        // Most specific first: "open hat" must not become a closed hat, "ghost snare" not a snare.
        if (contains ({ "ghost" }))                                            { role = Role::GhostSnare; return true; }
        if (contains ({ "openhat", "openhh", "ohat" }) || has ({ "oh", "ohh", "open" })) { role = Role::OpenHat; return true; }
        if (contains ({ "reverse", "crash", "riser", "sweep", "impact", "uplifter", "downlifter", "noise" }) || has ({ "fx", "sfx", "rev" }))
                                                                               { role = Role::FX; return true; }
        if (contains ({ "ride", "cymbal", "bell" }))                           { role = Role::Ride; return true; }
        if (contains ({ "kick", "bassdrum" }) || has ({ "bd", "kik", "kck" })) { role = Role::Kick; return true; }
        if (contains ({ "snare", "snr" }) || has ({ "sd", "sn" }))             { role = Role::Snare; return true; }
        if (contains ({ "hihat", "hat", "closedhat" }) || has ({ "hh", "ch", "chh", "closed", "hats" }))
                                                                               { role = Role::ClosedHat; return true; }
        if (contains ({ "perc", "rim", "clap", "shaker", "tom", "conga", "bongo", "tamb", "cowbell", "clave", "snap", "click", "block" }))
                                                                               { role = Role::Perc; return true; }
        return false;
    }
}

bool classify (const juce::File& file, const juce::File& kitRoot, Role& role)
{
    // The file name is the strongest hint, then the nearest folder, then the ones above it.
    juce::Array<juce::String> names { file.getFileNameWithoutExtension() };

    for (auto dir = file.getParentDirectory(); dir != kitRoot && dir.isAChildOf (kitRoot); dir = dir.getParentDirectory())
        names.add (dir.getFileName());

    for (const auto& n : names)
    {
        const auto t = tokens (n);
        if (matchRole (t, t.joinIntoString (""), role))
            return true;
    }

    return false;
}

KitInfo scanFolder (const juce::File& folder)
{
    KitInfo kit;
    kit.name = folder.getFileName();
    kit.folder = folder;

    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, supportedAudioWildcard(), juce::File::findFiles))
    {
        Role role;

        if (classify (entry.getFile(), folder, role))
            kit.samples[(size_t) role].add (SampleRef::fromFile (entry.getFile()));
    }

    for (auto& list : kit.samples)
        std::sort (list.begin(), list.end(), [] (const SampleRef& a, const SampleRef& b) {
            return a.file.getFullPathName().compareNatural (b.file.getFullPathName()) < 0;
        });

    return kit;
}

std::vector<KitInfo> scan (const juce::File& kitsFolder)
{
    std::vector<KitInfo> kits;

    for (const auto& name : BuiltInKits::kitNames())
    {
        KitInfo kit;
        kit.name = name;
        kit.builtIn = true;

        for (int r = 0; r < kNumRoles; ++r)
            for (const auto& id : BuiltInKits::soundsFor (name, (Role) r))
                kit.samples[(size_t) r].add (SampleRef::builtIn (id));

        kits.push_back (std::move (kit));
    }

    if (kitsFolder.isDirectory())
    {
        auto folders = kitsFolder.findChildFiles (juce::File::findDirectories, false);
        folders.sort();

        for (const auto& f : folders)
            if (auto kit = scanFolder (f); kit.totalSamples() > 0)
                kits.push_back (std::move (kit));
    }

    return kits;
}

} // namespace KitLibrary
} // namespace bf
