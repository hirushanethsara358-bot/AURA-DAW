# AURA Session — project-to-audio MVP adapter.
add_library(aura_session STATIC
  src/ProjectPlaybackSession.cpp
)

add_library(AURA::Session ALIAS aura_session)

target_include_directories(aura_session
  PUBLIC
    include
)

target_link_libraries(aura_session
  PUBLIC
    AURA::Engine
    AURA::Instruments
    AURA::Project
)

set_target_properties(aura_session PROPERTIES OUTPUT_NAME aura_session)
