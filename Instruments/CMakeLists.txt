# AURA Instruments — AURA Synth + AURA Sampler.
add_library(aura_instruments STATIC
  AuraSynth/src/Synth.cpp
  AuraSampler/src/Sampler.cpp
)

add_library(AURA::Instruments ALIAS aura_instruments)

target_include_directories(aura_instruments
  PUBLIC
    AuraSynth/include
    AuraSampler/include
)

target_link_libraries(aura_instruments PUBLIC AURA::Engine)
set_target_properties(aura_instruments PROPERTIES OUTPUT_NAME aura_instruments)
