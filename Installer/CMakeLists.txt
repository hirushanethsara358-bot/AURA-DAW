# AURA Installer — experimental Windows NSIS setup via CPack.
# This is packaging configuration only; clean-machine install and launch tests
# are still required before a distributable setup can be claimed.
include(InstallRequiredSystemLibraries)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/Samples/"
  DESTINATION "Factory Library"
  FILES_MATCHING PATTERN "*.md" EXCLUDE
)
install(FILES "${CMAKE_SOURCE_DIR}/README.md"
  DESTINATION "."
  RENAME "AURA-DAW-Readme.txt"
)

set(CPACK_PACKAGE_NAME "AURA DAW")
set(CPACK_PACKAGE_VENDOR "AURA")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AURA Digital Audio Workstation")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "AURA DAW")
set(CPACK_PACKAGE_FILE_NAME "AURA-DAW-Setup")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

set(CPACK_GENERATOR "NSIS")
set(CPACK_NSIS_DISPLAY_NAME "AURA Digital Audio Workstation")
set(CPACK_NSIS_PACKAGE_NAME "AURA DAW ${PROJECT_VERSION}")
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/aura.ico")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/aura.ico")
else()
  message(STATUS "No custom NSIS icon found; using the NSIS default.")
endif()
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
  WriteRegStr HKCU 'Software\\\\AURA\\\\DAW' 'Version' '${PROJECT_VERSION}'
")

# Code signing is not implemented. Do not imply that setting this legacy
# variable signs the application or the generated installer.
if(DEFINED AURA_SIGN_TOOL AND NOT "${AURA_SIGN_TOOL}" STREQUAL "")
  message(WARNING "AURA_SIGN_TOOL is not wired to CPack; the package will remain unsigned.")
endif()

include(CPack)
