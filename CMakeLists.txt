# Copyright (c) 2019-2026 Elias Bachaalany
# SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
#
# This file is licensed under the Human-Origin Source License v1.0.
# See LICENSE.

cmake_minimum_required(VERSION 3.27)
project(qscripts)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include ida-cmake bootstrap. ida-cmake is installed alongside the SDK at
# $IDASDK/ida-cmake (see the ida-cmake README). We use it directly rather than
# the SDK's own src/cmake, which differs across SDK versions.
include($ENV{IDASDK}/ida-cmake/bootstrap.cmake)
find_package(idasdk REQUIRED)

# Resolve libidacpp: use a local ../libidacpp if present, otherwise fetch it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libidacpp/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libidacpp ${CMAKE_CURRENT_BINARY_DIR}/libidacpp)
else()
    include(FetchContent)
    FetchContent_Declare(
        libidacpp
        GIT_REPOSITORY https://github.com/allthingsida/libidacpp.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(libidacpp)
endif()

# Convert paths to native format
set(SAMPLE_IDB "${IDA_CMAKE_DIR}/samples/wizmo32.exe.i64")
file(TO_NATIVE_PATH "${SAMPLE_IDB}" SAMPLE_IDB)

# Add plugin
ida_add_plugin(qscripts
    SOURCES
        qscripts.cpp
        qscripts_monitor.cpp
        script.hpp
        qscripts_utils.h
        qscripts_expander.h
        qscripts_deps.h
        qscripts_monitor.h
    DEBUG_ARGS
        "${SAMPLE_IDB}"
)

target_link_libraries(qscripts PRIVATE libidacpp::libidacpp)
