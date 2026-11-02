#pragma once

namespace exnilo
{
    /** A preset: values for a product's stored parameters (plain values, by parameter ID). */
    struct Preset
    {
        juce::String name;
        juce::String category;               ///< factory group (e.g. a style), or "User"
        std::map<juce::String, float> values;
        bool isFactory = true;
        juce::File file;                     ///< user presets only
    };

    /** Factory presets plus user presets saved as XML in %APPDATA%\Exnilo\<Product>\Presets (on Windows; the
        platform's user application data folder elsewhere). Presets are internal, not host programs, so hosts never
        swap state behind the plugin's back. Message thread only. */
    class PresetStore
    {
    public:
        PresetStore (juce::String productName, juce::StringArray storedParameterIds, std::vector<Preset> factoryPresets);

        /** Factory presets first (in the order given), then user presets (alphabetical). Rescans the user folder. */
        void rescan();
        const std::vector<Preset>& getAll() const noexcept { return presets; }
        int indexOf (const juce::String& name) const;

        juce::File getUserFolder() const;

        /** Sets every stored parameter to the preset's value (missing ones to their defaults), as host gestures. */
        void apply (const Preset&, juce::AudioProcessorValueTreeState&) const;
        /** True if the current parameter values equal the preset's (the preset is unmodified). */
        bool matches (const Preset&, juce::AudioProcessorValueTreeState&) const;
        Preset capture (const juce::String& name, juce::AudioProcessorValueTreeState&) const;

        juce::Result saveUser (const juce::String& name, juce::AudioProcessorValueTreeState&);
        bool deleteUser (const Preset&);

        static juce::String sanitiseName (const juce::String&);

    private:
        juce::String product;
        juce::StringArray ids;
        std::vector<Preset> factory, presets;
    };
}
