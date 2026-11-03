#include "Parameters.h"

namespace seam
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        const auto percent = AudioParameterFloatAttributes()
                                 .withLabel ("%")
                                 .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })
                                 .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("0123456789.-").getFloatValue(); });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::transition, 1 }, "Transition",
                                                           NormalisableRange<float> (0.0f, 100.0f, 0.0f), 0.0f, percent));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::style, 1 }, "Style", choices::style, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::complexity, 1 }, "Complexity",
                                                           NormalisableRange<float> (0.0f, 100.0f, 0.0f), 50.0f, percent));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::fillLength, 1 }, "Fill length", choices::fillLength, 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::fillMode, 1 }, "Fill mode", choices::fillMode, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::vacuum, 1 }, "Vacuum", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::vacuumSize, 1 }, "Vacuum gap", choices::vacuumSize, 1));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::tapeStop, 1 }, "Tape stop", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::autoMode, 1 }, "Auto mode", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::autoBars, 1 }, "Auto bars", choices::autoBars, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::autoCurve, 1 }, "Auto curve", choices::autoCurve, 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::phrase, 1 }, "Phrase length", choices::phrase, 1));

        for (const auto& amount : advancedAmounts)
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { amount.id, 1 }, String (amount.name) + " amount",
                                                               NormalisableRange<float> (0.0f, 150.0f, 0.0f), 100.0f, percent));
        return layout;
    }
}
