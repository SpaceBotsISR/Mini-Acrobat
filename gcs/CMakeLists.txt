cmake_minimum_required(VERSION 3.21)

# Apple Silicon Macs: build arm64 only (no Rosetta / universal binaries).
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
endif()
# Homebrew's Qt is built for the running macOS, so target it too (avoids
# "linking with dylib built for newer version" warnings).
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  execute_process(COMMAND sw_vers -productVersion OUTPUT_VARIABLE _macos_ver
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX MATCH "^[0-9]+" _macos_major "${_macos_ver}")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${_macos_major}.0" CACHE STRING "")
endif()

project(MAgcs VERSION 0.2.0 LANGUAGES CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(MAGCS_BUILD_TESTS "Build the unit/integration tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets Charts)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- core: no GUI. rosbridge client, heartbeat, drone facade, network --------
add_library(magcs_core STATIC
  src/core/Topics.hpp
  src/core/SafetyState.hpp
  src/core/SafetyState.cpp
  src/core/LinkStatus.hpp
  src/core/LinkStatus.cpp
  src/core/ConnectionTarget.hpp
  src/core/ConnectionTarget.cpp
  src/core/RosbridgeProtocol.hpp
  src/core/RosbridgeProtocol.cpp
  src/core/RosbridgeClient.hpp
  src/core/RosbridgeClient.cpp
  src/core/HeartbeatSender.hpp
  src/core/HeartbeatSender.cpp
  src/core/Drone.hpp
  src/core/Drone.cpp
  src/core/MessagePath.hpp
  src/core/MessagePath.cpp
  src/core/TopicHub.hpp
  src/core/TopicHub.cpp
  src/core/Tailscale.hpp
  src/core/Tailscale.cpp
  src/core/NetworkMonitor.hpp
  src/core/NetworkMonitor.cpp
  src/platform/WifiInfo.hpp
  src/platform/WifiInfo.cpp
)
if(APPLE)
  target_sources(magcs_core PRIVATE src/platform/WifiInfo_mac.mm)
  set_source_files_properties(src/platform/WifiInfo_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(magcs_core PRIVATE
    "-framework Foundation" "-framework CoreWLAN" "-framework CoreLocation")
else()
  target_sources(magcs_core PRIVATE src/platform/WifiInfo_linux.cpp)
endif()
target_include_directories(magcs_core PUBLIC src)
target_compile_definitions(magcs_core PUBLIC MAGCS_VERSION="${PROJECT_VERSION}")
target_link_libraries(magcs_core PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets)

# ---- ui: widgets ------------------------------------------------------------
add_library(magcs_ui STATIC
  src/widgets/SignalBars.hpp
  src/widgets/SignalBars.cpp
  src/widgets/Theme.hpp
  src/widgets/Theme.cpp
  src/widgets/TimePlot.hpp
  src/widgets/TimePlot.cpp
  src/widgets/MotorSlider.hpp
  src/widgets/MotorSlider.cpp
  src/widgets/DroneTopView.hpp
  src/widgets/DroneTopView.cpp
  src/widgets/LinkText.hpp
  src/widgets/LinkText.cpp
  src/widgets/MacNetworkBox.hpp
  src/widgets/MacNetworkBox.cpp
  src/widgets/ConnectDialog.hpp
  src/widgets/ConnectDialog.cpp
  src/widgets/ConnectionWidget.hpp
  src/widgets/ConnectionWidget.cpp
  src/widgets/SafetyWidget.hpp
  src/widgets/SafetyWidget.cpp
  src/widgets/MotorsWidget.hpp
  src/widgets/MotorsWidget.cpp
  src/widgets/PlotWidget.hpp
  src/widgets/PlotWidget.cpp
  src/widgets/ConsoleWidget.hpp
  src/widgets/ConsoleWidget.cpp
  src/panels/Panel.hpp
  src/panels/PlotPanel.hpp
  src/panels/PlotPanel.cpp
  src/panels/TopicsPanel.hpp
  src/panels/TopicsPanel.cpp
  src/panels/RawPanel.hpp
  src/panels/RawPanel.cpp
  src/panels/GaugePanel.hpp
  src/panels/GaugePanel.cpp
  src/panels/PublishPanel.hpp
  src/panels/PublishPanel.cpp
  src/panels/FlyPanel.hpp
  src/panels/FlyPanel.cpp
  src/MainWindow.hpp
  src/MainWindow.cpp
)
target_include_directories(magcs_ui PUBLIC src)
target_link_libraries(magcs_ui PUBLIC magcs_core Qt6::Widgets Qt6::Charts)

# ---- app --------------------------------------------------------------------
set(MAGCS_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/resources/MAgcs.icns)
set_source_files_properties(${MAGCS_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

add_executable(MAgcs src/main.cpp resources/resources.qrc ${MAGCS_ICNS})
target_link_libraries(MAgcs PRIVATE magcs_ui)
set_target_properties(MAgcs PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
  MACOSX_BUNDLE_BUNDLE_NAME "MAgcs"
  MACOSX_BUNDLE_GUI_IDENTIFIER "pt.ulisboa.tecnico.isr.magcs"
  MACOSX_BUNDLE_ICON_FILE "MAgcs.icns"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "ISR Lisboa - Mini-Acrobat"
)

# ---- install -----------------------------------------------------------------
# macOS: the self-contained bundle (Qt inside, signed) is made by
# scripts/build_app.sh, which handles Homebrew's split Qt kegs; this only
# installs the plain bundle.
if(APPLE)
  install(TARGETS MAgcs BUNDLE DESTINATION .)
else()
  include(GNUInstallDirs)
  install(TARGETS MAgcs RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---- tests ------------------------------------------------------------------
if(MAGCS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
