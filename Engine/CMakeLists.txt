# AURA Engine — audio, mixer, DSP, MIDI, transport, plugin host.
add_library(aura_engine STATIC
  AudioEngine/src/AudioEngine.cpp
  AudioEngine/src/Transport.cpp
  MixerEngine/src/Mixer.cpp
  DSP/src/DSP.cpp
  Midi/src/Midi.cpp
  PluginHost/src/PluginHost.cpp
)

add_library(AURA::Engine ALIAS aura_engine)

target_include_directories(aura_engine
  PUBLIC
    AudioEngine/include
    MixerEngine/include
    DSP/include
    Midi/include
    PluginHost/include
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(aura_engine PUBLIC nlohmann_json::nlohmann_json)

if(WIN32)
  target_link_libraries(aura_engine PRIVATE winmm)
endif()

set_target_properties(aura_engine PROPERTIES OUTPUT_NAME aura_engine)
