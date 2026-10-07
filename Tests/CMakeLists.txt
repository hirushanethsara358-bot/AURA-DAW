# AURA Tests — Google Test unit tests for all core modules.
add_executable(aura_tests
  test_dsp.cpp
  test_mixer.cpp
  test_midi.cpp
  test_engine.cpp
  test_project.cpp
  test_plugin.cpp
  test_synth.cpp
  test_sampler.cpp
  test_analyzer.cpp
  test_assistant.cpp
)

target_link_libraries(aura_tests
  PRIVATE
    AURA::Engine
    AURA::AI
    AURA::Instruments
    AURA::Project
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(aura_tests)
