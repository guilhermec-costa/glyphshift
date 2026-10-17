cmake_minimum_required(VERSION 3.20)
project(GlyphShift VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "GlyphShift currently requires Linux x86-64")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)

find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

add_library(glyphshift_gl SHARED src/render/opengl/glx_interpose.cpp)
set_target_properties(glyphshift_gl PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(glyphshift_gl PRIVATE ${X11_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})
target_link_libraries(glyphshift_gl PRIVATE ${CMAKE_DL_LIBS})
add_library(glyphshift_vk SHARED
    src/render/vulkan/layer.cpp src/render/vulkan/dispatch.cpp)
set_target_properties(glyphshift_vk PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(glyphshift_vk PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(glyphshift_vk PRIVATE Threads::Threads)
set(vulkan_exports "${CMAKE_CURRENT_SOURCE_DIR}/src/render/vulkan/exports.map")
target_link_options(glyphshift_vk PRIVATE "LINKER:--version-script=${vulkan_exports}")
set_property(TARGET glyphshift_vk APPEND PROPERTY LINK_DEPENDS "${vulkan_exports}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/layers")
configure_file(layers/VK_LAYER_GLYPHSHIFT_overlay.json.in
    "${CMAKE_CURRENT_BINARY_DIR}/layers/VK_LAYER_GLYPHSHIFT_overlay.json" @ONLY)
add_executable(glyphshift apps/launcher/main.cpp)
add_dependencies(glyphshift glyphshift_gl glyphshift_vk)

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

add_library(glyphshift_core STATIC src/core/fixture.cpp)
target_include_directories(glyphshift_core PUBLIC include)
add_library(fixture_window STATIC tests/fixtures/window.cpp)
target_include_directories(fixture_window PUBLIC tests/fixtures)
target_link_libraries(fixture_window PUBLIC glyphshift_core X11::X11)

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

add_executable(glyphshift-glx-fixture tests/fixtures/glx.cpp)
target_link_libraries(glyphshift-glx-fixture PRIVATE fixture_window OpenGL::GL)
add_executable(glyphshift-vulkan-fixture tests/fixtures/vulkan.cpp)
target_link_libraries(glyphshift-vulkan-fixture PRIVATE fixture_window Vulkan::Vulkan)
add_dependencies(glyphshift-vulkan-fixture fixture_shaders)
foreach(target glyphshift-glx-fixture glyphshift-vulkan-fixture)
    target_compile_definitions(${target} PRIVATE FIXTURE_SHADER_DIR="${FIXTURE_SHADER_DIR}")
endforeach()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(CLANG_FORMAT clang-format)
    if(CLANG_FORMAT)
        add_test(NAME format-hook COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/format_test.py"
            "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    add_executable(glyphshift-launcher-probe tests/unit/launcher_probe.cpp)
    add_test(NAME launcher COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/launcher_test.py"
        "$<TARGET_FILE:glyphshift>" "$<TARGET_FILE:glyphshift-launcher-probe>")
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
    add_executable(glyphshift-core-tests tests/unit/fixture_test.cpp)
    target_link_libraries(glyphshift-core-tests PRIVATE glyphshift_core)
    add_test(NAME core COMMAND glyphshift-core-tests)
    add_test(NAME glx-help COMMAND glyphshift-glx-fixture --help)
    add_test(NAME vulkan-help COMMAND glyphshift-vulkan-fixture --help)
endif()

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_target(format
        COMMAND "${Python3_EXECUTABLE}" tools/format.py --apply
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
    add_custom_target(format-check
        COMMAND "${Python3_EXECUTABLE}" tools/format.py --check
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
endif()
