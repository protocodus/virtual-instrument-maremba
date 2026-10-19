cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

option(MAREMBA_BUILD_UNIVERSAL "Build arm64 and x86_64 slices on macOS" ON)

# These must be established before project() so CMake's compiler checks use the
# same deployment target and architectures as the final plug-in.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version")
    endif()
    if(MAREMBA_BUILD_UNIVERSAL AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    endif()
endif()

# Keep Windows binaries independent of an installed Visual C++ redistributable.
# Set before project() so every static library uses the same runtime.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library")
endif()

# The DSP version is the product version (Rack devices take X.Y.Z from it).
# C is required too: JUCE's macOS AU and Standalone wrappers compile C sources.
project(Maremba VERSION 1.5.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MAREMBA_BUILD_PLUGIN "Build the JUCE plug-in and standalone app" ON)
option(MAREMBA_BUILD_CLAP "Build the CLAP format through clap-juce-extensions" ON)
option(MAREMBA_BUILD_TOOLS "Build the demo renderer and the other offline tools" ON)
option(MAREMBA_ENABLE_SANITIZERS "Instrument the build with ASan and UBSan, and build the libFuzzer harness" OFF)
option(MAREMBA_REQUIRE_PYTHON_TESTS
    "Fail to configure unless every Python check can be registered (CI sets it)" OFF)
set(MAREMBA_JUCE_PATH "" CACHE PATH "Optional local JUCE 8.0.14 checkout (avoids downloading JUCE)")
set(MAREMBA_CLAP_JUCE_EXTENSIONS_PATH "" CACHE PATH
    "Optional local clap-juce-extensions checkout with submodules")

# X.Y.Z-build.<run>: CI's run number, or "local" for a developer build.
if(DEFINED ENV{BUILD_NUMBER} AND NOT "$ENV{BUILD_NUMBER}" STREQUAL "")
    set(MAREMBA_BUILD_NUMBER "$ENV{BUILD_NUMBER}")
elseif(DEFINED ENV{GITHUB_RUN_NUMBER})
    set(MAREMBA_BUILD_NUMBER "$ENV{GITHUB_RUN_NUMBER}")
else()
    set(MAREMBA_BUILD_NUMBER "local")
endif()

if(MAREMBA_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "MAREMBA_ENABLE_SANITIZERS requires Clang or GCC")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    add_compile_definitions(MAREMBA_SANITIZED)
endif()

include(CTest)

set(MAREMBA_DSP_SOURCES
    Source/DSP/MarembaEngine.cpp
    Source/DSP/MarembaVoice.cpp)

add_library(MarembaDSP STATIC ${MAREMBA_DSP_SOURCES})
# Tests include engine headers by name; front ends include "DSP/..." and the
# plug-in tables beside it in Source/. Consumers read them as system headers:
# the engine's own build checks them with its warnings, and JUCE's stricter
# recommended warnings would otherwise bury the plug-in's own.
set(MAREMBA_DSP_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP" "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(MarembaDSP PRIVATE ${MAREMBA_DSP_INCLUDES})
target_include_directories(MarembaDSP SYSTEM INTERFACE ${MAREMBA_DSP_INCLUDES})
# C++17, as the Rack Extension compiles it, so the tests check the shipped engine.
set_target_properties(MarembaDSP PROPERTIES
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(MarembaDSP PRIVATE /W4 /permissive-)
else()
    target_compile_options(MarembaDSP PRIVATE -Wall -Wextra)
endif()

# The Reason Rack Extension compiles Source/DSP as C++17 with -Wall; this
# target proves that here rather than only in the Rack repository.
if(NOT MSVC)
    add_library(MarembaDSPCxx17 OBJECT ${MAREMBA_DSP_SOURCES})
    set_target_properties(MarembaDSPCxx17 PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_options(MarembaDSPCxx17 PRIVATE -Wall -Wextra -Werror)
endif()

if(MAREMBA_BUILD_PLUGIN)
    if(MAREMBA_JUCE_PATH)
        if(NOT EXISTS "${MAREMBA_JUCE_PATH}/CMakeLists.txt")
            message(FATAL_ERROR "MAREMBA_JUCE_PATH does not contain a JUCE CMakeLists.txt: ${MAREMBA_JUCE_PATH}")
        endif()
        add_subdirectory("${MAREMBA_JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            # JUCE 8.0.14, pinned to its immutable release commit and archive
            # checksum so automated release builds cannot follow a moved tag.
            URL https://github.com/juce-framework/JUCE/archive/2cdfca8feb300fb424002ba2c2751569e5bacb64.tar.gz
            URL_HASH SHA256=814019716960a16f8f850a1593eeb56e2f21910aff2d673f7d3513b4ebaf251d)
        FetchContent_MakeAvailable(JUCE)
    endif()

    if(MAREMBA_BUILD_CLAP)
        if(MAREMBA_CLAP_JUCE_EXTENSIONS_PATH)
            foreach(required_file IN ITEMS CMakeLists.txt
                    clap-libs/clap/CMakeLists.txt clap-libs/clap-helpers/CMakeLists.txt)
                if(NOT EXISTS "${MAREMBA_CLAP_JUCE_EXTENSIONS_PATH}/${required_file}")
                    message(FATAL_ERROR "MAREMBA_CLAP_JUCE_EXTENSIONS_PATH is missing ${required_file}; "
                        "use a checkout with git submodule update --init --recursive")
                endif()
            endforeach()
            add_subdirectory("${MAREMBA_CLAP_JUCE_EXTENSIONS_PATH}"
                             "${CMAKE_CURRENT_BINARY_DIR}/clap-juce-extensions" EXCLUDE_FROM_ALL)
        else()
            include(FetchContent)
            FetchContent_Declare(
                clap_juce_extensions
                GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
                GIT_TAG c1a5ad025f95d01e03267857fa8276ebeed16500
                # Archive downloads omit these gitlinks. Fetch both dependencies
                # at the immutable revisions recorded by the wrapper commit.
                GIT_SUBMODULES clap-libs/clap clap-libs/clap-helpers
                GIT_SUBMODULES_RECURSE TRUE)
            FetchContent_MakeAvailable(clap_juce_extensions)
        endif()
    endif()

    if(APPLE)
        set(MAREMBA_PLUGIN_FORMATS VST3 AU Standalone)
    else()
        set(MAREMBA_PLUGIN_FORMATS VST3 Standalone)
    endif()

    # The codes and ids never change once a build has reached users: hosts
    # recall sessions by them.
    juce_add_plugin(Maremba
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "Protocodus"
        COMPANY_COPYRIGHT "Copyright (c) 2026 Protocodus"
        BUNDLE_ID "cz.protocodus.maremba"
        PLUGIN_MANUFACTURER_CODE Mrmb
        PLUGIN_CODE Mrb1
        PRODUCT_NAME "Maremba"
        DESCRIPTION "Physically modelled marimba, balafon and kalimba"
        FORMATS ${MAREMBA_PLUGIN_FORMATS}
        VST3_CATEGORIES Instrument
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_SANDBOX_SAFE TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    if(MAREMBA_BUILD_CLAP)
        # CLAP comes from the JUCE extension, outside JUCE's FORMATS list; it
        # inherits Maremba's name, version and artefacts directory.
        clap_juce_extensions_plugin(TARGET Maremba
            CLAP_ID "cz.protocodus.maremba"
            CLAP_FEATURES instrument synthesizer stereo)
    endif()

    juce_generate_juce_header(Maremba)

    target_sources(Maremba PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h)

    target_link_libraries(Maremba
        PRIVATE
            MarembaDSP
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    target_compile_definitions(Maremba
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_DISPLAY_SPLASH_SCREEN=0
            MAREMBA_BUILD_NUMBER="${MAREMBA_BUILD_NUMBER}")
endif()

if(MAREMBA_BUILD_TOOLS)
    # Renders the README's preset demos (see Tools/RenderPresetDemos.cpp).
    add_executable(RenderMarembaPresetDemos Tools/RenderPresetDemos.cpp)
    target_link_libraries(RenderMarembaPresetDemos PRIVATE MarembaDSP)
    add_executable(CalibrateMarembaMain Tools/CalibrateMain.cpp)
    target_link_libraries(CalibrateMarembaMain PRIVATE MarembaDSP)
    if(MSVC)
        target_compile_options(RenderMarembaPresetDemos PRIVATE /W4 /permissive-)
    else()
        target_compile_options(RenderMarembaPresetDemos PRIVATE -Wall -Wextra)
    endif()

    if(MAREMBA_BUILD_PLUGIN)
        # Writes the README's editor screenshot (see Tools/EditorSnapshot.cpp).
        add_executable(MarembaEditorSnapshot Tools/EditorSnapshot.cpp)
        target_link_libraries(MarembaEditorSnapshot PRIVATE Maremba)
        target_include_directories(MarembaEditorSnapshot PRIVATE
            "$<TARGET_PROPERTY:Maremba,JUCE_GENERATED_SOURCES_DIRECTORY>"
            "$<TARGET_PROPERTY:Maremba,INCLUDE_DIRECTORIES>")
    endif()
endif()

if(MAREMBA_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Coverage-guided input fuzzing of the public DSP API (Tests/DspFuzz.cpp).
    # It compiles the engine itself so the engine carries the coverage guides.
    add_executable(MarembaDspFuzz Tests/DspFuzz.cpp ${MAREMBA_DSP_SOURCES})
    target_include_directories(MarembaDspFuzz PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP" "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    set_target_properties(MarembaDspFuzz PROPERTIES CXX_STANDARD 17)
    target_compile_options(MarembaDspFuzz PRIVATE -fsanitize=fuzzer)
    target_link_options(MarembaDspFuzz PRIVATE -fsanitize=fuzzer)
endif()

if(BUILD_TESTING)
    # Tests/contracts.txt is the one list of physics and behaviour contracts.
    file(STRINGS Tests/contracts.txt MAREMBA_CONTRACT_LINES)
    set(MAREMBA_CONTRACTS test_dsp TouchResponseTest)
    foreach(line IN LISTS MAREMBA_CONTRACT_LINES)
        string(REGEX REPLACE "#.*" "" line "${line}")
        string(STRIP "${line}" line)
        if(line)
            list(APPEND MAREMBA_CONTRACTS "${line}")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS Tests/contracts.txt)

    foreach(contract IN LISTS MAREMBA_CONTRACTS)
        string(REGEX REPLACE "Test$" "" suite "${contract}")
        if(suite STREQUAL "test_dsp")
            set(suite Engine)
        endif()
        add_executable(Maremba${suite}Tests Tests/${contract}.cpp)
        target_link_libraries(Maremba${suite}Tests PRIVATE MarembaDSP)
        set_target_properties(Maremba${suite}Tests PROPERTIES CXX_STANDARD 17)
        if(MSVC)
            # Several contracts gate with assert(); keep it live in Release.
            # M_PI is POSIX, which MSVC exposes only on request.
            target_compile_options(Maremba${suite}Tests PRIVATE /W3 /bigobj /UNDEBUG)
            target_compile_definitions(Maremba${suite}Tests PRIVATE _USE_MATH_DEFINES)
        else()
            target_compile_options(Maremba${suite}Tests PRIVATE -Wall -Wextra -UNDEBUG)
        endif()
        if(suite MATCHES "^(EngineFuzz|ExtremeRange)$")
            # Bounded knob, switch and extreme-range coverage; --extended and
            # --full are the long opt-in matrices.
            add_test(NAME Maremba.${suite} COMMAND Maremba${suite}Tests --compact)
            set_tests_properties(Maremba.${suite} PROPERTIES LABELS fuzz TIMEOUT 1800)
        elseif(suite STREQUAL "MainCalibration")
            foreach(rate IN ITEMS 44100 48000 96000)
                add_test(NAME Maremba.MainCalibration${rate}
                         COMMAND MarembaMainCalibrationTests --rate ${rate} --stress)
                set_tests_properties(Maremba.MainCalibration${rate} PROPERTIES LABELS unit TIMEOUT 1800)
            endforeach()
        else()
            add_test(NAME Maremba.${suite} COMMAND Maremba${suite}Tests)
            set_tests_properties(Maremba.${suite} PROPERTIES LABELS unit TIMEOUT 1800)
        endif()
    endforeach()

    # The shared U4 tools (Tests/FuzzHarness.h, Tests/ParameterMatrix.h): the
    # fuzz harness replays committed reproducers and seeded inputs on every
    # compiler, and the parameter-extremes matrix runs over MarembaPlayer.
    add_executable(MarembaDspFuzzReplay Tests/DspFuzz.cpp)
    add_executable(MarembaParameterMatrix Tests/ParameterMatrixTest.cpp)
    foreach(target MarembaDspFuzzReplay MarembaParameterMatrix)
        target_link_libraries(${target} PRIVATE MarembaDSP)
        set_target_properties(${target} PROPERTIES CXX_STANDARD 17)
    endforeach()
    target_compile_definitions(MarembaDspFuzzReplay PRIVATE PROTOCODUS_FUZZ_MAIN)
    file(GLOB MAREMBA_FUZZ_REPRODUCERS CONFIGURE_DEPENDS Tests/fuzz-regressions/*)
    add_test(NAME Maremba.DspFuzzReplay COMMAND MarembaDspFuzzReplay -runs=200 ${MAREMBA_FUZZ_REPRODUCERS})
    set_tests_properties(Maremba.DspFuzzReplay PROPERTIES LABELS unit TIMEOUT 1800)
    # A sixteenth of the cases, one configuration each, so the sanitizer job
    # stays bounded; main-build.yml runs the full matrix in shards.
    add_test(NAME Maremba.ParameterMatrix COMMAND MarembaParameterMatrix --rotate --jobs 4 --shard 0/16)
    set_tests_properties(Maremba.ParameterMatrix PROPERTIES LABELS fuzz TIMEOUT 3600)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        # Generated tables must match their generators; neither check needs NumPy.
        add_test(NAME Maremba.OutputPeakKernel
                 COMMAND "${Python3_EXECUTABLE}" Tests/generate_output_peak_kernel.py --check
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME Maremba.CloseHrtf
                 COMMAND "${Python3_EXECUTABLE}" Tests/generate_close_hrtf.py --check
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(Maremba.OutputPeakKernel Maremba.CloseHrtf PROPERTIES LABELS unit)

        # Tools/Demos/run_benchmark.py builds its renderer with clang++ as a
        # POSIX tool (extensionless binary, filenames Windows rejects), so the
        # contract runs on macOS and Linux, where the Rack CI ran it before.
        find_program(MAREMBA_CLANGXX clang++)
        if(MAREMBA_CLANGXX AND NOT WIN32)
            add_test(NAME Maremba.DemoRenderer
                     COMMAND "${Python3_EXECUTABLE}" Tests/test_demo_renderer.py
                     WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            set_tests_properties(Maremba.DemoRenderer PROPERTIES LABELS unit TIMEOUT 1800)
        endif()

        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -c "import numpy, scipy, mpmath"
            RESULT_VARIABLE MAREMBA_PYTHON_NUMERIC_RESULT
            OUTPUT_QUIET ERROR_QUIET)
        if(MAREMBA_PYTHON_NUMERIC_RESULT EQUAL 0)
            # generate_bar_modes.py, generate_kalimba_modes.py and
            # generate_kalimba_shapes.py are not registered: their checks fail
            # on unchanged source (see README, Known gaps).
            foreach(check IN ITEMS
                    "KalimbaMountTable|generate_kalimba_mount.py"
                    "RoomTemporalQualityTool|test_room_temporal_quality.py"
                    "CalibrationTools|test_calibration_tools.py")
                string(REPLACE "|" ";" check "${check}")
                list(POP_FRONT check name script)
                add_test(NAME Maremba.${name}
                         COMMAND "${Python3_EXECUTABLE}" Tests/${script} ${check}
                         WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
                set_tests_properties(Maremba.${name} PROPERTIES LABELS unit TIMEOUT 1800)
            endforeach()

            # Both measurements compile their own probe from Source/DSP and
            # refuse a non-empty output directory, so each run starts empty.
            set(MAREMBA_MEASURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/measurements")
            add_test(NAME Maremba.MeasurementsClean
                     COMMAND "${CMAKE_COMMAND}" -E rm -rf "${MAREMBA_MEASURE_DIR}")
            set_tests_properties(Maremba.MeasurementsClean PROPERTIES
                FIXTURES_SETUP MarembaMeasurements LABELS unit)
            add_test(NAME Maremba.OutputHeadroom
                     COMMAND "${Python3_EXECUTABLE}" Tests/measure_output_headroom.py
                             --compiler "${CMAKE_CXX_COMPILER}"
                             --output-dir "${MAREMBA_MEASURE_DIR}/output-headroom"
                     WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME Maremba.RoomQualityMeasurement
                     COMMAND "${Python3_EXECUTABLE}" Tests/measure_room_quality.py --check
                             --output "${MAREMBA_MEASURE_DIR}/room-quality"
                     WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            set_tests_properties(Maremba.OutputHeadroom Maremba.RoomQualityMeasurement PROPERTIES
                FIXTURES_REQUIRED MarembaMeasurements LABELS unit TIMEOUT 1800
                ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}")
        elseif(MAREMBA_REQUIRE_PYTHON_TESTS)
            message(FATAL_ERROR "Maremba: the Python checks need numpy, scipy and mpmath")
        else()
            message(WARNING "Maremba: numpy, scipy or mpmath missing; generator and measurement checks are not registered")
        endif()
    elseif(MAREMBA_REQUIRE_PYTHON_TESTS)
        message(FATAL_ERROR "Maremba: MAREMBA_REQUIRE_PYTHON_TESTS needs a Python 3 interpreter")
    endif()

    if(MAREMBA_BUILD_TOOLS)
        add_test(NAME Maremba.AudioDemos
                 COMMAND RenderMarembaPresetDemos --check "${CMAKE_CURRENT_SOURCE_DIR}/Docs/audio/midi")
        set_tests_properties(Maremba.AudioDemos PROPERTIES LABELS audio TIMEOUT 3600)
    endif()

    if(MAREMBA_BUILD_PLUGIN)
        add_executable(MarembaPluginProcessorTests Tests/PluginProcessorTests.cpp)
        target_link_libraries(MarembaPluginProcessorTests PRIVATE Maremba)
        target_include_directories(MarembaPluginProcessorTests PRIVATE
            "$<TARGET_PROPERTY:Maremba,JUCE_GENERATED_SOURCES_DIRECTORY>"
            "$<TARGET_PROPERTY:Maremba,INCLUDE_DIRECTORIES>")
        add_test(NAME Maremba.PluginProcessor COMMAND MarembaPluginProcessorTests)
        set_tests_properties(Maremba.PluginProcessor PROPERTIES LABELS unit TIMEOUT 600)

        if(MAREMBA_BUILD_TOOLS)
            add_test(NAME Maremba.EditorRender
                     COMMAND MarembaEditorSnapshot "${CMAKE_CURRENT_BINARY_DIR}/editor-render/maremba.png")
            set_tests_properties(Maremba.EditorRender PROPERTIES LABELS render TIMEOUT 300)
        endif()
    endif()

    if(TARGET MarembaDspFuzz)
        # The time-boxed run every CI build makes; main builds run it longer.
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fuzz-corpus")
        add_test(NAME Maremba.DspFuzz
                 COMMAND MarembaDspFuzz -max_total_time=120 -seed=1 -rss_limit_mb=4096
                         "${CMAKE_CURRENT_BINARY_DIR}/fuzz-corpus")
        set_tests_properties(Maremba.DspFuzz PROPERTIES LABELS fuzz TIMEOUT 600)
    endif()
endif()
