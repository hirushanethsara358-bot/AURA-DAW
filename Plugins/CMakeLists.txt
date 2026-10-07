# AURA Plugins — VST3 host integration.
add_library(aura_plugins STATIC
  VST3Host/src/Vst3Plugin.cpp
)

add_library(AURA::Plugins ALIAS aura_plugins)

target_include_directories(aura_plugins
  PUBLIC
    VST3Host/include
)

target_link_libraries(aura_plugins PUBLIC AURA::Engine)
set_target_properties(aura_plugins PROPERTIES OUTPUT_NAME aura_plugins)
