#
# Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
#
# Written by Václav Kubernát <kubernat@cesnet.cz>
#

project(sysrepo-cpp LANGUAGES CXX)
cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_FLAGS_DEBUG "-Werror ${CMAKE_CXX_FLAGS_DEBUG}")

set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic -Woverloaded-virtual -Wimplicit-fallthrough -Wsuggest-override ${CMAKE_CXX_FLAGS}")

add_custom_target(sysrepo-cpp-version-cmake
        cmake/ProjectGitVersion.cmake
        cmake/ProjectGitVersionRunner.cmake
        )
include(cmake/ProjectGitVersion.cmake)
prepare_git_version(SYSREPO_CPP_VERSION "0.0")

find_package(Doxygen)
option(WITH_DOCS "Create and install internal documentation (needs Doxygen)" ${DOXYGEN_FOUND})
option(WITH_EXAMPLES "Build examples" ON)

find_package(PkgConfig)
pkg_check_modules(LIBYANG_CPP REQUIRED libyang-cpp=1.1.0 IMPORTED_TARGET)
pkg_check_modules(SYSREPO REQUIRED sysrepo>=2.2.12 IMPORTED_TARGET)
set(SYSREPO_CPP_PKG_VERSION "1.2.0")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(sysrepo-cpp SHARED
        src/Connection.cpp
        src/Enum.cpp
        src/Session.cpp
        src/Subscription.cpp
        src/utils/exception.cpp
        src/utils/utils.cpp
    )

target_link_libraries(sysrepo-cpp PRIVATE PkgConfig::SYSREPO PUBLIC PkgConfig::LIBYANG_CPP)

if(BUILD_TESTING)
    find_package(doctest 2.4.6 REQUIRED)
    find_package(trompeloeil 42 REQUIRED)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    add_library(DoctestIntegration STATIC
        tests/doctest-integration.cpp
        tests/utils.cpp
        )
    target_include_directories(DoctestIntegration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/ ${CMAKE_CURRENT_SOURCE_DIR}/src/ "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(DoctestIntegration doctest::doctest trompeloeil::trompeloeil)
    target_compile_definitions(DoctestIntegration PUBLIC DOCTEST_CONFIG_SUPER_FAST_ASSERTS)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_repositories)

    include(cmake/DiscoverSysrepoExecutables.cmake)
    discover_sysrepo_executables()

    function(sysrepo_cpp_test name module)
        set(TESTNAME test_${name})

        add_executable(${TESTNAME}
            tests/${name}.cpp
            )
        target_link_libraries(test_${name} DoctestIntegration sysrepo-cpp)

        add_test(${TESTNAME} ${TESTNAME})

        set_tests_properties(${TESTNAME} PROPERTIES FIXTURES_REQUIRED ${TESTNAME}_setup)

        add_test(NAME ${TESTNAME}_init COMMAND ${CMAKE_CURRENT_BINARY_DIR}/init_datastore.bash "${module}")
        set_tests_properties(${TESTNAME}_init PROPERTIES FIXTURES_SETUP ${TESTNAME}_setup)
        add_test(NAME ${TESTNAME}_cleanup COMMAND ${CMAKE_CURRENT_BINARY_DIR}/cleanup_datastore.bash)
        set_tests_properties(${TESTNAME}_cleanup PROPERTIES FIXTURES_CLEANUP ${TESTNAME}_setup)

        set_property(TEST ${TESTNAME} ${TESTNAME}_init ${TESTNAME}_cleanup APPEND PROPERTY ENVIRONMENT
            "SYSREPO_REPOSITORY_PATH=${CMAKE_CURRENT_BINARY_DIR}/test_repositories/${TESTNAME}"
            "SYSREPO_SHM_PREFIX=sysrepo-cpp_${TESTNAME}"
            )
    endfunction()

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/init_datastore.bash.in
        ${CMAKE_CURRENT_BINARY_DIR}/init_datastore.bash @ONLY)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/cleanup_datastore.bash.in
        ${CMAKE_CURRENT_BINARY_DIR}/cleanup_datastore.bash @ONLY)

    sysrepo_cpp_test(session "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_module.yang")
    sysrepo_cpp_test(subscriptions "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_module.yang")
    target_link_libraries(test_subscriptions Threads::Threads)
    sysrepo_cpp_test(unsafe "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_module.yang")
    target_link_libraries(test_unsafe PkgConfig::SYSREPO)
endif()

if(WITH_DOCS)
    set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${doxyfile_in} ${doxyfile} @ONLY)
    add_custom_target(doc
            COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
            SOURCES ${doxyfile_in}
            )
endif()

if(WITH_EXAMPLES)
    function(sysrepo_cpp_example name)
        add_executable(${name}
            examples/${name}.cpp
            )
        target_link_libraries(${name} sysrepo-cpp)
    endfunction()

    sysrepo_cpp_example(module_change_example)

endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/sysrepo-cpp.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/sysrepo-cpp.pc" @ONLY)

# this is not enough, but at least it will generate the `install` target so that the CI setup is less magic
install(TARGETS sysrepo-cpp)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/sysrepo-cpp" TYPE INCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sysrepo-cpp.pc" DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
