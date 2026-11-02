#pragma once

#include "Parameters.h"
#include "engine/SeamEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace seam
{
    /** UI-visible state, written by the audio thread and read by the editor's timer. Lock-free. */
    struct UiState
    {
        std::atomic<bool> playing { false };
        std::atomic<double> barsToSeam { 0.0 };
        std::atomic<int> barInPhrase { 0 };
        std::atomic<int> phraseBars { 16 };
        std::atomic<float> beatPhase { -1.0f };
        std::atomic<float> effective { 0.0f };
        std::atomic<bool> fillActive { false };
        std::atomic<double> barsPerBeat { 0.25 };
        std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };   ///< max since the editor last read them

        void takePeaks (float& left, float& right) noexcept
        {
            left = peakLeft.exchange (0.0f);
            right = peakRight.exchange (0.0f);
        }
    };

    class SeamProcessor final : public juce::AudioProcessor
    {
    public:
        SeamProcessor();
        ~SeamProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using juce::AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        // Presets are internal (not host programs), so hosts never restore per-program state behind our back.
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
        UiState& getUiState() noexcept { return ui; }

        /** Editor-only settings kept in the session (theme, scale). */
        juce::String getThemeId() const;
        void setThemeId (const juce::String&);
        float getEditorScale() const;
        void setEditorScale (float);

    private:
        engine::Settings readSettings() const noexcept;

        juce::AudioProcessorValueTreeState state;
        engine::SeamEngine engine;
        UiState ui;

        struct ParamPointers
        {
            std::atomic<float>* transition = nullptr; std::atomic<float>* style = nullptr; std::atomic<float>* complexity = nullptr;
            std::atomic<float>* fillLength = nullptr; std::atomic<float>* fillMode = nullptr; std::atomic<float>* vacuum = nullptr;
            std::atomic<float>* vacuumSize = nullptr; std::atomic<float>* tapeStop = nullptr; std::atomic<float>* autoMode = nullptr;
            std::atomic<float>* autoBars = nullptr; std::atomic<float>* autoCurve = nullptr; std::atomic<float>* phrase = nullptr;
            std::array<std::atomic<float>*, 5> amounts {};
        } p;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeamProcessor)
    };
}
