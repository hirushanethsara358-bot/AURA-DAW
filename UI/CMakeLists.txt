# AURA UI — Qt6/QML application (full builds only).
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Concurrent)

qt_add_executable(aura_daw_app
  MainWindow/src/main.cpp
  MainWindow/src/MainWindow.cpp
  MainWindow/include/Aura/MainWindow.hpp
)

qt_add_qml_module(aura_daw_app
  URI AuraUI
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/TransportBar.qml
    qml/MixerView.qml
    qml/PianoRoll.qml
    qml/Browser.qml
)

target_include_directories(aura_daw_app PRIVATE MainWindow/include)

target_link_libraries(aura_daw_app
  PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::Concurrent
    AURA::Engine
    AURA::AI
    AURA::Instruments
    AURA::Project
    AURA::Session
)

set_target_properties(aura_daw_app PROPERTIES OUTPUT_NAME "AURA DAW")

install(TARGETS aura_daw_app
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin
)
