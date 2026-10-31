########################################################################
# Run the plugin self-tests from a staged install (cmake -P script).
# A copy of cmake/PothosRunSelfTests.cmake of PothosComms (with the check for PothosBlocks).
#
# Required: -DPREFIX=<staged install> -DTEST_HOME_DIR=<isolated user dirs>
#           -DBINDIR=<relative bin dir> -DLIBDIR=<relative lib dir>
# Optional: -DTEST_PATH=<plugin path> to run a single self test,
#           -DTEST_PATHS=<;-list of plugin paths> to run each of them
#           -DPLUGIN_PATH=<module directory> for POTHOS_PLUGIN_PATH (modules
#           that are not installed in PREFIX)
########################################################################
cmake_minimum_required(VERSION 3.25) #policies for cmake -P scripts
foreach(var PREFIX TEST_HOME_DIR BINDIR LIBDIR)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "PothosRunSelfTests: ${var} is not set")
    endif ()
endforeach()

#the audio self-tests use blocks of PothosBlocks (/blocks/feeder_source, ...)
file(GLOB blocks_dirs "${PREFIX}/*/Pothos/modules*/blocks")
if (NOT blocks_dirs)
    message(FATAL_ERROR "PothosRunSelfTests: PothosBlocks is not installed in ${PREFIX}. "
        "The audio self-tests need its blocks (/blocks/feeder_source, /blocks/collector_sink). "
        "Install PothosBlocks into the prefix of the core.")
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/PothosTestEnv.cmake)
pothos_test_env("${PREFIX}" "${TEST_HOME_DIR}" "${BINDIR}" "${LIBDIR}")
if (PLUGIN_PATH)
    #POTHOS_PLUGIN_PATH adds to the module directories of PREFIX: the same
    #modules installed in PREFIX would load too, and a test could run them
    file(GLOB plugin_subdirs RELATIVE "${PLUGIN_PATH}" "${PLUGIN_PATH}/*")
    foreach(subdir IN LISTS plugin_subdirs)
        file(GLOB installed "${PREFIX}/*/Pothos/modules*/${subdir}")
        if (installed)
            message(FATAL_ERROR "PothosRunSelfTests: the modules under test are also installed "
                "in ${installed}. Remove them, or run the tests against another core prefix.")
        endif ()
    endforeach()
    set(ENV{POTHOS_PLUGIN_PATH} "${PLUGIN_PATH}")
endif ()

set(util "${PREFIX}/${BINDIR}/PothosUtil")
if (WIN32)
    set(util "${PREFIX}/${BINDIR}/PothosUtil.exe")
endif ()

if (DEFINED TEST_PATHS)
    if (NOT TEST_PATHS)
        message(FATAL_ERROR "PothosRunSelfTests: TEST_PATHS is empty")
    endif ()
    foreach(path IN LISTS TEST_PATHS)
        execute_process(COMMAND "${util}" --self-test1 "${path}" RESULT_VARIABLE rc)
        if (NOT rc EQUAL 0)
            message(FATAL_ERROR "PothosUtil --self-test1 ${path} failed: ${rc}")
        endif ()
    endforeach()
    return()
endif ()

if (TEST_PATH)
    set(args --self-test1 "${TEST_PATH}")
else ()
    set(args --self-tests)
endif ()

execute_process(COMMAND "${util}" ${args} RESULT_VARIABLE rc)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "PothosUtil ${args} failed: ${rc}")
endif ()
