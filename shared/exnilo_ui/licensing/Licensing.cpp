namespace exnilo
{
    Licensing::Licensing (LicenceConfig c) : config (std::move (c))
    {
        load();
    }

    Licensing::~Licensing()
    {
        pool.removeAllJobs (true, 5000);
    }

    LicenceState Licensing::getState() const
    {
        const juce::ScopedLock sl (lock);
        return state;
    }

    juce::File Licensing::getLicenceFile() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Exnilo").getChildFile (config.product).getChildFile ("licence.xml");
    }

    juce::String Licensing::signatureFor (const LicenceState& s, const juce::String& product)
    {
        const auto text = product + "|" + s.key.trim() + "|" + s.instanceId + "|" + juce::SystemStats::getUniqueDeviceID()
                          + "|exnilo-from-nothing-to-too-much";
        return juce::SHA256 (text.toUTF8()).toHexString();
    }

    void Licensing::load()
    {
        LicenceState loaded;
        if (const auto xml = juce::XmlDocument::parse (getLicenceFile()))
        {
            loaded.key = xml->getStringAttribute ("key");
            loaded.instanceId = xml->getStringAttribute ("instance");
            loaded.instanceName = xml->getStringAttribute ("computer");
            loaded.customerName = xml->getStringAttribute ("customer");
            loaded.licensed = loaded.key.isNotEmpty() && loaded.instanceId.isNotEmpty()
                              && xml->getStringAttribute ("signature") == signatureFor (loaded, config.product);
        }
        const juce::ScopedLock sl (lock);
        state = loaded;
        licensed.store (state.licensed);
    }

    bool Licensing::save (const LicenceState& s) const
    {
        const auto file = getLicenceFile();
        if (! s.licensed)
            return ! file.exists() || file.deleteFile();
        file.getParentDirectory().createDirectory();
        juce::XmlElement xml ("ExniloLicence");
        xml.setAttribute ("product", config.product);
        xml.setAttribute ("key", s.key);
        xml.setAttribute ("instance", s.instanceId);
        xml.setAttribute ("computer", s.instanceName);
        xml.setAttribute ("customer", s.customerName);
        xml.setAttribute ("signature", signatureFor (s, config.product));
        return xml.writeTo (file);
    }

    void Licensing::setStateForTesting (const LicenceState& s)
    {
        const juce::ScopedLock sl (lock);
        state = s;
        licensed.store (s.licensed);
    }

    //==============================================================================
    juce::Result Licensing::parseActivation (const juce::var& json, const LicenceConfig& config, LicenceState& out)
    {
        if (! json.isObject())
            return juce::Result::fail ("The licence server sent an unexpected reply. Please try again.");

        const auto error = json.getProperty ("error", {}).toString();
        const bool activated = json.getProperty ("activated", false);
        if (! activated)
        {
            if (error.containsIgnoreCase ("activation limit"))
                return juce::Result::fail ("This licence is already active on the maximum number of computers ("
                                           + juce::String (config.maxComputers) + "). Deactivate one of them first.");
            if (error.isNotEmpty())
                return juce::Result::fail (error.containsIgnoreCase ("not found") || error.containsIgnoreCase ("invalid")
                                               ? juce::String ("That licence key wasn't recognised.")
                                               : error);
            return juce::Result::fail ("That licence key wasn't recognised.");
        }

        const auto meta = json.getProperty ("meta", {});
        const auto productId = meta.getProperty ("product_id", {}).toString();
        const auto storeId = meta.getProperty ("store_id", {}).toString();
        if (productId != config.productId || storeId != config.storeId)
            return juce::Result::fail ("That key is for a different product. Please use your " + config.product + " licence key.");

        const auto licenceKey = json.getProperty ("license_key", {});
        const auto status = licenceKey.getProperty ("status", {}).toString();
        if (status == "expired" || status == "disabled")
            return juce::Result::fail ("This licence is " + status + ". Please contact support.");

        const auto instance = json.getProperty ("instance", {});
        out.instanceId = instance.getProperty ("id", {}).toString();
        out.instanceName = instance.getProperty ("name", {}).toString();
        out.key = licenceKey.getProperty ("key", out.key).toString();
        out.customerName = meta.getProperty ("customer_name", {}).toString();
        out.licensed = out.instanceId.isNotEmpty();
        return out.licensed ? juce::Result::ok() : juce::Result::fail ("The licence server did not return an activation.");
    }

    juce::Result Licensing::parseDeactivation (const juce::var& json)
    {
        if (json.isObject() && static_cast<bool> (json.getProperty ("deactivated", false)))
            return juce::Result::ok();
        const auto error = json.getProperty ("error", {}).toString();
        return juce::Result::fail (error.isNotEmpty() ? error : juce::String ("Could not deactivate. Please try again."));
    }

    juce::var Licensing::post (const juce::String& endpoint, const juce::StringPairArray& fields, juce::String& networkError)
    {
        auto url = juce::URL ("https://api.lemonsqueezy.com/v1/licenses/" + endpoint);
        for (const auto& name : fields.getAllKeys())
            url = url.withParameter (name, fields[name]);

        int status = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders ("Accept: application/json")
                           .withConnectionTimeoutMs (15000)
                           .withStatusCode (&status);
        const auto stream = url.createInputStream (options);
        if (stream == nullptr)
        {
            networkError = "Could not reach the licence server. Please check your internet connection.";
            return {};
        }
        return juce::JSON::parse (stream->readEntireStreamAsString());
    }

    void Licensing::activate (const juce::String& rawKey, Callback onDone)
    {
        const auto key = rawKey.trim();
        if (key.isEmpty())
        {
            onDone (juce::Result::fail ("Please paste your licence key."));
            return;
        }
        if (! isConfigured())
        {
            onDone (juce::Result::fail ("Licensing is not set up in this build."));
            return;
        }

        pool.addJob ([this, key, onDone, weak = juce::WeakReference<Licensing> (this)]
        {
            juce::StringPairArray fields;
            fields.set ("license_key", key);
            fields.set ("instance_name", juce::SystemStats::getComputerName());
            juce::String networkError;
            const auto reply = post ("activate", fields, networkError);

            LicenceState next;
            next.key = key;
            auto result = networkError.isNotEmpty() ? juce::Result::fail (networkError) : parseActivation (reply, config, next);
            if (result.wasOk())
            {
                if (! save (next))
                    result = juce::Result::fail ("Activated, but could not save the licence file: " + getLicenceFile().getFullPathName());
                const juce::ScopedLock sl (lock);
                state = next;
                licensed.store (next.licensed);
            }
            juce::MessageManager::callAsync ([weak, onDone, result] { if (weak != nullptr && onDone) onDone (result); });
        });
    }

    void Licensing::deactivate (Callback onDone)
    {
        const auto current = getState();
        if (! current.licensed)
        {
            onDone (juce::Result::ok());
            return;
        }

        pool.addJob ([this, current, onDone, weak = juce::WeakReference<Licensing> (this)]
        {
            juce::StringPairArray fields;
            fields.set ("license_key", current.key);
            fields.set ("instance_id", current.instanceId);
            juce::String networkError;
            const auto reply = post ("deactivate", fields, networkError);
            const auto result = networkError.isNotEmpty() ? juce::Result::fail (networkError) : parseDeactivation (reply);
            if (result.wasOk())
            {
                save ({});
                const juce::ScopedLock sl (lock);
                state = {};
                licensed.store (false);
            }
            juce::MessageManager::callAsync ([weak, onDone, result] { if (weak != nullptr && onDone) onDone (result); });
        });
    }
}
