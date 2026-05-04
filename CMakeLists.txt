cmake_minimum_required(VERSION 3.20)

project(pstwriter
    VERSION 0.1.0
    DESCRIPTION "Standalone C++ library that creates Microsoft Outlook .pst files"
    LANGUAGES CXX
)

# --------------------------------------------------------------------
# Global C++ settings
# --------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Place built artefacts in predictable locations under build/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --------------------------------------------------------------------
# Options
# --------------------------------------------------------------------
option(PSTWRITER_BUILD_TESTS         "Build pstwriter unit tests"             ON)
option(PSTWRITER_BUILD_TOOLS         "Build pst_info and other CLI tools"     ON)
option(PSTWRITER_WARNINGS_AS_ERRORS  "Treat compile warnings as errors"       ON)

# --------------------------------------------------------------------
# pstwriter core library (M1 + M2 sources)
# --------------------------------------------------------------------
add_library(pstwriter STATIC
    src/crc.cpp
    src/encoding.cpp
    src/ndb.cpp
    src/page.cpp
    src/block.cpp
    src/writer.cpp
    src/ltp.cpp
    src/m5_allocator.cpp
    src/nbt.cpp
    src/messaging.cpp
    src/graph_convert.cpp
    src/graph_message.cpp
    src/mail.cpp
    src/graph_contact.cpp
    src/contact.cpp
    src/graph_event.cpp
    src/event.cpp
    src/pst_baseline.cpp
)

# Two include paths so internal code can write `#include "crc.hpp"`
# (bare) and downstream consumers can write `#include <pstwriter/crc.hpp>`
# (namespaced). The first is also picked up by the test binary.
target_include_directories(pstwriter
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/pstwriter>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(pstwriter PUBLIC cxx_std_17)

# --------------------------------------------------------------------
# Compiler warning configuration
# --------------------------------------------------------------------
if(MSVC)
    # /W4 = high warning level. /permissive- = strict standards conformance.
    # /Zc:__cplusplus = report the real __cplusplus value.
    target_compile_options(pstwriter PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
    if(PSTWRITER_WARNINGS_AS_ERRORS)
        target_compile_options(pstwriter PRIVATE /WX)
    endif()

    target_compile_definitions(pstwriter PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
else()
    target_compile_options(pstwriter PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    )
    if(PSTWRITER_WARNINGS_AS_ERRORS)
        target_compile_options(pstwriter PRIVATE -Werror)
    endif()
endif()

# --------------------------------------------------------------------
# Tests and tools
# --------------------------------------------------------------------
if(PSTWRITER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PSTWRITER_BUILD_TOOLS)
    # tools/CMakeLists.txt is added in M2 once pst_info exists.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
        add_subdirectory(tools)
    endif()
endif()

# --------------------------------------------------------------------
# Install rules (basic; expanded in M7)
# --------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS pstwriter
    EXPORT pstwriterTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/pstwriter
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)
