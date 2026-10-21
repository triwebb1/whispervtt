cmake_minimum_required(VERSION 3.28)
project(WhisperVTT VERSION 1.0.0
        DESCRIPTION "Offline push-to-talk voice-to-text for Windows"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ──────────────────────────────────────────────────────────
option(WHISPER_VTT_VULKAN      "GPU acceleration via Vulkan (any vendor)"       ON)
option(WHISPER_VTT_DEBUG       "Write a debug log to %TEMP%/whisper_vtt_debug.log" OFF)
option(WHISPER_VTT_BUILD_TESTS "Build the unit tests"                           ON)

# The exe, whisper/ggml DLLs and ggml's backends share one folder, so a
# build runs in place and ggml finds its backends next to the exe.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── whisper.cpp ──────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG        v1.9.5   # pinned for reproducible builds
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL        # keep its headers/libs out of our install
)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
# Backends are DLLs that ggml loads at runtime: the CPU backend in one
# variant per instruction set (the best one for the user's CPU is
# picked), and Vulkan, which loads only where a GPU driver provides
# vulkan-1.dll.  The build machine's CPU and GPU never matter.
set(BUILD_SHARED_LIBS      ON  CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL        ON  CACHE BOOL "" FORCE)
set(GGML_CPU_ALL_VARIANTS  ON  CACHE BOOL "" FORCE)
set(GGML_NATIVE            OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN ${WHISPER_VTT_VULKAN} CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(whisper)

# ── Qt 6 ─────────────────────────────────────────────────────────────
# After whisper.cpp, so automoc applies only to our own targets.
find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

# ── Core library: platform-independent, unit-tested logic ────────────
add_library(whispervtt_core STATIC
    src/audioconvert.cpp   src/audioconvert.h
    src/formatter.cpp      src/formatter.h
    src/modelcatalog.cpp   src/modelcatalog.h
    src/recordingflow.cpp  src/recordingflow.h
    src/textcleanup.cpp    src/textcleanup.h
    src/transcriptlog.cpp  src/transcriptlog.h
)
target_include_directories(whispervtt_core PUBLIC src)
target_link_libraries(whispervtt_core PUBLIC Qt6::Core)

# ── Application ──────────────────────────────────────────────────────
qt_add_executable(WhisperVTT WIN32
    src/main.cpp
    src/app.cpp            src/app.h
    src/audiorecorder.cpp  src/audiorecorder.h
    src/clipboard.cpp      src/clipboard.h
    src/debuglog.cpp       src/debuglog.h
    src/editorwindow.cpp   src/editorwindow.h
    src/hotkeymanager.cpp  src/hotkeymanager.h
    src/livetyper.cpp      src/livetyper.h
    src/logpanel.cpp       src/logpanel.h
    src/modelsetup.cpp     src/modelsetup.h
    src/tones.cpp          src/tones.h
    src/transcriber.cpp    src/transcriber.h
    resources/app.rc
    resources/vad.qrc
)
target_link_libraries(WhisperVTT PRIVATE whispervtt_core Qt6::Widgets whisper ggml)
target_compile_definitions(WhisperVTT PRIVATE
    WHISPER_VTT_VERSION="${PROJECT_VERSION}"
    WHISPER_VTT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    WHISPER_VTT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    WHISPER_VTT_VERSION_PATCH=${PROJECT_VERSION_PATCH})

foreach(target whispervtt_core WhisperVTT)
    if(MSVC)
        # Sources are UTF-8; without /utf-8 MSVC reads them in the ANSI
        # codepage and literals like "—" turn into mojibake.
        target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive-)
    endif()
    if(WIN32)
        target_compile_definitions(${target} PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    endif()
endforeach()

if(WHISPER_VTT_DEBUG)
    target_compile_definitions(WhisperVTT PRIVATE WHISPER_VTT_DEBUG)
endif()

# Keyboard hook, clipboard, SendInput, waveIn, PlaySound
target_link_libraries(WhisperVTT PRIVATE user32 winmm)

# ── Tests ────────────────────────────────────────────────────────────
if(WHISPER_VTT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install: a self-contained, portable folder ───────────────────────
# `cmake --install` lays out the exe with every DLL it needs (whisper,
# ggml and its backends, Qt, the MSVC and OpenMP runtimes); `cpack`
# zips it.
install(TARGETS WhisperVTT RUNTIME DESTINATION .)
foreach(lib whisper ggml ggml-base ${GGML_AVAILABLE_BACKENDS})
    install(FILES $<TARGET_FILE:${lib}> DESTINATION .)
endforeach()

# Skipped plugin types are the network-capable ones (TUIO touch over UDP,
# TLS, network status), so Qt6Network.dll is not deployed either.
# tools/verify-offline.ps1 checks this.
qt_generate_deploy_script(
    TARGET WhisperVTT
    OUTPUT_SCRIPT deploy_script
    CONTENT "
qt_deploy_runtime_dependencies(
    EXECUTABLE $<TARGET_FILE_NAME:WhisperVTT>
    BIN_DIR .
    PLUGINS_DIR plugins
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS --no-system-d3d-compiler --no-system-dxc-compiler
                        --no-opengl-sw
                        --skip-plugin-types generic,networkinformation,tls
)")
install(SCRIPT ${deploy_script})

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)   # ggml's CPU backend uses OpenMP
include(InstallRequiredSystemLibraries)

install(FILES LICENSE THIRD-PARTY-NOTICES.md DESTINATION .)
install(DIRECTORY LICENSES DESTINATION .)
# Portable mode: a models folder next to the exe is used instead of %APPDATA%.
install(FILES resources/models-README.txt DESTINATION models RENAME README.txt)

# `cmake --build <dir> --target installer` builds the Windows installer
# (installer/WhisperVTT.iss) from a fresh install of this build.
set(pf86 "ProgramFiles(x86)")
find_program(ISCC_EXECUTABLE ISCC
    PATHS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6" "$ENV{${pf86}}/Inno Setup 6")
if(ISCC_EXECUTABLE)
    set(stage ${CMAKE_BINARY_DIR}/installer-stage)
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${stage}
        COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${stage}
        COMMAND ${ISCC_EXECUTABLE} /Q /DAppVersion=${PROJECT_VERSION}
                /DSourceDir=${stage} /DOutputDir=${CMAKE_BINARY_DIR}
                ${CMAKE_SOURCE_DIR}/installer/WhisperVTT.iss
        VERBATIM)
    add_dependencies(installer WhisperVTT)
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "WhisperVTT-${PROJECT_VERSION}-win64-portable")
include(CPack)
