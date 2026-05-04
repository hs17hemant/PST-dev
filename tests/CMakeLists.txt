# pstwriter/tests/CMakeLists.txt
#
# Catch2 v3 is fetched at configure time so the developer does not need to
# install it manually. We pin to a tagged release rather than `main` so the
# build is reproducible.

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# --------------------------------------------------------------------
# pstwriter_tests — single executable that pulls in every test_*.cpp.
# CTest discovers individual TEST_CASEs via Catch2's discovery macro.
# --------------------------------------------------------------------
add_executable(pstwriter_tests
    test_crc.cpp
    test_encoding.cpp
    test_types.cpp
    test_ndb.cpp
    test_block.cpp
    test_ltp.cpp
    test_m5_allocator.cpp
    test_m5_btpage.cpp
    test_m5_nbt_reader.cpp
    test_m5_end_to_end.cpp
    test_messaging.cpp
    # pst_info source compiled into the test binary so the LTP
    # end-to-end test can call runPstInfo() directly. We define
    # PSTWRITER_PST_INFO_NO_MAIN to suppress the binary's main().
    ${CMAKE_SOURCE_DIR}/tools/pst_info.cpp
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/tools/pst_info.cpp
    PROPERTIES COMPILE_DEFINITIONS PSTWRITER_PST_INFO_NO_MAIN
)

target_link_libraries(pstwriter_tests
    PRIVATE
        pstwriter
        Catch2::Catch2WithMain
)

target_include_directories(pstwriter_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/pstwriter
        ${CMAKE_SOURCE_DIR}/tools
)

if(MSVC)
    target_compile_options(pstwriter_tests PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
    if(PSTWRITER_WARNINGS_AS_ERRORS)
        target_compile_options(pstwriter_tests PRIVATE /WX)
    endif()
    target_compile_definitions(pstwriter_tests PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(pstwriter_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
    )
endif()

# --------------------------------------------------------------------
# Register every Catch2 TEST_CASE with CTest.
# --------------------------------------------------------------------
include(CTest)
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(pstwriter_tests
    EXTRA_ARGS --reporter console
)
