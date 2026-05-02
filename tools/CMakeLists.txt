# pstwriter/tools/CMakeLists.txt — M2 CLI utilities

add_executable(pst_info pst_info.cpp)

target_link_libraries(pst_info PRIVATE pstwriter)

target_include_directories(pst_info PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/pstwriter
)

if(MSVC)
    target_compile_options(pst_info PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
    if(PSTWRITER_WARNINGS_AS_ERRORS)
        target_compile_options(pst_info PRIVATE /WX)
    endif()
    target_compile_definitions(pst_info PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

set_target_properties(pst_info PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
