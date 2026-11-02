/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 exnilo_ui
  vendor:             Exnilo
  version:            1.0.0
  name:               Exnilo shared UI
  description:        The Exnilo family look (theme, LookAndFeel, hero knob, energy display, widgets),
                      plus presets and licensing shared by every Exnilo plugin.
  dependencies:       juce_audio_processors juce_audio_utils juce_gui_extra juce_cryptography
  minimumCppStandard: 20

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_cryptography/juce_cryptography.h>

#include <map>
#include <vector>

#include "theme/ExniloTheme.h"
#include "theme/ExniloLookAndFeel.h"
#include "widgets/HeroKnob.h"
#include "widgets/EnergyDisplay.h"
#include "widgets/Widgets.h"
#include "widgets/PresetBar.h"
#include "widgets/AdvancedPanel.h"
#include "editor/ScaledEditor.h"
#include "presets/PresetStore.h"
#include "licensing/Licensing.h"
