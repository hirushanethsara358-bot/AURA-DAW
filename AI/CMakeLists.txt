# AURA AI — Music Analyzer + Assistant.
add_library(aura_ai STATIC
  MusicAnalyzer/src/MusicAnalyzer.cpp
  AuraAssistant/src/Assistant.cpp
)

add_library(AURA::AI ALIAS aura_ai)

target_include_directories(aura_ai
  PUBLIC
    MusicAnalyzer/include
    AuraAssistant/include
)

set_target_properties(aura_ai PROPERTIES OUTPUT_NAME aura_ai)
