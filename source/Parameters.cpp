#include "Parameters.h"

namespace benchmark
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using juce::AudioParameterBool;
        using juce::AudioParameterBoolAttributes;
        using juce::ParameterID;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamIDs::bypass, parameterVersion }, "Bypass", false));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamIDs::listenRef, parameterVersion }, "Mix / Reference", false,
            AudioParameterBoolAttributes().withStringFromValueFunction (
                [] (bool isReference, int) { return juce::String (isReference ? "Reference" : "Mix"); })));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamIDs::loudnessMatch, parameterVersion }, "Loudness Match", true));

        return layout;
    }
}
