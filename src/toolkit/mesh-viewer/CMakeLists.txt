cmake_minimum_required(VERSION 3.10)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
get_filename_component(NDEVC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(NDEVC_SRC_ROOT "${NDEVC_ROOT}/src")

add_definitions(-DSOURCE_DIR=\"${NDEVC_ROOT}\")

if(NOT TARGET ndevc_core)
    add_library(glad STATIC ${NDEVC_ROOT}/libs/glad/src/glad.c)
    target_include_directories(glad PUBLIC
        ${NDEVC_ROOT}/libs/glad/include
        ${NDEVC_ROOT}/libs/glm
    )

    set(NDEVC_GLFW_TARGET "")
    if(WIN32)
        set(NDEVC_BUNDLED_GLFW_DIR ${NDEVC_ROOT}/libs/glfw/lib-vc2022)
        set(NDEVC_BUNDLED_GLFW_DLL ${NDEVC_BUNDLED_GLFW_DIR}/glfw3.dll)
        set(NDEVC_BUNDLED_GLFW_IMPLIB_DLL ${NDEVC_BUNDLED_GLFW_DIR}/glfw3dll.lib)
        set(NDEVC_BUNDLED_GLFW_IMPLIB_STD ${NDEVC_BUNDLED_GLFW_DIR}/glfw3.lib)
        if(EXISTS ${NDEVC_BUNDLED_GLFW_DLL} AND EXISTS ${NDEVC_BUNDLED_GLFW_IMPLIB_DLL})
            add_library(ndevc_glfw SHARED IMPORTED)
            set_target_properties(ndevc_glfw PROPERTIES
                IMPORTED_IMPLIB ${NDEVC_BUNDLED_GLFW_IMPLIB_DLL}
                IMPORTED_LOCATION ${NDEVC_BUNDLED_GLFW_DLL}
            )
            target_include_directories(ndevc_glfw INTERFACE ${NDEVC_ROOT}/libs/glfw/include)
            set(NDEVC_GLFW_TARGET ndevc_glfw)
        elseif(EXISTS ${NDEVC_BUNDLED_GLFW_DLL} AND EXISTS ${NDEVC_BUNDLED_GLFW_IMPLIB_STD})
            add_library(ndevc_glfw SHARED IMPORTED)
            set_target_properties(ndevc_glfw PROPERTIES
                IMPORTED_IMPLIB ${NDEVC_BUNDLED_GLFW_IMPLIB_STD}
                IMPORTED_LOCATION ${NDEVC_BUNDLED_GLFW_DLL}
            )
            target_include_directories(ndevc_glfw INTERFACE ${NDEVC_ROOT}/libs/glfw/include)
            set(NDEVC_GLFW_TARGET ndevc_glfw)
        endif()
    endif()

    if(NOT NDEVC_GLFW_TARGET)
        find_package(glfw3 CONFIG QUIET)
        if(TARGET glfw)
            set(NDEVC_GLFW_TARGET glfw)
        elseif(TARGET glfw3)
            set(NDEVC_GLFW_TARGET glfw3)
        else()
            message(FATAL_ERROR "[PORTABILITY][ERROR] GLFW not found. Install glfw3 or provide bundled libs/glfw.")
        endif()
    endif()

    option(NDEVC_ENABLE_DIRECTXTEX "Enable DirectXTex DDS loading support" ON)
    set(NDEVC_HAS_DIRECTXTEX 0)
    set(NDEVC_DIRECTXTEX_TARGET "")
    if(NDEVC_ENABLE_DIRECTXTEX AND WIN32)
        set(NDEVC_DXT_DEBUG_LIB ${NDEVC_ROOT}/libs/directxtex/debug/lib/DirectXTex.lib)
        set(NDEVC_DXT_RELEASE_LIB ${NDEVC_ROOT}/libs/directxtex/release/lib/DirectXTex.lib)
        set(NDEVC_DXT_INCLUDE_DIR ${NDEVC_ROOT}/libs/directxtex/release/include)
        if(NOT EXISTS ${NDEVC_DXT_INCLUDE_DIR})
            set(NDEVC_DXT_INCLUDE_DIR ${NDEVC_ROOT}/libs/directxtex/debug/include)
        endif()
        if(EXISTS ${NDEVC_DXT_DEBUG_LIB} AND EXISTS ${NDEVC_DXT_RELEASE_LIB} AND EXISTS ${NDEVC_DXT_INCLUDE_DIR})
            add_library(ndevc_directxtex STATIC IMPORTED)
            set_target_properties(ndevc_directxtex PROPERTIES
                IMPORTED_CONFIGURATIONS "DEBUG;RELEASE;RELWITHDEBINFO;MINSIZEREL"
                IMPORTED_LOCATION_DEBUG ${NDEVC_DXT_DEBUG_LIB}
                IMPORTED_LOCATION_RELEASE ${NDEVC_DXT_RELEASE_LIB}
                IMPORTED_LOCATION_RELWITHDEBINFO ${NDEVC_DXT_RELEASE_LIB}
                IMPORTED_LOCATION_MINSIZEREL ${NDEVC_DXT_RELEASE_LIB}
                INTERFACE_INCLUDE_DIRECTORIES ${NDEVC_DXT_INCLUDE_DIR}
            )
            set(NDEVC_DIRECTXTEX_TARGET ndevc_directxtex)
            set(NDEVC_HAS_DIRECTXTEX 1)
        else()
            message(WARNING "[PORTABILITY][ERROR] DirectXTex artifacts not found; DDS loading disabled.")
        endif()
    elseif(NDEVC_ENABLE_DIRECTXTEX)
        message(WARNING "[PORTABILITY][ERROR] DirectXTex is Windows-only in this build; DDS loading disabled.")
    endif()

    add_library(ndevc_core STATIC
        ${NDEVC_SRC_ROOT}/Rendering.cpp
        ${NDEVC_SRC_ROOT}/Shader.cpp
        ${NDEVC_SRC_ROOT}/ShaderManager.cpp
        ${NDEVC_SRC_ROOT}/FrameGraph.cpp
        ${NDEVC_SRC_ROOT}/Mesh.cpp
        ${NDEVC_SRC_ROOT}/optional.cpp
        ${NDEVC_SRC_ROOT}/Parser.cpp
        ${NDEVC_SRC_ROOT}/Model/Model.cpp
        ${NDEVC_SRC_ROOT}/Servers/TextureServer.cpp
        ${NDEVC_SRC_ROOT}/DrawBatchSystem.cpp
        ${NDEVC_SRC_ROOT}/MegaBuffer.cpp
        ${NDEVC_SRC_ROOT}/Map/MapLoader.cpp
    )

    set_property(TARGET ndevc_core PROPERTY CXX_STANDARD 20)
    target_compile_definitions(ndevc_core PRIVATE LIBDATA_EXPORTS)
    target_include_directories(ndevc_core PUBLIC
        ${NDEVC_ROOT}
        ${NDEVC_SRC_ROOT}
        ${NDEVC_ROOT}/libs/imgui-1.92.5
        ${NDEVC_ROOT}/libs/imgui-1.92.5/backends
    )
    target_link_libraries(ndevc_core PUBLIC
        glad
        ${NDEVC_GLFW_TARGET}
    )
    if(NDEVC_DIRECTXTEX_TARGET)
        target_link_libraries(ndevc_core PUBLIC ${NDEVC_DIRECTXTEX_TARGET})
    endif()
    target_compile_definitions(ndevc_core PUBLIC NDEVC_HAS_DIRECTXTEX=${NDEVC_HAS_DIRECTXTEX})
endif()

add_executable(mesh-viewer
        main.cpp
        MeshViewer.h
        MeshViewer.cpp
        ${NDEVC_ROOT}/libs/imgui-1.92.5/imgui.cpp
        ${NDEVC_ROOT}/libs/imgui-1.92.5/imgui_draw.cpp
        ${NDEVC_ROOT}/libs/imgui-1.92.5/imgui_tables.cpp
        ${NDEVC_ROOT}/libs/imgui-1.92.5/imgui_widgets.cpp
        ${NDEVC_ROOT}/libs/imgui-1.92.5/backends/imgui_impl_glfw.cpp
        ${NDEVC_ROOT}/libs/imgui-1.92.5/backends/imgui_impl_opengl3.cpp
)

set_property(TARGET mesh-viewer PROPERTY CXX_STANDARD 20)
target_link_libraries(mesh-viewer PRIVATE ndevc_core)
target_compile_definitions(mesh-viewer PRIVATE LIBDATA_EXPORTS)

if(WIN32 AND EXISTS ${NDEVC_ROOT}/libs/glfw/lib-vc2022/glfw3.dll)
    add_custom_command(TARGET mesh-viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${NDEVC_ROOT}/libs/glfw/lib-vc2022/glfw3.dll
            $<TARGET_FILE_DIR:mesh-viewer>/glfw3.dll
    )
endif()

target_include_directories(mesh-viewer PRIVATE
    ${NDEVC_ROOT}
    ${NDEVC_SRC_ROOT}
    ${NDEVC_ROOT}/libs/glad/include
    ${NDEVC_ROOT}/libs/glm
    ${NDEVC_ROOT}/libs/glfw/include
    ${NDEVC_ROOT}/libs/imgui-1.92.5
    ${NDEVC_ROOT}/libs/imgui-1.92.5/backends
)

target_compile_definitions(mesh-viewer PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)

target_link_libraries(mesh-viewer PRIVATE ndevc_core)
