# AURA Project — .aura format + autosave.
add_library(aura_project STATIC
  AuraProject/src/Project.cpp
)

add_library(AURA::Project ALIAS aura_project)

target_include_directories(aura_project
  PUBLIC
    AuraProject/include
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(aura_project PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(aura_project PROPERTIES OUTPUT_NAME aura_project)
