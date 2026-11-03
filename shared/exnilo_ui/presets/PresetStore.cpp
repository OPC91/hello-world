namespace exnilo
{
    PresetStore::PresetStore (juce::String productName, juce::StringArray storedParameterIds, std::vector<Preset> factoryPresets)
        : product (std::move (productName)), ids (std::move (storedParameterIds)), factory (std::move (factoryPresets))
    {
        rescan();
    }

    juce::File PresetStore::getUserFolder() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Exnilo").getChildFile (product).getChildFile ("Presets");
    }

    juce::String PresetStore::sanitiseName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).substring (0, 64);
    }

    void PresetStore::rescan()
    {
        presets = factory;
        std::vector<Preset> user;
        for (const auto& file : getUserFolder().findChildFiles (juce::File::findFiles, false, "*.xml"))
        {
            const auto xml = juce::XmlDocument::parse (file);
            if (xml == nullptr || ! xml->hasTagName ("ExniloPreset") || xml->getStringAttribute ("product") != product)
                continue;
            Preset p;
            p.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
            p.category = "User";
            p.isFactory = false;
            p.file = file;
            for (const auto* param : xml->getChildWithTagNameIterator ("param"))
                p.values[param->getStringAttribute ("id")] = static_cast<float> (param->getDoubleAttribute ("value"));
            user.push_back (std::move (p));
        }
        std::sort (user.begin(), user.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
        presets.insert (presets.end(), user.begin(), user.end());
    }

    int PresetStore::indexOf (const juce::String& name) const
    {
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == name)
                return static_cast<int> (i);
        return -1;
    }

    void PresetStore::apply (const Preset& preset, juce::AudioProcessorValueTreeState& state) const
    {
        for (const auto& id : ids)
        {
            auto* param = state.getParameter (id);
            if (param == nullptr)
                continue;
            const auto it = preset.values.find (id);
            const float normalised = it != preset.values.end() ? param->convertTo0to1 (it->second) : param->getDefaultValue();
            param->beginChangeGesture();
            param->setValueNotifyingHost (normalised);
            param->endChangeGesture();
        }
    }

    bool PresetStore::matches (const Preset& preset, juce::AudioProcessorValueTreeState& state) const
    {
        for (const auto& id : ids)
        {
            auto* param = state.getParameter (id);
            if (param == nullptr)
                continue;
            const auto it = preset.values.find (id);
            const float expected = it != preset.values.end() ? param->convertTo0to1 (it->second) : param->getDefaultValue();
            if (std::abs (param->getValue() - expected) > 1.0e-3f)
                return false;
        }
        return true;
    }

    Preset PresetStore::capture (const juce::String& name, juce::AudioProcessorValueTreeState& state) const
    {
        Preset p;
        p.name = name;
        p.category = "User";
        p.isFactory = false;
        for (const auto& id : ids)
            if (auto* param = state.getParameter (id))
                p.values[id] = param->convertFrom0to1 (param->getValue());
        return p;
    }

    juce::Result PresetStore::saveUser (const juce::String& name, juce::AudioProcessorValueTreeState& state)
    {
        const auto clean = sanitiseName (name);
        if (clean.isEmpty())
            return juce::Result::fail ("Please give the preset a name.");
        for (const auto& f : factory)
            if (f.name.equalsIgnoreCase (clean))
                return juce::Result::fail ("\"" + clean + "\" is a factory preset name. Please choose another.");

        const auto folder = getUserFolder();
        if (! folder.createDirectory())
            return juce::Result::fail ("Could not create " + folder.getFullPathName());

        juce::XmlElement xml ("ExniloPreset");
        xml.setAttribute ("product", product);
        xml.setAttribute ("name", clean);
        xml.setAttribute ("version", 1);
        for (const auto& [id, value] : capture (clean, state).values)
        {
            auto* param = xml.createNewChildElement ("param");
            param->setAttribute ("id", id);
            param->setAttribute ("value", value);
        }
        if (! xml.writeTo (folder.getChildFile (clean + ".xml")))
            return juce::Result::fail ("Could not write the preset file.");
        rescan();
        return juce::Result::ok();
    }

    bool PresetStore::deleteUser (const Preset& preset)
    {
        if (preset.isFactory || ! preset.file.existsAsFile())
            return false;
        const bool ok = preset.file.deleteFile();
        rescan();
        return ok;
    }
}
