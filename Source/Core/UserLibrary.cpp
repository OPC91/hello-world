#include "UserLibrary.h"
#include "StateIO.h"

namespace bf::UserLibrary
{

namespace
{
    juce::File& rootOverride()
    {
        static juce::File f;
        return f;
    }
}

juce::File getRoot()
{
    if (rootOverride() != juce::File())
        return rootOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("BreakForge");
}

void setRootOverride (const juce::File& f) { rootOverride() = f; }

juce::File getGroovesFolder() { return getRoot().getChildFile ("Grooves"); }
juce::File getKitsFolder()    { return getRoot().getChildFile ("Kits"); }

juce::Array<juce::File> listGrooves()
{
    auto files = getGroovesFolder().findChildFiles (juce::File::findFiles, false, "*.bfgroove");
    files.sort();
    return files;
}

bool saveGroove (const juce::String& name, const GrooveTemplate& g)
{
    const auto safe = juce::File::createLegalFileName (name.trim());

    if (safe.isEmpty() || ! getGroovesFolder().createDirectory())
        return false;

    auto tree = StateIO::grooveToTree (g);
    tree.setProperty ("name", name.trim(), nullptr);
    return getGroovesFolder().getChildFile (safe + ".bfgroove").replaceWithText (tree.toXmlString());
}

bool loadGroove (const juce::File& file, GrooveTemplate& out)
{
    const auto tree = juce::ValueTree::fromXml (file.loadFileAsString());

    if (! tree.isValid())
        return false;

    out = StateIO::grooveFromTree (tree);
    return true;
}

} // namespace bf::UserLibrary
