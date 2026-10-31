########################################################################
# PothosAudio: audio source and sink blocks for Pothos.
# Builds against an installed Pothos core (find_package(Pothos)).
# The audio device input and output come from miniaudio, a copy of the
# single-file library in third_party/miniaudio (no system package).
# nlohmann_json comes from find_package: from the system, or from a
# package manager that gives a toolchain file.
########################################################################
cmake_minimum_required(VERSION 3.25)
project(PothosAudio C CXX)

option(AUDIO_BUILD_TESTS "Register the CTest tests of the audio blocks" ${PROJECT_IS_TOP_LEVEL})
option(AUDIO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (GCC and Clang)" OFF)

#the core defines the variable POTHOS_ROOT (its install prefix), and the
#runtime environment often sets POTHOS_ROOT too. find_package must not use
#them as search prefixes (CMP0144): it could find another core than
#CMAKE_PREFIX_PATH gives. NO_PACKAGE_ROOT_PATH keeps them out.
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

find_package(Pothos CONFIG REQUIRED NO_PACKAGE_ROOT_PATH)

#the core loads modules from its own prefix (POTHOS_ROOT): install there
#when the user does not give CMAKE_INSTALL_PREFIX
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${POTHOS_ROOT}" CACHE PATH "Install prefix (default: the prefix of the Pothos core)" FORCE)
    message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX} (the prefix of the Pothos core)")
endif()

########################################################################
# Third party libraries
########################################################################
find_package(nlohmann_json 3.11 CONFIG)
if(NOT nlohmann_json_FOUND)
    message(FATAL_ERROR
        "PothosAudio: missing build dependency: nlohmann_json (>= 3.11).\n"
        "Install the system package (for example: nlohmann-json), "
        "or install it with a package manager and give its toolchain file "
        "(-DCMAKE_TOOLCHAIN_FILE=...) or CMAKE_PREFIX_PATH. Refer to README.md.")
endif()

add_library(audio_json INTERFACE)
target_link_libraries(audio_json INTERFACE nlohmann_json::nlohmann_json)

#miniaudio: the implementation is compiled once, as third party code (no
#warning options of this project), with only the device input and output
find_package(Threads REQUIRED)
add_library(audio_miniaudio STATIC MiniaudioImpl.c)
set_target_properties(audio_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(audio_miniaudio SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/miniaudio)
target_compile_definitions(audio_miniaudio PUBLIC
    MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION
    MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE)
target_link_libraries(audio_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(APPLE)
    target_link_libraries(audio_miniaudio PUBLIC
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(audio_miniaudio PUBLIC m)
elseif(WIN32)
    target_link_libraries(audio_miniaudio PUBLIC winmm) #timeBeginPeriod of the blocks
endif()

########################################################################
# Module
########################################################################
#audio_warnings(<target>): the common warnings (GCC and Clang, as the core),
#errors with AUDIO_WARNINGS_AS_ERRORS (CI)
function(audio_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        if(AUDIO_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

#self-tests under /audio/tests, run one by one by the audio.selftests test
set(AUDIO_TESTS
    test_audio_int8_conversion
    test_audio_format_from_dtype
    test_audio_backend_names
    test_audio_interleave
    test_audio_native_sample_rates
    test_audio_info
    test_audio_bad_rate
    test_audio_rate_rounded
    test_audio_deactivate_without_stream
    test_audio_overlay
    test_audio_device_select
    test_audio_report_mode
    test_audio_source_formats
    test_audio_sink_formats
    test_audio_sink_start_no_underflow
    test_audio_source_overflow
    test_audio_source_stop_fast
    test_audio_loop_graph
    test_audio_sink_backoff_recovers
    test_audio_device_stopped
)

POTHOS_MODULE_UTIL(
    TARGET AudioSupport
    SOURCES
        AudioBlock.cpp
        AudioSource.cpp
        AudioSink.cpp
        AudioInfo.cpp
        AudioHelpers.cpp
        TestAudioHelpers.cpp
        TestAudioBlocks.cpp
    LIBRARIES audio_json audio_miniaudio
    DOC_SOURCES AudioSource.cpp AudioSink.cpp
    DESTINATION audio
)
set_target_properties(AudioSupport PROPERTIES CXX_EXTENSIONS OFF)
target_compile_features(AudioSupport PRIVATE cxx_std_23)
#the number of frames to wait for when no frames are available
target_compile_definitions(AudioSupport PRIVATE MIN_FRAMES_BLOCKING=1024)
audio_warnings(AudioSupport)

########################################################################
# Tests: the module is staged into a prefix inside the build tree and
# loaded with POTHOS_PLUGIN_PATH by the PothosUtil of the installed core.
# The tests use blocks of PothosBlocks: install it into the core prefix.
# They need no audio hardware: miniaudio uses its null backend.
########################################################################
if(AUDIO_BUILD_TESTS)
    enable_testing()
    set(AUDIO_STAGE_DIR ${PROJECT_BINARY_DIR}/stage)
    set(AUDIO_SELFTEST_TIMEOUT 600 CACHE STRING "Timeout in seconds for the self-tests")

    add_test(NAME audio.stage
        COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --prefix ${AUDIO_STAGE_DIR} --config $<CONFIG>)
    set_tests_properties(audio.stage PROPERTIES FIXTURES_SETUP audio_stage)

    list(TRANSFORM AUDIO_TESTS PREPEND /audio/tests/ OUTPUT_VARIABLE audio_test_paths)
    add_test(NAME audio.selftests
        COMMAND ${CMAKE_COMMAND}
            -DPREFIX=${POTHOS_ROOT}
            -DTEST_HOME_DIR=${PROJECT_BINARY_DIR}/test-home
            -DBINDIR=bin
            -DLIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DPLUGIN_PATH=${AUDIO_STAGE_DIR}/${CMAKE_INSTALL_LIBDIR}/Pothos/modules${POTHOS_ABI_VERSION}
            "-DTEST_PATHS=${audio_test_paths}"
            -P ${PROJECT_SOURCE_DIR}/cmake/PothosRunSelfTests.cmake)
    set_tests_properties(audio.selftests PROPERTIES
        FIXTURES_REQUIRED audio_stage
        ENVIRONMENT POTHOS_AUDIO_BACKEND=null
        TIMEOUT ${AUDIO_SELFTEST_TIMEOUT})
endif()
