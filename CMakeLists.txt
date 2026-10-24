cmake_minimum_required(VERSION 3.14)
project(memviz VERSION 1.0.1 LANGUAGES CXX)

# memviz is header-only: an INTERFACE target carries include dirs + threads.
add_library(memviz INTERFACE)
add_library(memviz::memviz ALIAS memviz)

target_compile_features(memviz INTERFACE cxx_std_17)

target_include_directories(memviz INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

find_package(Threads REQUIRED)
target_link_libraries(memviz INTERFACE Threads::Threads)

# ---------------------------------------------------------------------------
# install + find_package(memviz CONFIG)
#
# The previous version installed the targets file but no package config, so
# `find_package(memviz CONFIG REQUIRED)` failed for anyone consuming an
# installed copy (FetchContent worked, which hid the problem).
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS memviz EXPORT memvizTargets)
install(FILES include/memviz/memviz.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/memviz)
install(EXPORT memvizTargets
        NAMESPACE memviz::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memviz)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/memvizConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/memvizConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memviz)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/memvizConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/memvizConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/memvizConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memviz)

# ---------------------------------------------------------------------------
# examples / tests: built only when memviz is the top-level project, so that
# `add_subdirectory(memviz)` or FetchContent does not inject targets into the
# consumer's build.
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(MEMVIZ_BUILD_EXAMPLES "Build the bundled examples" ON)
    option(MEMVIZ_BUILD_TESTS "Build the test suite" ON)

    if(MEMVIZ_BUILD_EXAMPLES)
        add_executable(sort_demo examples/sort_demo.cpp)
        target_link_libraries(sort_demo PRIVATE memviz::memviz)
    endif()

    if(MEMVIZ_BUILD_TESTS)
        enable_testing()
        add_executable(memviz_tests tests/memviz_test.cpp)
        target_link_libraries(memviz_tests PRIVATE memviz::memviz)
        if(MSVC)
            target_compile_options(memviz_tests PRIVATE /W4)
        else()
            target_compile_options(memviz_tests PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME memviz_tests COMMAND memviz_tests)
    endif()
endif()
