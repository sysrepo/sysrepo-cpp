#
# Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
#
# Written by Václav Kubernát <kubernat@cesnet.cz>
#

cmake_minimum_required(VERSION 3.19)
project(sysrepo-cpp LANGUAGES CXX)
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
set(SYSREPO_CPP_PKG_VERSION "5")
prepare_git_version(SYSREPO_CPP_VERSION ${SYSREPO_CPP_PKG_VERSION})

find_package(Doxygen)
option(WITH_DOCS "Create and install internal documentation (needs Doxygen)" ${DOXYGEN_FOUND})
option(WITH_EXAMPLES "Build examples" ON)

find_package(PkgConfig)
pkg_check_modules(LIBYANG_CPP REQUIRED libyang-cpp>=3 IMPORTED_TARGET)
pkg_check_modules(SYSREPO REQUIRED sysrepo>=3.4.8 IMPORTED_TARGET)

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
# We do not offer any long-term API/ABI guarantees. To make stuff easier for downstream consumers,
# we will be bumping both API and ABI versions very deliberately.
# There will be no attempts at semver tracking, for example.
set_target_properties(sysrepo-cpp PROPERTIES
    VERSION ${SYSREPO_CPP_PKG_VERSION}
    SOVERSION ${SYSREPO_CPP_PKG_VERSION})

if(BUILD_TESTING)
    find_package(doctest 2.4.8 REQUIRED)
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

    include(cmake/SysrepoTest.cmake)

    function(sysrepo_cpp_test)
        cmake_parse_arguments(TEST "" "NAME" "LIBRARIES" ${ARGN})
        sysrepo_test(NAME ${TEST_NAME} LIBRARIES DoctestIntegration sysrepo-cpp ${TEST_LIBRARIES} ${TEST_UNPARSED_ARGUMENTS})
    endfunction()

    set(fixture-test-module
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_module.yang
        )
    set(fixture-dynamic-subscriptions
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_module.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/iana-if-type@2014-05-08.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/ietf-interfaces@2018-02-20.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/ietf-ip@2018-02-22.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/ietf-network-instance@2019-01-21.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/ietf-subscribed-notifications@2019-09-09.yang -e replay
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/ietf-yang-push@2019-09-09.yang -e on-change
        )

    sysrepo_cpp_test(NAME session FIXTURE fixture-test-module)
    sysrepo_cpp_test(NAME subscriptions FIXTURE fixture-test-module LIBRARIES Threads::Threads)
    sysrepo_cpp_test(NAME subscriptions-dynamic FIXTURE fixture-dynamic-subscriptions LIBRARIES PkgConfig::SYSREPO)
    sysrepo_cpp_test(NAME unsafe FIXTURE fixture-test-module LIBRARIES PkgConfig::SYSREPO)
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
