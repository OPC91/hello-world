#pragma once

namespace exnilo
{
    /** A row of mutually exclusive pills (e.g. 8 / 16 / 32 bars). The selected one is filled with the theme gradient. */
    class Segmented : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Segmented (juce::StringArray labels = {});

        void setLabels (juce::StringArray newLabels);
        void setSelected (int index, juce::NotificationType = juce::sendNotificationSync);
        int getSelected() const noexcept { return selected; }
        std::function<void (int)> onChange;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        int indexAt (juce::Point<float>) const;
        juce::StringArray labels;
        int selected = 0, hovered = -1;
    };

    /** Keeps a Segmented in sync with a choice (or bool) parameter, with undo-friendly gestures. */
    class SegmentedAttachment
    {
    public:
        SegmentedAttachment (juce::RangedAudioParameter&, Segmented&, juce::UndoManager* = nullptr);
        ~SegmentedAttachment();

    private:
        Segmented& control;
        juce::ParameterAttachment attachment;
    };

    /** Stereo output meter (two bars, green-to-red, with peak hold), like Nil2Max's OUT meter. */
    class OutMeter : public juce::Component
    {
    public:
        /** Linear peak levels since the last call; call from a UI timer. */
        void push (float left, float right, float dt);
        void paint (juce::Graphics&) override;

    private:
        std::array<float, 2> level {}, peak {}, peakHold {};
    };

    /** The product wordmark: parts in ink, accent or gradient, then "by EXNILO". Clickable (licence menu). */
    class Wordmark : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        enum class Ink { plain, accentA, gradient };
        struct Part { juce::String text; Ink ink; };

        explicit Wordmark (std::vector<Part> parts);
        std::function<void()> onClick;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

    private:
        std::vector<Part> parts;
    };

    /** A card panel with an optional small caps title ("OPTIONS"). Children are laid out by the owner. */
    class Card : public juce::Component
    {
    public:
        explicit Card (juce::String titleText = {}) : title (std::move (titleText)) {}
        void paint (juce::Graphics&) override;
        juce::String title;
    };

    /** Small caps label used inside cards. */
    void drawCaption (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                      juce::Justification = juce::Justification::centredLeft, juce::Colour = palette::dim);
}
