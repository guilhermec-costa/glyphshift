cmake_minimum_required(VERSION 3.20)
project(GlyphShift VERSION 0.1.0 LANGUAGES CXX)

# Linux builds every mode (launcher, injected modules, X11 overlay). Windows
# (M7) builds the portable core, configuration, editor and Vulkan pipeline with
# the Windows platform layer; its capture overlay follows.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GLYPHSHIFT_LINUX ON)
elseif(WIN32 AND MSVC)
    set(GLYPHSHIFT_WINDOWS ON)
endif()
if(NOT (GLYPHSHIFT_LINUX OR GLYPHSHIFT_WINDOWS) OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "GlyphShift requires Linux x86-64, or Windows x64 with MSVC")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Keep ImGui's text representation consistent across the shared model, GPU
# backends and vendor sources, including characters outside the BMP.
add_compile_definitions(IMGUI_USE_WCHAR32)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
if(MSVC)
    # Sources are UTF-8; the Windows API is used in its UTF-16 form.
    add_compile_options(/W4 /utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    # The 8 MB main-thread stack Linux gives: a character atlas (about 570 KB)
    # is built on the stack, beyond Windows's default 1 MB with its callers.
    add_link_options(/STACK:8388608)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(GLYPHSHIFT_LINUX)
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
    pkg_check_modules(XCB_INPUT REQUIRED IMPORTED_TARGET xcb xcb-keysyms)
endif()
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_package(Freetype REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)

add_library(glyphshift_imgui STATIC third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp)
set_target_properties(glyphshift_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(MSVC)
    # Vendor sources: no project warning level (MSVC reports /W4 overridden).
    get_target_property(imgui_options glyphshift_imgui COMPILE_OPTIONS)
    list(REMOVE_ITEM imgui_options /W4)
    set_target_properties(glyphshift_imgui PROPERTIES COMPILE_OPTIONS "${imgui_options};/w")
else()
    target_compile_options(glyphshift_imgui PRIVATE -w)
endif()
target_include_directories(glyphshift_imgui SYSTEM PUBLIC third_party/imgui)
# Linux shares one editor session between the injected GL and Vulkan modules,
# so the library is a shared object there; Windows links it into the overlay.
set(editor_sources src/editor/editor.cpp src/config/settings.cpp src/config/presets.cpp
    src/config/characters.cpp src/config/builtin_presets.cpp)
if(GLYPHSHIFT_LINUX)
    add_library(glyphshift_editor SHARED ${editor_sources})
else()
    add_library(glyphshift_editor STATIC ${editor_sources})
endif()
# Embed the shipped styles so the overlay works outside the source checkout.
# Changes to a config trigger reconfiguration; the .conf files remain the source.
set(BUILTIN_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/presets")
file(MAKE_DIRECTORY "${BUILTIN_GENERATED}")
foreach(style comic sepia newsprint chalk ink-edges blueprint midnight crt digital-rain)
    set(preset_source "${CMAKE_CURRENT_SOURCE_DIR}/presets/art-${style}.conf")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${preset_source}")
    file(READ "${preset_source}" PRESET_CONTENT)
    string(REPLACE "-" "_" PRESET_NAME "${style}")
    configure_file(cmake/embed_preset.hpp.in
        "${BUILTIN_GENERATED}/${PRESET_NAME}.hpp" @ONLY)
endforeach()
target_include_directories(glyphshift_editor PRIVATE "${BUILTIN_GENERATED}")
set_target_properties(glyphshift_editor PROPERTIES CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
if(GLYPHSHIFT_LINUX)
    set(editor_exports "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/exports.map")
    target_link_options(glyphshift_editor PRIVATE "LINKER:--version-script=${editor_exports}")
    set_property(TARGET glyphshift_editor APPEND PROPERTY LINK_DEPENDS "${editor_exports}")
endif()
target_include_directories(glyphshift_editor PUBLIC include)
if(GLYPHSHIFT_LINUX)
    # Platform layer, Linux: desktop integration (autostart, notifications),
    # process facilities, XDG paths, font discovery (Fontconfig) and file
    # access. Operating-system interfaces only; the shared editor library
    # links it as well.
    add_library(glyphshift_platform STATIC src/platform/linux/desktop.cpp
        src/platform/linux/process.cpp src/platform/linux/fonts.cpp src/platform/linux/paths.cpp
        src/platform/linux/files.cpp)
    target_link_libraries(glyphshift_platform PRIVATE PkgConfig::FONTCONFIG)
else()
    # Platform layer, Windows: the configuration folder, the executable's
    # path, font discovery (DirectWrite) and file access.
    add_library(glyphshift_platform STATIC src/platform/windows/paths.cpp
        src/platform/windows/process.cpp src/platform/windows/fonts.cpp
        src/platform/windows/files.cpp)
    target_link_libraries(glyphshift_platform PRIVATE dwrite shell32 ole32 advapi32)
endif()
set_target_properties(glyphshift_platform PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(glyphshift_platform PUBLIC include)
target_link_libraries(glyphshift_editor PRIVATE glyphshift_imgui Threads::Threads Freetype::Freetype
    glyphshift_platform)
if(GLYPHSHIFT_LINUX)
# Window-system layer, X11: the editor's input surface (own XCB connection).
# Linked into each module that presents the editor; the first module to give a
# window its input source keeps it, so one window never gets two connections.
add_library(glyphshift_x11_input STATIC src/display/x11/editor_input.cpp)
set_target_properties(glyphshift_x11_input PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(glyphshift_x11_input PRIVATE ${X11_INCLUDE_DIR})
target_link_libraries(glyphshift_x11_input PUBLIC glyphshift_editor PRIVATE PkgConfig::XCB_INPUT)

# Glyph pipeline shared by every GL path: the injected renderer, the capture
# overlay and the preview tool. GL is resolved at run time (no libGL link),
# through the loader of the window system that owns the context.
add_library(glyphshift_gl_pipeline STATIC src/render/opengl/gl_api.cpp
    src/render/opengl/pipeline.cpp src/render/opengl/gpu_timer.cpp
    src/render/opengl/gl_state.cpp src/render/opengl/injected.cpp src/render/opengl/owned.cpp)
set_target_properties(glyphshift_gl_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(glyphshift_gl_pipeline PUBLIC GL_GLEXT_PROTOTYPES)
target_include_directories(glyphshift_gl_pipeline SYSTEM PRIVATE third_party/imgui)
target_include_directories(glyphshift_gl_pipeline PUBLIC ${OPENGL_INCLUDE_DIR})
target_link_libraries(glyphshift_gl_pipeline PUBLIC ${CMAKE_DL_LIBS} glyphshift_core
    glyphshift_editor)

# The injected OpenGL module (Linux): GLX and Xlib interposition (X11 window
# system) around the renderer's InjectedRenderer.
add_library(glyphshift_gl SHARED src/display/x11/glx_interpose.cpp
    src/display/x11/glx_renderer.cpp src/display/x11/xlib_interpose.cpp)
target_compile_definitions(glyphshift_gl PRIVATE GL_GLEXT_PROTOTYPES)
set_target_properties(glyphshift_gl PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(glyphshift_gl SYSTEM PRIVATE third_party/imgui)
target_include_directories(glyphshift_gl PRIVATE ${X11_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})
target_link_libraries(glyphshift_gl PRIVATE ${CMAKE_DL_LIBS} glyphshift_core glyphshift_editor
    glyphshift_gl_pipeline glyphshift_x11_input Threads::Threads)
set(opengl_exports "${CMAKE_CURRENT_SOURCE_DIR}/src/display/x11/glx_exports.map")
target_link_options(glyphshift_gl PRIVATE "LINKER:--version-script=${opengl_exports}")
set_property(TARGET glyphshift_gl APPEND PROPERTY LINK_DEPENDS "${opengl_exports}")
endif()  # GLYPHSHIFT_LINUX
# The Vulkan glyph pipeline, independent of the loader layer: device-function
# table, resources, analysis/composition passes and editor drawing.
add_library(glyphshift_vk_pipeline STATIC src/render/vulkan/resources.cpp
    src/render/vulkan/pipeline.cpp)
set_target_properties(glyphshift_vk_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(glyphshift_vk_pipeline SYSTEM PRIVATE third_party/imgui)
target_include_directories(glyphshift_vk_pipeline PUBLIC ${Vulkan_INCLUDE_DIRS})
target_link_libraries(glyphshift_vk_pipeline PUBLIC glyphshift_core glyphshift_editor)
# Vulkan rendering in a device and window this process owns (the capture
# overlay), linked to the loader; kept apart from the pipeline library, which
# the Linux layer uses without the loader. Windows adds the import of captured
# D3D11 textures.
add_library(glyphshift_vk_owned STATIC src/render/vulkan/owned.cpp)
target_include_directories(glyphshift_vk_owned SYSTEM PRIVATE third_party/imgui)
target_link_libraries(glyphshift_vk_owned PUBLIC glyphshift_vk_pipeline Vulkan::Vulkan)
if(GLYPHSHIFT_WINDOWS)
    target_sources(glyphshift_vk_owned PRIVATE src/render/vulkan/win32_import.cpp)
    target_compile_definitions(glyphshift_vk_owned PUBLIC VK_USE_PLATFORM_WIN32_KHR)
endif()
if(GLYPHSHIFT_LINUX)
# The Vulkan loader layer (Linux injection module) around that pipeline.
add_library(glyphshift_vk SHARED
    src/render/vulkan/layer.cpp src/render/vulkan/dispatch.cpp src/render/vulkan/renderer.cpp)
set_target_properties(glyphshift_vk PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(glyphshift_vk SYSTEM PRIVATE third_party/imgui)
target_compile_definitions(glyphshift_vk PRIVATE VK_USE_PLATFORM_XLIB_KHR)
target_include_directories(glyphshift_vk PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(glyphshift_vk PRIVATE Threads::Threads glyphshift_core glyphshift_editor
    glyphshift_vk_pipeline glyphshift_x11_input)
set(vulkan_exports "${CMAKE_CURRENT_SOURCE_DIR}/src/render/vulkan/exports.map")
target_link_options(glyphshift_vk PRIVATE "LINKER:--version-script=${vulkan_exports}")
set_property(TARGET glyphshift_vk APPEND PROPERTY LINK_DEPENDS "${vulkan_exports}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/layers")
configure_file(layers/VK_LAYER_GLYPHSHIFT_overlay.json.in
    "${CMAKE_CURRENT_BINARY_DIR}/layers/VK_LAYER_GLYPHSHIFT_overlay.json" @ONLY)
add_executable(glyphshift apps/launcher/main.cpp)
target_include_directories(glyphshift PRIVATE include)
add_dependencies(glyphshift glyphshift_gl glyphshift_vk)
endif()  # GLYPHSHIFT_LINUX

# Keep the development compilation database at the checkout root for LSPs.
# Release builds never replace it, and existing user-owned files are preserved.
option(GLYPHSHIFT_LINK_COMPILE_COMMANDS "Link the build's database at the source root" OFF)
if(GLYPHSHIFT_LINK_COMPILE_COMMANDS)
    set(database_link "${CMAKE_SOURCE_DIR}/compile_commands.json")
    set(database_target "${CMAKE_BINARY_DIR}/compile_commands.json")
    if(IS_SYMLINK "${database_link}")
        file(READ_SYMLINK "${database_link}" existing_database)
        if(NOT existing_database STREQUAL database_target)
            message(WARNING "Preserving existing compilation database link: ${database_link}")
        endif()
    elseif(EXISTS "${database_link}")
        message(WARNING "Preserving existing compilation database: ${database_link}")
    else()
        file(CREATE_LINK "${database_target}" "${database_link}" SYMBOLIC RESULT database_result)
        if(NOT database_result STREQUAL "0")
            message(FATAL_ERROR "Cannot link compilation database: ${database_result}")
        endif()
    endif()
endif()

add_library(glyphshift_core STATIC src/core/fixture.cpp src/core/glyph.cpp)
set_target_properties(glyphshift_core PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(glyphshift_core PUBLIC include)
if(GLYPHSHIFT_LINUX)
    add_library(fixture_window STATIC tests/fixtures/window.cpp tests/fixtures/common.cpp)
    target_include_directories(fixture_window PUBLIC tests/fixtures)
    target_link_libraries(fixture_window PUBLIC glyphshift_core X11::X11 ${CMAKE_DL_LIBS})
else()
    # The Vulkan fixture's Win32 window (M7): the capture overlay's test target.
    add_library(fixture_window STATIC tests/fixtures/window_win32.cpp tests/fixtures/common.cpp)
    target_include_directories(fixture_window PUBLIC tests/fixtures)
    target_link_libraries(fixture_window PUBLIC glyphshift_core user32 dwmapi)
    add_executable(glyphshift-vulkan-fixture tests/fixtures/vulkan.cpp)
    target_link_libraries(glyphshift-vulkan-fixture PRIVATE fixture_window Vulkan::Vulkan)
    add_dependencies(glyphshift-vulkan-fixture fixture_shaders)
    target_compile_definitions(glyphshift-vulkan-fixture PRIVATE
        FIXTURE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/fixture-shaders")
    if(BUILD_TESTING)
        add_test(NAME vulkan-help COMMAND glyphshift-vulkan-fixture --help)
    endif()
    # Window-system layer, Win32 (M7): Windows.Graphics.Capture of the target,
    # the input-transparent overlay window and target tracking.
    add_library(glyphshift_win32_overlay STATIC src/display/win32/capture.cpp
        src/display/win32/overlay_window.cpp src/display/win32/tracker.cpp)
    target_link_libraries(glyphshift_win32_overlay PUBLIC user32 PRIVATE d3d11 dxgi dwmapi
        windowsapp)
    # The capture overlay: the Win32 window system, the Windows platform layer
    # and the Vulkan renderer.
    add_executable(glyphshift-overlay apps/overlay/main_win32.cpp apps/overlay/options.cpp)
    target_link_libraries(glyphshift-overlay PRIVATE glyphshift_vk_owned glyphshift_platform
        glyphshift_win32_overlay)
endif()

# Compile the very same gradient source for the two APIs. Files are read at
# startup by these development fixtures; no installation is required for M0.
set(FIXTURE_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixture-shaders")
file(MAKE_DIRECTORY "${FIXTURE_SHADER_DIR}")
foreach(stage vert frag)
    configure_file(shaders/fixtures/gradient.${stage}
                   "${FIXTURE_SHADER_DIR}/gradient.${stage}" COPYONLY)
    add_custom_command(OUTPUT "${FIXTURE_SHADER_DIR}/gradient.${stage}.spv"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fixtures/gradient.${stage}"
                -o "${FIXTURE_SHADER_DIR}/gradient.${stage}.spv"
        DEPENDS "shaders/fixtures/gradient.${stage}" VERBATIM)
    list(APPEND FIXTURE_SPIRV "${FIXTURE_SHADER_DIR}/gradient.${stage}.spv")
endforeach()
add_custom_target(fixture_shaders DEPENDS ${FIXTURE_SPIRV})

set(GLYPH_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GLYPH_GENERATED}")
file(READ shaders/glyph/glyph.vert GLYPH_VERTEX)
file(READ shaders/glyph/glyph.frag GLYPH_FRAGMENT)
# Further passes are the same source with one define: per-cell analysis, the
# exposure meter, and the anchored grid's motion search, motion resolve and
# luminance copy.
set(GLYPH_VARIANTS analysis meter motion resolve luma)
foreach(variant ${GLYPH_VARIANTS})
    string(TOUPPER "${variant}" define)
    string(REPLACE "#version 330 core\n" "#version 330 core\n#define ${define}\n"
        "GLYPH_${define}" "${GLYPH_FRAGMENT}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    shaders/glyph/glyph.vert shaders/glyph/glyph.frag)
file(READ shaders/editor/editor.vert EDITOR_VERTEX)
file(READ shaders/editor/editor.frag EDITOR_FRAGMENT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    shaders/editor/editor.vert shaders/editor/editor.frag)
configure_file(cmake/glyph_shaders.hpp.in "${GLYPH_GENERATED}/glyph_shaders.hpp" @ONLY)
foreach(stage vert frag)
    add_custom_command(OUTPUT "${GLYPH_GENERATED}/glyph_${stage}_spv.hpp"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/glyph/glyph.${stage}"
            -o "${GLYPH_GENERATED}/glyph.${stage}.spv"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${GLYPH_GENERATED}/glyph.${stage}.spv"
            "-DOUTPUT=${GLYPH_GENERATED}/glyph_${stage}_spv.hpp" "-DNAME=glyph_${stage}_spv"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
        DEPENDS "shaders/glyph/glyph.${stage}" cmake/embed_spirv.cmake VERBATIM)
    target_sources(glyphshift_vk_pipeline PRIVATE "${GLYPH_GENERATED}/glyph_${stage}_spv.hpp")
endforeach()
foreach(variant ${GLYPH_VARIANTS})
    string(TOUPPER "${variant}" define)
    add_custom_command(OUTPUT "${GLYPH_GENERATED}/glyph_${variant}_spv.hpp"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0 -D${define}
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/glyph/glyph.frag"
            -o "${GLYPH_GENERATED}/glyph.${variant}.spv"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${GLYPH_GENERATED}/glyph.${variant}.spv"
            "-DOUTPUT=${GLYPH_GENERATED}/glyph_${variant}_spv.hpp" "-DNAME=glyph_${variant}_spv"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
        DEPENDS "shaders/glyph/glyph.frag" cmake/embed_spirv.cmake VERBATIM)
    target_sources(glyphshift_vk_pipeline PRIVATE "${GLYPH_GENERATED}/glyph_${variant}_spv.hpp")
endforeach()
foreach(stage vert frag)
    add_custom_command(OUTPUT "${GLYPH_GENERATED}/editor_${stage}_spv.hpp"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/editor/editor.${stage}"
            -o "${GLYPH_GENERATED}/editor.${stage}.spv"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${GLYPH_GENERATED}/editor.${stage}.spv"
            "-DOUTPUT=${GLYPH_GENERATED}/editor_${stage}_spv.hpp" "-DNAME=editor_${stage}_spv"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
        DEPENDS "shaders/editor/editor.${stage}" cmake/embed_spirv.cmake VERBATIM)
    target_sources(glyphshift_vk_pipeline PRIVATE "${GLYPH_GENERATED}/editor_${stage}_spv.hpp")
endforeach()
target_include_directories(glyphshift_vk_pipeline PRIVATE "${GLYPH_GENERATED}")
if(GLYPHSHIFT_LINUX)
target_include_directories(glyphshift_gl PRIVATE "${GLYPH_GENERATED}")
target_include_directories(glyphshift_gl_pipeline PRIVATE "${GLYPH_GENERATED}")
# M6 capture overlay: reads a chosen window through X Composite and draws the
# glyph effect in its own input-transparent window; it never enters the game.
pkg_check_modules(X11_CAPTURE REQUIRED IMPORTED_TARGET xcomposite xdamage xfixes)
# Window-system layer, X11: system tray icon and menu (XEmbed, Xft text).
pkg_check_modules(XFT REQUIRED IMPORTED_TARGET xft)
add_library(glyphshift_x11_tray STATIC src/display/x11/tray.cpp)
target_link_libraries(glyphshift_x11_tray PUBLIC X11::X11 PRIVATE PkgConfig::XFT)
# Window-system layer, X11: the overlay's capture (Composite, XDamage,
# texture_from_pixmap), its click-through window, target tracking and the
# daemon's global hotkey.
add_library(glyphshift_x11_overlay STATIC src/display/x11/capture.cpp
    src/display/x11/overlay_window.cpp src/display/x11/tracker.cpp
    src/display/x11/global_hotkey.cpp)
target_link_libraries(glyphshift_x11_overlay PUBLIC X11::X11 OpenGL::GL
    PRIVATE PkgConfig::X11_CAPTURE)
add_executable(glyphshift-overlay apps/overlay/main.cpp apps/overlay/daemon.cpp
    apps/overlay/options.cpp)
target_link_libraries(glyphshift-overlay PRIVATE glyphshift_gl_pipeline glyphshift_platform
    glyphshift_x11_input glyphshift_x11_overlay glyphshift_x11_tray)

add_executable(glyphshift-glx-fixture tests/fixtures/glx.cpp)
target_link_libraries(glyphshift-glx-fixture PRIVATE fixture_window OpenGL::GL)
add_executable(glyphshift-vulkan-fixture tests/fixtures/vulkan.cpp)
target_link_libraries(glyphshift-vulkan-fixture PRIVATE fixture_window Vulkan::Vulkan)
add_dependencies(glyphshift-vulkan-fixture fixture_shaders)
foreach(target glyphshift-glx-fixture glyphshift-vulkan-fixture)
    target_compile_definitions(${target} PRIVATE FIXTURE_SHADER_DIR="${FIXTURE_SHADER_DIR}")
endforeach()
endif()  # GLYPHSHIFT_LINUX

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(CLANG_FORMAT clang-format)
    if(CLANG_FORMAT)
        add_test(NAME format-hook COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/format_test.py"
            "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    # Portable logic tests: every platform.
    add_executable(glyphshift-art-tests tests/unit/art_test.cpp)
    target_link_libraries(glyphshift-art-tests PRIVATE glyphshift_core glyphshift_editor)
    add_test(NAME illustration COMMAND glyphshift-art-tests)
    add_executable(glyphshift-character-tests tests/unit/characters_test.cpp)
    target_link_libraries(glyphshift-character-tests PRIVATE glyphshift_editor)
    add_test(NAME characters COMMAND glyphshift-character-tests)
    add_executable(glyphshift-presets-tests tests/unit/presets_test.cpp)
    target_link_libraries(glyphshift-presets-tests PRIVATE glyphshift_editor Threads::Threads)
    add_test(NAME presets COMMAND glyphshift-presets-tests)
    add_executable(glyphshift-editor-tests tests/unit/editor_test.cpp)
    target_include_directories(glyphshift-editor-tests SYSTEM PRIVATE third_party/imgui)
    target_link_libraries(glyphshift-editor-tests PRIVATE glyphshift_editor Threads::Threads)
    add_test(NAME editor COMMAND glyphshift-editor-tests)
    add_executable(glyphshift-glyph-tests tests/unit/glyph_test.cpp)
    target_link_libraries(glyphshift-glyph-tests PRIVATE glyphshift_core glyphshift_editor Threads::Threads)
    add_test(NAME glyph-semantics COMMAND glyphshift-glyph-tests)
    add_executable(glyphshift-core-tests tests/unit/fixture_test.cpp)
    target_link_libraries(glyphshift-core-tests PRIVATE glyphshift_core)
    add_test(NAME core COMMAND glyphshift-core-tests)
    # Offscreen preview through the Vulkan glyph pipeline, for the image
    # oracles: glyphshift-preview on Windows, next to the GL one on Linux.
    if(GLYPHSHIFT_LINUX)
        set(vulkan_preview glyphshift-vulkan-preview)
    else()
        set(vulkan_preview glyphshift-preview)
    endif()
    add_executable(${vulkan_preview} tools/vulkan_preview.cpp)
    target_link_libraries(${vulkan_preview} PRIVATE glyphshift_vk_pipeline Vulkan::Vulkan)
    add_executable(glyphshift-platform-files-tests tests/unit/platform_files_test.cpp)
    target_link_libraries(glyphshift-platform-files-tests PRIVATE glyphshift_platform)
    add_test(NAME platform-files COMMAND glyphshift-platform-files-tests)
    if(GLYPHSHIFT_WINDOWS)
        target_link_libraries(glyphshift-platform-files-tests PRIVATE advapi32)
        add_executable(glyphshift-platform-windows-tests tests/unit/platform_windows_test.cpp)
        target_link_libraries(glyphshift-platform-windows-tests PRIVATE glyphshift_platform shell32
            ole32)
        add_test(NAME platform-windows COMMAND glyphshift-platform-windows-tests)
        target_link_libraries(glyphshift-presets-tests PRIVATE advapi32 shell32 ole32)
        # MSVC Debug gives each character atlas these tests build (about
        # 570 KB) its own stack slot in main.
        foreach(test glyphshift-art-tests glyphshift-character-tests glyphshift-presets-tests)
            target_link_options(${test} PRIVATE /STACK:33554432)
        endforeach()
    endif()
endif()
# Linux tests: launcher, injected modules, X11, Linux platform and install.
if(BUILD_TESTING AND GLYPHSHIFT_LINUX)
    add_executable(glyphshift-launcher-probe tests/unit/launcher_probe.cpp)
    add_test(NAME launcher COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/launcher_test.py"
        "$<TARGET_FILE:glyphshift>" "$<TARGET_FILE:glyphshift-launcher-probe>")
    add_executable(glyphshift-preview tools/preview.cpp)
    target_link_libraries(glyphshift-preview PRIVATE glyphshift_gl_pipeline OpenGL::GL X11::X11)
    add_library(glyphshift-fault-glx SHARED tests/fixtures/fault_glx.cpp)
    target_link_libraries(glyphshift-fault-glx PRIVATE ${CMAKE_DL_LIBS})
    target_include_directories(glyphshift-fault-glx PRIVATE ${OPENGL_INCLUDE_DIR} ${X11_INCLUDE_DIR})
    add_library(glyphshift-fake-glx SHARED tests/unit/fake_glx.cpp)
    target_include_directories(glyphshift-fake-glx PRIVATE ${X11_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})
    target_link_libraries(glyphshift-fake-glx PRIVATE ${CMAKE_DL_LIBS})
    add_executable(glyphshift-interpose-tests tests/unit/interpose_test.cpp)
    target_include_directories(glyphshift-interpose-tests PRIVATE ${X11_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})
    target_link_libraries(glyphshift-interpose-tests PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    add_test(NAME interpose COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/interpose_test.py"
        "$<TARGET_FILE:glyphshift-interpose-tests>" "$<TARGET_FILE:glyphshift_gl>"
        "$<TARGET_FILE:glyphshift-fake-glx>")
    add_executable(glyphshift-vulkan-layer-tests tests/unit/vulkan_layer_test.cpp)
    target_include_directories(glyphshift-vulkan-layer-tests PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(glyphshift-vulkan-layer-tests PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    target_link_options(glyphshift-vulkan-layer-tests PRIVATE -Wl,--export-dynamic)
    add_test(NAME vulkan-layer COMMAND glyphshift-vulkan-layer-tests "$<TARGET_FILE:glyphshift_vk>")
    add_executable(glyphshift-x11-keys-tests tests/unit/x11_keys_test.cpp)
    target_link_libraries(glyphshift-x11-keys-tests PRIVATE glyphshift_editor)
    add_test(NAME x11-keys COMMAND glyphshift-x11-keys-tests)
    add_executable(glyphshift-platform-tests tests/unit/platform_test.cpp)
    target_link_libraries(glyphshift-platform-tests PRIVATE glyphshift_platform)
    add_test(NAME platform COMMAND glyphshift-platform-tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME install COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/install_test.py"
            "${CMAKE_COMMAND}" "${CMAKE_BINARY_DIR}")
    endif()
    add_test(NAME glx-help COMMAND glyphshift-glx-fixture --help)
    add_test(NAME vulkan-help COMMAND glyphshift-vulkan-fixture --help)
endif()

if(GLYPHSHIFT_LINUX)
# Installation: the launcher, overlay, libraries and layer manifest find each
# other through $ORIGIN, so they stay together in a private directory, with
# links in bin/. The desktop entry starts the background overlay.
include(GNUInstallDirs)
set(GLYPHSHIFT_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/glyphshift")
set_target_properties(glyphshift glyphshift-overlay glyphshift_gl glyphshift_vk glyphshift_editor
    PROPERTIES INSTALL_RPATH "$ORIGIN")
install(TARGETS glyphshift glyphshift-overlay glyphshift_gl glyphshift_vk glyphshift_editor
    RUNTIME DESTINATION "${GLYPHSHIFT_INSTALL_DIR}"
    LIBRARY DESTINATION "${GLYPHSHIFT_INSTALL_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/layers/VK_LAYER_GLYPHSHIFT_overlay.json"
    DESTINATION "${GLYPHSHIFT_INSTALL_DIR}/layers")
foreach(program glyphshift glyphshift-overlay)
    install(CODE "
        set(link \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${program}\")
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        file(REMOVE \"\${link}\")
        file(CREATE_LINK \"../${GLYPHSHIFT_INSTALL_DIR}/${program}\" \"\${link}\" SYMBOLIC)
        message(STATUS \"Installing: \${link}\")")
endforeach()
configure_file(cmake/install_desktop.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/install_desktop.cmake"
    @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install_desktop.cmake")
install(FILES data/glyphshift.svg
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
# MIT notices for GlyphShift and the vendored Dear ImGui, kept with every copy.
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/glyphshift")
install(FILES third_party/imgui/LICENSE.txt DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/glyphshift"
    RENAME LICENSE.imgui.txt)

endif()  # GLYPHSHIFT_LINUX

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_target(format
        COMMAND "${Python3_EXECUTABLE}" tools/format.py --apply
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
    add_custom_target(format-check
        COMMAND "${Python3_EXECUTABLE}" tools/format.py --check
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
endif()

option(GLYPHSHIFT_GPU_SMOKE_TESTS "Enable X11/GPU smoke tests in CTest" OFF)
if(BUILD_TESTING AND Python3_Interpreter_FOUND AND GLYPHSHIFT_WINDOWS)
    # Windows graphics smoke (M7): the Win32 Vulkan fixture, then the overlay,
    # and the image oracles through the Vulkan preview.
    add_custom_target(smoke-windows
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/smoke/windows_run.py"
            --build "${CMAKE_BINARY_DIR}"
        DEPENDS glyphshift-vulkan-fixture glyphshift-preview
        USES_TERMINAL VERBATIM)
    if(GLYPHSHIFT_GPU_SMOKE_TESTS)
        add_test(NAME smoke-windows COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/smoke/windows_run.py" --build "${CMAKE_BINARY_DIR}")
        set_tests_properties(smoke-windows PROPERTIES LABELS "graphics" RUN_SERIAL TRUE
            TIMEOUT 180 SKIP_RETURN_CODE 77)
    endif()
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND AND GLYPHSHIFT_LINUX)
    foreach(api glx vulkan)
        add_custom_target(smoke-${api}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/smoke/run.py"
                --build "${CMAKE_BINARY_DIR}" --api ${api}
            DEPENDS glyphshift glyphshift-${api}-fixture glyphshift-fault-glx glyphshift-preview glyphshift-overlay
            USES_TERMINAL VERBATIM)
        if(GLYPHSHIFT_GPU_SMOKE_TESTS)
            add_test(NAME smoke-${api} COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/smoke/run.py" --build "${CMAKE_BINARY_DIR}" --api ${api})
            set_tests_properties(smoke-${api} PROPERTIES LABELS "graphics" RUN_SERIAL TRUE
                TIMEOUT 180 SKIP_RETURN_CODE 77)
        endif()
    endforeach()
    add_custom_target(smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/smoke/run.py" --build "${CMAKE_BINARY_DIR}" --api glx
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/smoke/run.py" --build "${CMAKE_BINARY_DIR}" --api vulkan
        DEPENDS glyphshift glyphshift-glx-fixture glyphshift-vulkan-fixture glyphshift-fault-glx glyphshift-preview glyphshift-overlay
        USES_TERMINAL VERBATIM)
endif()
