cmake_minimum_required(VERSION 3.20)
project(blender_ui VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Bundled third-party libraries (pre-built, in third_party/)
# ---------------------------------------------------------------------------

set(TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---- OpenGL ----
find_package(OpenGL REQUIRED)

# ---- GLFW (bundled prebuilt) ----
add_library(glfw3_bundled INTERFACE)
target_include_directories(glfw3_bundled INTERFACE "${TP}/glfw/include")

if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC))
    target_link_libraries(glfw3_bundled INTERFACE "${TP}/glfw/lib/glfw3.lib")
elseif(MINGW)
    target_link_libraries(glfw3_bundled INTERFACE "${TP}/glfw/lib/libglfw3.a")
else()
    # Fallback: system GLFW
    find_package(glfw3 3.3 REQUIRED)
    target_link_libraries(glfw3_bundled INTERFACE glfw)
endif()

# GLFW on Windows needs these system libs
if(WIN32)
    target_link_libraries(glfw3_bundled INTERFACE gdi32 user32 shell32)
endif()

# ---- GLEW (bundled prebuilt, static) ----
add_library(glew_bundled INTERFACE)
target_include_directories(glew_bundled INTERFACE "${TP}/glew/include")
target_compile_definitions(glew_bundled INTERFACE GLEW_STATIC)

if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    target_link_libraries(glew_bundled INTERFACE "${TP}/glew/lib/glew32s.lib")
elseif(MINGW)
    # MinGW: need to link against the MSVC static lib or rebuild — use opengl32 loader
    # Fallback: find system GLEW or skip (user must install)
    find_package(GLEW QUIET)
    if(GLEW_FOUND)
        target_link_libraries(glew_bundled INTERFACE GLEW::GLEW)
    else()
        message(WARNING "GLEW not found for MinGW. Install glew via MSYS2/pacman.")
    endif()
else()
    find_package(GLEW REQUIRED)
    target_link_libraries(glew_bundled INTERFACE GLEW::GLEW)
endif()

# ---------------------------------------------------------------------------
# bl_ui — static library
# ---------------------------------------------------------------------------

set(BL_UI_SOURCES
    src/core/menu_type.cpp
    src/core/layout.cpp
    src/core/keymap.cpp
    src/render/gl_context.cpp
    src/render/gfx/gl_backend.cpp
    src/render/roundbox.cpp
    src/render/font.cpp
    src/render/icon_atlas.cpp
    src/render/grid.cpp
    src/render/nav_gizmo.cpp
    src/ui/popup_menu.cpp
    src/ui/menu_bar.cpp
    src/ui/viewport.cpp
    src/ui/confirm_dialog.cpp
    src/ui/file_dialog.cpp
    src/ui/about_dialog.cpp
    src/ui/splash_screen.cpp
    src/ui/toolbar.cpp
    src/ui/scene_panel.cpp
    src/ui/checkbox.cpp
    src/ui/app.cpp
)

# ---------------------------------------------------------------------------
# Optional Vulkan backend — enable with -DBLUI_VULKAN_BACKEND=ON
# Requires: Vulkan SDK (dxcompiler from the SDK is used for GLSL → SPIR-V)
# ---------------------------------------------------------------------------
option(BLUI_VULKAN_BACKEND "Build the Vulkan rendering backend" OFF)
if(BLUI_VULKAN_BACKEND)
    find_package(Vulkan REQUIRED)
    list(APPEND BL_UI_SOURCES src/render/gfx/vk_backend.cpp)
endif()

add_library(bl_ui STATIC ${BL_UI_SOURCES})

target_include_directories(bl_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        # Internal render/ui headers (needed when downstream includes app.h etc.)
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(bl_ui
    PUBLIC
        OpenGL::GL
        glfw3_bundled
        glew_bundled
)

if(BLUI_VULKAN_BACKEND)
    target_compile_definitions(bl_ui PUBLIC BLUI_VULKAN_BACKEND)
    target_link_libraries(bl_ui PUBLIC Vulkan::Vulkan)
    # DXC ships with the Vulkan SDK and is used for runtime GLSL → SPIR-V.
    # glslang ships with the Vulkan SDK and handles GLSL → SPIR-V.
    # Use debug libs in Debug builds, release libs otherwise.
    set(_VK_LIB "$ENV{VULKAN_SDK}/Lib")
    foreach(_lib glslang SPIRV glslang-default-resource-limits
                 MachineIndependent GenericCodeGen OSDependent
                 SPIRV-Tools SPIRV-Tools-opt)
        target_link_libraries(bl_ui PUBLIC
            "$<$<CONFIG:Debug>:${_VK_LIB}/${_lib}d.lib>"
            "$<$<NOT:$<CONFIG:Debug>>:${_VK_LIB}/${_lib}.lib>"
        )
    endforeach()
    target_include_directories(bl_ui PUBLIC "$ENV{VULKAN_SDK}/Include")
endif()

if(WIN32 AND MSVC)
    # /utf-8 so string literals with Unicode in source are compiled correctly
    target_compile_options(bl_ui PUBLIC /utf-8)
endif()

# ---------------------------------------------------------------------------
# bl_ui_demo — demo executable
# ---------------------------------------------------------------------------

add_executable(bl_ui_demo demo/main.cpp)
target_link_libraries(bl_ui_demo PRIVATE bl_ui)

# Copy GLFW DLL next to the exe so it runs without PATH setup
add_custom_command(TARGET bl_ui_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TP}/glfw/lib/glfw3.dll"
        $<TARGET_FILE_DIR:bl_ui_demo>
)

