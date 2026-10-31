########################################################################
# Shared environment setup for tests that run against a staged install.
# A copy of cmake/PothosTestEnv.cmake of PothosCore.
#
# pothos_test_env(PREFIX TEST_HOME_DIR BINDIR LIBDIR)
#   - points POTHOS_ROOT at the staged install prefix
#   - isolates user data and config directories inside TEST_HOME_DIR
#     so tests never touch the real home directory; only the data/ and
#     config/ subdirectories of TEST_HOME_DIR are ever recreated
#   - makes the staged shared libraries loadable on Windows (PATH);
#     Linux and macOS rely on the relative install rpath
########################################################################
function(pothos_test_env PREFIX TEST_HOME_DIR BINDIR LIBDIR)
    get_filename_component(test_home "${TEST_HOME_DIR}" ABSOLUTE)
    foreach(real_home "$ENV{HOME}" "$ENV{USERPROFILE}")
        if (real_home)
            get_filename_component(real_home "${real_home}" ABSOLUTE)
            if (test_home STREQUAL real_home)
                message(FATAL_ERROR "pothos_test_env: refusing to use the home directory ${real_home} as TEST_HOME_DIR")
            endif ()
        endif ()
    endforeach()

    file(REMOVE_RECURSE "${test_home}/data" "${test_home}/config")
    file(MAKE_DIRECTORY "${test_home}/data" "${test_home}/config")

    set(ENV{POTHOS_ROOT} "${PREFIX}")
    set(ENV{XDG_DATA_HOME} "${test_home}/data")
    set(ENV{XDG_CONFIG_HOME} "${test_home}/config")
    set(ENV{APPDATA} "${test_home}/data")

    if (WIN32)
        file(TO_NATIVE_PATH "${PREFIX}/${BINDIR}" bin_path)
        set(ENV{PATH} "${bin_path};$ENV{PATH}")
    endif ()
endfunction()
