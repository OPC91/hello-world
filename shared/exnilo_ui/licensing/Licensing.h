#pragma once

namespace exnilo
{
    /** How a product is sold (Lemon Squeezy). */
    struct LicenceConfig
    {
        juce::String product;          ///< e.g. "SEAM" (also the %APPDATA%\Exnilo\<product> folder)
        juce::String storeId;          ///< Lemon Squeezy store id ("" = not configured in this build)
        juce::String productId;        ///< Lemon Squeezy product id: a key for another product is refused
        juce::String buyUrl;
        int maxComputers = 2;
    };

    /** The stored activation. */
    struct LicenceState
    {
        bool licensed = false;
        juce::String key, instanceId, instanceName, customerName;
    };

    /** Licensing shared by Exnilo plugins: a Lemon Squeezy licence key, activated once online per computer (2
        computers per licence), then works offline. Deactivating frees the slot for another computer.

        One object per product per process (hold it in a juce::SharedResourcePointer), so activating in one plugin
        instance unlocks every instance. Network calls run on a background thread; callbacks arrive on the message
        thread. The activation is stored in %APPDATA%\Exnilo\<Product>\licence.xml, bound to this computer by a hash
        (a speed bump, not DRM: any purely offline check can be patched out). */
    class Licensing
    {
    public:
        explicit Licensing (LicenceConfig);
        virtual ~Licensing();

        bool isLicensed() const noexcept { return licensed.load(); }
        LicenceState getState() const;
        const LicenceConfig& getConfig() const noexcept { return config; }
        bool isConfigured() const noexcept { return config.storeId.isNotEmpty() && config.productId.isNotEmpty(); }

        using Callback = std::function<void (juce::Result)>;
        void activate (const juce::String& key, Callback onDone);
        void deactivate (Callback onDone);

        /** Interprets Lemon Squeezy's activate / deactivate replies (exposed for tests). */
        static juce::Result parseActivation (const juce::var& json, const LicenceConfig&, LicenceState& out);
        static juce::Result parseDeactivation (const juce::var& json);

        juce::File getLicenceFile() const;

        /** For tests: install a state without the network. */
        void setStateForTesting (const LicenceState&);

    private:
        void load();
        bool save (const LicenceState&) const;
        static juce::String signatureFor (const LicenceState&, const juce::String& product);
        static juce::var post (const juce::String& endpoint, const juce::StringPairArray& fields, juce::String& networkError);

        LicenceConfig config;
        mutable juce::CriticalSection lock;
        LicenceState state;
        std::atomic<bool> licensed { false };
        juce::ThreadPool pool { juce::ThreadPoolOptions().withNumberOfThreads (1).withThreadName ("Exnilo licensing") };

        JUCE_DECLARE_WEAK_REFERENCEABLE (Licensing)
    };
}
