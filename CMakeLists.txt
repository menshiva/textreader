cmake_minimum_required(VERSION 3.28)
project(TextReader CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # static CRT

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(IMGUI STATIC)
target_sources(IMGUI PRIVATE
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(IMGUI PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

# use freetype in ImGui
include(FetchContent)
FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-14-3
        GIT_SHALLOW TRUE
)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)

target_link_libraries(IMGUI PUBLIC d3d11 dxgi d3dcompiler freetype)
target_compile_definitions(IMGUI PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# App
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(TextReader WIN32 ${SOURCES})
target_link_libraries(TextReader PRIVATE IMGUI winhttp)
target_compile_options(TextReader PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus /MP)

# copy fonts
add_custom_command(TARGET TextReader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/res/fonts
        $<TARGET_FILE_DIR:TextReader>/fonts
)
