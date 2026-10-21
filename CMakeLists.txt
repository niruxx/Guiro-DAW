cmake_minimum_required(VERSION 3.21)
project(Guiro VERSION 0.1.0 DESCRIPTION "A Linux-first pattern-based DAW" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(GUIRO_WITH_VST3 "Build with VST3 plugin hosting" ON)
option(GUIRO_WITH_CLAP "Build with CLAP plugin hosting" ON)
option(GUIRO_WITH_LV2  "Build with LV2 plugin hosting (needs lilv)" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---------------------------------------------------------------- CLAP
if(GUIRO_WITH_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        1.2.6
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _no_cmake_)
    FetchContent_MakeAvailable(clap)
    add_library(guiro_clap INTERFACE)
    target_include_directories(guiro_clap SYSTEM INTERFACE ${clap_SOURCE_DIR}/include)
endif()

# ---------------------------------------------------------------- VST3 (MIT licensed since SDK 3.8)
if(GUIRO_WITH_VST3)
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG        v3.8.0_build_66
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES base pluginterfaces public.sdk
        SOURCE_SUBDIR  _no_cmake_)
    FetchContent_MakeAvailable(vst3sdk)
    set(V ${vst3sdk_SOURCE_DIR})
    add_library(guiro_vst3sdk STATIC
        ${V}/base/source/baseiids.cpp
        ${V}/base/source/fbuffer.cpp
        ${V}/base/source/fdebug.cpp
        ${V}/base/source/fdynlib.cpp
        ${V}/base/source/fobject.cpp
        ${V}/base/source/fstreamer.cpp
        ${V}/base/source/fstring.cpp
        ${V}/base/source/timer.cpp
        ${V}/base/source/updatehandler.cpp
        ${V}/base/thread/source/fcondition.cpp
        ${V}/base/thread/source/flock.cpp
        ${V}/pluginterfaces/base/conststringtable.cpp
        ${V}/pluginterfaces/base/coreiids.cpp
        ${V}/pluginterfaces/base/funknown.cpp
        ${V}/pluginterfaces/base/ustring.cpp
        ${V}/public.sdk/source/common/commoniids.cpp
        ${V}/public.sdk/source/common/commonstringconvert.cpp
        ${V}/public.sdk/source/common/memorystream.cpp
        ${V}/public.sdk/source/common/readfile.cpp
        ${V}/public.sdk/source/vst/hosting/connectionproxy.cpp
        ${V}/public.sdk/source/vst/hosting/eventlist.cpp
        ${V}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${V}/public.sdk/source/vst/hosting/module.cpp
        ${V}/public.sdk/source/vst/hosting/module_linux.cpp
        ${V}/public.sdk/source/vst/hosting/parameterchanges.cpp
        ${V}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
        ${V}/public.sdk/source/vst/hosting/processdata.cpp
        ${V}/public.sdk/source/vst/utility/stringconvert.cpp
        ${V}/public.sdk/source/vst/vstinitiids.cpp)
    target_include_directories(guiro_vst3sdk SYSTEM PUBLIC ${V})
    target_compile_definitions(guiro_vst3sdk PUBLIC $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
    target_compile_options(guiro_vst3sdk PRIVATE -w)
    set_target_properties(guiro_vst3sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(guiro_vst3sdk PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
endif()

# ---------------------------------------------------------------- LV2
if(GUIRO_WITH_LV2)
    pkg_check_modules(LILV IMPORTED_TARGET lilv-0)
    if(NOT LILV_FOUND)
        message(WARNING "lilv not found - LV2 hosting disabled")
        set(GUIRO_WITH_LV2 OFF)
    endif()
endif()

# ---------------------------------------------------------------- Guiro
add_executable(guiro
    resources/guiro.qrc
    src/main.cpp

    src/core/Types.h
    src/core/Param.h            src/core/Param.cpp
    src/core/Sample.h           src/core/Sample.cpp
    src/core/Project.h          src/core/Project.cpp
    src/core/ProjectIO.h        src/core/ProjectIO.cpp
    src/core/DefaultKit.h       src/core/DefaultKit.cpp

    src/engine/SpscQueue.h
    src/engine/Dsp.h
    src/engine/Snapshot.h
    src/engine/Runtime.h        src/engine/Runtime.cpp
    src/engine/AudioEngine.h    src/engine/AudioEngine.cpp
    src/engine/SnapshotBuilder.cpp
    src/engine/Backends.h       src/engine/Backends.cpp
    src/engine/Exporter.h       src/engine/Exporter.cpp

    src/plugins/PluginInstance.h
    src/plugins/PluginManager.h src/plugins/PluginManager.cpp
    src/plugins/ClapHost.h      src/plugins/ClapHost.cpp
    src/plugins/Vst2Abi.h
    src/plugins/Vst2Host.h      src/plugins/Vst2Host.cpp
    src/plugins/Vst3Host.h      src/plugins/Vst3Host.cpp
    src/plugins/Lv2Host.h       src/plugins/Lv2Host.cpp

    src/ui/Theme.h              src/ui/Theme.cpp
    src/ui/Icons.h              src/ui/Icons.cpp
    src/ui/UiHub.h              src/ui/UiHub.cpp
    src/ui/MainWindow.h         src/ui/MainWindow.cpp
    src/ui/TopBar.h             src/ui/TopBar.cpp
    src/ui/Browser.h            src/ui/Browser.cpp
    src/ui/ChannelRack.h        src/ui/ChannelRack.cpp
    src/ui/PianoRoll.h          src/ui/PianoRoll.cpp
    src/ui/Playlist.h           src/ui/Playlist.cpp
    src/ui/Mixer.h              src/ui/Mixer.cpp
    src/ui/PadsPanel.h          src/ui/PadsPanel.cpp
    src/ui/PluginWindow.h       src/ui/PluginWindow.cpp
    src/ui/ChannelSettings.h    src/ui/ChannelSettings.cpp
    src/ui/WineBridge.h         src/ui/WineBridge.cpp
    src/ui/DockTitle.h          src/ui/DockTitle.cpp
    src/ui/Workspace.h          src/ui/Workspace.cpp
    src/ui/widgets/Knob.h       src/ui/widgets/Knob.cpp
    src/ui/widgets/Fader.h      src/ui/widgets/Fader.cpp
    src/ui/widgets/Meter.h      src/ui/widgets/Meter.cpp
    src/ui/widgets/Waveform.h   src/ui/widgets/Waveform.cpp
    src/ui/widgets/Scope.h      src/ui/widgets/Scope.cpp
)

target_include_directories(guiro PRIVATE src)
target_compile_options(guiro PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(guiro PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent
    PkgConfig::JACK PkgConfig::SNDFILE PkgConfig::SAMPLERATE
    Threads::Threads ${CMAKE_DL_LIBS})

if(GUIRO_WITH_CLAP)
    target_link_libraries(guiro PRIVATE guiro_clap)
    target_compile_definitions(guiro PRIVATE GUIRO_HAVE_CLAP=1)
endif()
if(GUIRO_WITH_VST3)
    target_link_libraries(guiro PRIVATE guiro_vst3sdk)
    target_compile_definitions(guiro PRIVATE GUIRO_HAVE_VST3=1)
endif()
if(GUIRO_WITH_LV2)
    target_link_libraries(guiro PRIVATE PkgConfig::LILV)
    target_compile_definitions(guiro PRIVATE GUIRO_HAVE_LV2=1)
endif()

include(GNUInstallDirs)
install(TARGETS guiro RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/guiro.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/guiro.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
