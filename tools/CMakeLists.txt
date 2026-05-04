# pstwriter/tools/CMakeLists.txt — M2 CLI utilities

add_executable(pst_info pst_info.cpp)
add_executable(pst_convert pst_convert.cpp)

foreach(_tool pst_info pst_convert)
    target_link_libraries(${_tool} PRIVATE pstwriter)
    target_include_directories(${_tool} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/pstwriter
    )
    set_target_properties(${_tool} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    if(MSVC)
        target_compile_options(${_tool} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
        if(PSTWRITER_WARNINGS_AS_ERRORS)
            target_compile_options(${_tool} PRIVATE /WX)
        endif()
        target_compile_definitions(${_tool} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()
endforeach()
