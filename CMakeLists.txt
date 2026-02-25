cmake_minimum_required(VERSION 3.10)
project(NDEVC LANGUAGES C CXX)

option(BUILD_TOOLS "Build toolkit applications" ON)
option(BUILD_MESH_VIEWER "Build mesh viewer tool" ON)
option(BUILD_MODEL_VIEWER "Build model viewer tool" ON)
option(BUILD_PARTICLE_EDITOR "Build particle editor tool" ON)

add_definitions(-DSOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")

add_library(glad STATIC libs/glad/src/glad.c)

target_include_directories(glad PUBLIC
        ${CMAKE_SOURCE_DIR}/libs/glad/include
        ${CMAKE_SOURCE_DIR}/libs/glm
)

set(NDEVC_GLFW_TARGET "")
if(WIN32)
    set(NDEVC_BUNDLED_GLFW_DIR ${CMAKE_SOURCE_DIR}/libs/glfw/lib-vc2022)
    set(NDEVC_BUNDLED_GLFW_DLL ${NDEVC_BUNDLED_GLFW_DIR}/glfw3.dll)
    set(NDEVC_BUNDLED_GLFW_IMPLIB_DLL ${NDEVC_BUNDLED_GLFW_DIR}/glfw3dll.lib)
    set(NDEVC_BUNDLED_GLFW_IMPLIB_STD ${NDEVC_BUNDLED_GLFW_DIR}/glfw3.lib)

    if(EXISTS ${NDEVC_BUNDLED_GLFW_DLL} AND EXISTS ${NDEVC_BUNDLED_GLFW_IMPLIB_DLL})
        add_library(ndevc_glfw SHARED IMPORTED)
        set_target_properties(ndevc_glfw PROPERTIES
                IMPORTED_IMPLIB ${NDEVC_BUNDLED_GLFW_IMPLIB_DLL}
                IMPORTED_LOCATION ${NDEVC_BUNDLED_GLFW_DLL}
        )
        target_include_directories(ndevc_glfw INTERFACE ${CMAKE_SOURCE_DIR}/libs/glfw/include)
        set(NDEVC_GLFW_TARGET ndevc_glfw)
    elseif(EXISTS ${NDEVC_BUNDLED_GLFW_DLL} AND EXISTS ${NDEVC_BUNDLED_GLFW_IMPLIB_STD})
        add_library(ndevc_glfw SHARED IMPORTED)
        set_target_properties(ndevc_glfw PROPERTIES
                IMPORTED_IMPLIB ${NDEVC_BUNDLED_GLFW_IMPLIB_STD}
                IMPORTED_LOCATION ${NDEVC_BUNDLED_GLFW_DLL}
        )
        target_include_directories(ndevc_glfw INTERFACE ${CMAKE_SOURCE_DIR}/libs/glfw/include)
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
    set(NDEVC_DXT_DEBUG_LIB ${CMAKE_SOURCE_DIR}/libs/directxtex/debug/lib/DirectXTex.lib)
    set(NDEVC_DXT_RELEASE_LIB ${CMAKE_SOURCE_DIR}/libs/directxtex/release/lib/DirectXTex.lib)
    set(NDEVC_DXT_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libs/directxtex/release/include)
    if(NOT EXISTS ${NDEVC_DXT_INCLUDE_DIR})
        set(NDEVC_DXT_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libs/directxtex/debug/include)
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
        src/Rendering.cpp
        src/Rendering.h
        src/DrawCmd.h
        src/DeferredRenderer.h
        src/DeferredRenderer.cpp
        src/DeferredRendererWebSnapshot.cpp
        src/DeferredRendererRenderLoop.cpp
        src/DeferredRendererScene.cpp
        src/DeferredRendererPicking.cpp
        src/DeferredRendererUI.cpp
        src/DeferredRendererVisibility.cpp
        src/DeferredRendererAnimation.h
        src/DeferredRendererAnimation.cpp
        src/AnimatorNodeInstance.h
        src/AnimatorNodeInstance.cpp
        src/SelectionRaycaster.h
        src/SelectionRaycaster.cpp
        src/AnimationSystem.cpp
        src/AnimationSystem.h
        src/Shader.cpp
        src/Shader.h
        src/ShaderManager.cpp
        src/ShaderManager.h
        src/FrameGraph.cpp
        src/FrameGraph.h
        src/NDEVcHeaders.h
        src/NDEVcStructure.h
        src/NDEVcBinaryWriter.h
        src/NDEVcBinaryReader.h
        src/Mesh.cpp
        src/Mesh.h
        src/Parser.cpp
        src/Parser.h
        src/Model/Model.h
        src/Model/ModelInstance.h
        src/Model/ModelServer.h
        src/Model/Model.cpp
        src/Servers/MeshServer.h
        src/Servers/TextureServer.h
        src/Servers/TextureServer.cpp
        src/Camera.h
        src/DrawBatchSystem.cpp
        src/DrawBatchSystem.h
        src/visibility/VisibilityGrid.h
        src/visibility/VisibilityGrid.cpp
        src/MegaBuffer.cpp
        src/MegaBuffer.h
        src/Map/MapHeader.h
        src/Map/MapLoader.cpp
        src/Map/MapLoader.h
        src/ParticleData/EmitterMesh.cpp
        src/ParticleData/EmitterMesh.h
        src/ParticleData/EnvelopeCurve.cpp
        src/ParticleData/EnvelopeCurve.h
        src/ParticleData/EnvelopeSampleBuffer.cpp
        src/ParticleData/EnvelopeSampleBuffer.h
        src/ParticleData/EmitterAttrs.h
        src/ParticleData/Particle.h
        src/ParticleData/RingBuffer.h
        src/ParticleData/ParticleRenderer.cpp
        src/ParticleData/ParticleRenderer.h
        src/ParticleData/ParticleRenderInfo.h
        src/ParticleData/ParticleServer.h
        src/ParticleData/ParticleSystem.cpp
        src/ParticleData/ParticleSystem.h
        src/ParticleData/ParticleSystemState.h
        src/ParticleData/ParticleSystemInstanceBase.cpp
        src/ParticleData/ParticleSystemInstanceBase.h
        src/ParticleData/glparticlesysteminstance.cpp
        src/ParticleData/glparticlesysteminstance.h
        src/ParticleData/ParticleSystemNode.cpp
        src/ParticleData/ParticleSystemNode.h
        src/ParticleData/particlesystem_loader.h
        src/ParticleData/glparticlerenderer.cpp
        src/ParticleData/glparticlerenderer.h
        src/rendering/abstract/RenderingTypes.h
        src/rendering/abstract/IGraphicsDevice.h
        src/rendering/abstract/ITexture.h
        src/rendering/abstract/IFramebuffer.h
        src/rendering/abstract/IRenderState.h
        src/rendering/abstract/IBuffer.h
        src/rendering/abstract/ISampler.h
        src/rendering/abstract/IShader.h
        src/rendering/abstract/IShaderManager.h
        src/rendering/abstract/ITextureLoader.h
        src/rendering/abstract/IPlatform.h
        src/rendering/abstract/IWindow.h
        src/rendering/abstract/IInputSystem.h
        src/rendering/abstract/IRenderer.h
        src/rendering/opengl/OpenGLDevice.h
        src/rendering/opengl/OpenGLDevice.cpp
        src/rendering/opengl/OpenGLTexture.h
        src/rendering/opengl/OpenGLTexture.cpp
        src/rendering/opengl/OpenGLFramebuffer.h
        src/rendering/opengl/OpenGLFramebuffer.cpp
        src/rendering/opengl/OpenGLRenderState.h
        src/rendering/opengl/OpenGLRenderState.cpp
        src/rendering/opengl/OpenGLBuffer.h
        src/rendering/opengl/OpenGLBuffer.cpp
        src/rendering/opengl/OpenGLSampler.h
        src/rendering/opengl/OpenGLSampler.cpp
        src/rendering/opengl/OpenGLShader.h
        src/rendering/opengl/OpenGLShader.cpp
        src/rendering/opengl/OpenGLShaderManager.h
        src/rendering/opengl/OpenGLShaderManager.cpp
        src/rendering/opengl/OpenGLTextureLoader.h
        src/rendering/opengl/OpenGLTextureLoader.cpp
        src/rendering/opengl/GLFWPlatform.h
        src/rendering/opengl/GLFWPlatform.cpp
        src/rendering/opengl/GLFWWindow.h
        src/rendering/opengl/GLFWWindow.cpp
        src/rendering/opengl/GLFWInput.h
        src/rendering/opengl/GLFWInput.cpp
)

set_property(TARGET ndevc_core PROPERTY CXX_STANDARD 20)

target_include_directories(ndevc_core PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5/backends
)

target_link_libraries(ndevc_core PUBLIC
        glad
        ${NDEVC_GLFW_TARGET}
)
if(WIN32)
    target_link_libraries(ndevc_core PUBLIC winhttp)
endif()
if(NDEVC_DIRECTXTEX_TARGET)
    target_link_libraries(ndevc_core PUBLIC ${NDEVC_DIRECTXTEX_TARGET})
endif()
target_compile_definitions(ndevc_core PUBLIC NDEVC_HAS_DIRECTXTEX=${NDEVC_HAS_DIRECTXTEX})

add_executable(NDEVC
        NDEVC.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5/imgui.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5/imgui_widgets.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5/backends/imgui_impl_glfw.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5/backends/imgui_impl_opengl3.cpp
)
set_property(TARGET NDEVC PROPERTY CXX_STANDARD 20)
target_link_libraries(NDEVC PRIVATE ndevc_core)
target_include_directories(NDEVC PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.92.5/backends
)
target_compile_definitions(NDEVC PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
option(ENABLE_PARTICLE_DEBUG_LOG "Enable verbose particle debug logs/checks" OFF)
if(ENABLE_PARTICLE_DEBUG_LOG)
    target_compile_definitions(NDEVC PRIVATE ENABLE_PARTICLE_LOG ENABLE_PARTICLE_GL_CHECK)
endif()
if(WIN32 AND TARGET ndevc_glfw)
    add_custom_command(TARGET NDEVC POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${NDEVC_BUNDLED_GLFW_DLL}
            $<TARGET_FILE_DIR:NDEVC>/glfw3.dll
    )
endif()

if(BUILD_TOOLS)
    if(BUILD_MESH_VIEWER)
        add_subdirectory(src/toolkit/mesh-viewer)
    endif()
    if(BUILD_MODEL_VIEWER)
        add_subdirectory(src/toolkit/model-viewer)
    endif()
endif()
