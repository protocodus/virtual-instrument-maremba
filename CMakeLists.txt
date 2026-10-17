cmake_minimum_required(VERSION 3.22)

# The DSP version is the product version (Rack devices take X.Y.Z from it).
project(Maremba VERSION 1.5.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MAREMBA_REQUIRE_PYTHON_TESTS
    "Fail to configure unless every Python check can be registered (CI sets it)" OFF)

include(CTest)

set(MAREMBA_DSP_SOURCES
    Source/DSP/MarembaEngine.cpp
    Source/DSP/MarembaVoice.cpp)

add_library(MarembaDSP STATIC ${MAREMBA_DSP_SOURCES})
target_include_directories(MarembaDSP PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP")
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
        else()
            add_test(NAME Maremba.${suite} COMMAND Maremba${suite}Tests)
            set_tests_properties(Maremba.${suite} PROPERTIES LABELS unit TIMEOUT 1800)
        endif()
    endforeach()

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
                    "RoomTemporalQualityTool|test_room_temporal_quality.py")
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
endif()
