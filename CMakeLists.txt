cmake_minimum_required(VERSION 3.24)

project(RenderGraphPrototype VERSION 0.1.0 DESCRIPTION "Standalone RenderGraph prototype (NVRHI / D3D12)" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(RG_BUILD_APP "Build the Win32 / NVRHI / D3D12 sample application" ${WIN32})
option(RG_BUILD_TESTS "Build the headless RenderGraph tests (no GPU needed)" ON)
option(RG_BACKEND_VULKAN "Also build NVRHI's Vulkan backend (the Vulkan DeviceBackend is still a placeholder)" OFF)

# ---------------------------------------------------------------------------------------------
# Common compile settings
# ---------------------------------------------------------------------------------------------
add_library(rg_build_config INTERFACE)
target_compile_features(rg_build_config INTERFACE cxx_std_23)
target_include_directories(rg_build_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(rg_build_config INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP)
else()
    target_compile_options(rg_build_config INTERFACE -Wall -Wextra)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Idea/MeshData.h declares "IndexFormat IndexFormat;", which GCC rejects by default.
    target_compile_options(rg_build_config INTERFACE -Wno-changes-meaning)
endif()
if(WIN32)
    target_compile_definitions(rg_build_config INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

# ---------------------------------------------------------------------------------------------
# RenderGraphCore: RenderGraph + RenderPass + example passes + scene data.
# No graphics API, no NVRHI, no window. Builds on any platform.
# ---------------------------------------------------------------------------------------------
add_library(RenderGraphCore STATIC
    RenderGraph/Types.h
    RenderGraph/Types.cpp
    RenderGraph/DrawContext.h
    RenderGraph/RenderPass.h
    RenderGraph/RenderPass.cpp
    RenderGraph/RenderGraph.h
    RenderGraph/RenderGraph.cpp

    Passes/DepthPass.h
    Passes/ShadowPass.h
    Passes/GBufferPass.h
    Passes/LightUpdatePass.h
    Passes/LightingPass.h
    Passes/ForwardPass.h
    Passes/DebugNormalsPass.h
    Passes/PostProcessPass.h
    Passes/OverlayPass.h
    Passes/SceneDraw.h
    Passes/DemoGraph.h
    Passes/DemoGraph.cpp

    Scene/MeshData.h
    Scene/MaterialData.h
    Scene/TextureData.h
    Scene/Math.h
    Scene/PerspectiveCamera.h
    Scene/PerspectiveCamera.cpp
    Scene/Scene.h
    Scene/Scene.cpp
)
target_link_libraries(RenderGraphCore PUBLIC rg_build_config)

if(RG_BUILD_TESTS)
    enable_testing()
    add_executable(RenderGraphTests Tests/RenderGraphTests.cpp)
    target_link_libraries(RenderGraphTests PRIVATE RenderGraphCore)
    add_test(NAME RenderGraphTests COMMAND RenderGraphTests)
endif()

# ---------------------------------------------------------------------------------------------
# Sample application: Win32 + Renderer (NVRHI) + D3D12 backend + ImGui + Slang.
# ---------------------------------------------------------------------------------------------
if(RG_BUILD_APP)
    if(NOT WIN32)
        message(FATAL_ERROR "The sample application is Windows only. Configure with -DRG_BUILD_APP=OFF to build the tests.")
    endif()

    # vcpkg packages (see vcpkg.json).
    find_package(imgui CONFIG REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(slang CONFIG REQUIRED)
    find_package(directx-dxc CONFIG REQUIRED) # dxcompiler.dll / dxil.dll, used by Slang to emit DXIL
    find_package(MaterialX CONFIG REQUIRED)   # .mtlx reading (Scene/MaterialXImporter.cpp)
    find_package(Stb REQUIRED)                # stb_image, for the textures a .mtlx references

    # MaterialX asset drawn on the center cube, extracted into the build tree at configure time.
    set(RG_MATERIALX_ZIP ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Dark_Glass_2k_8b_oCH0KDS.zip)
    set(RG_MATERIALX_DIR ${CMAKE_BINARY_DIR}/Assets/Dark_Glass_2k_8b_oCH0KDS)
    file(ARCHIVE_EXTRACT INPUT ${RG_MATERIALX_ZIP} DESTINATION ${RG_MATERIALX_DIR})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RG_MATERIALX_ZIP})

    # NVRHI, pinned to a known commit. NVRHI fetches its own DirectX-Headers.
    include(FetchContent)
    set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX12 ON CACHE BOOL "" FORCE)
    set(NVRHI_WITH_VULKAN ${RG_BACKEND_VULKAN} CACHE BOOL "" FORCE)
    set(NVRHI_WITH_RTXMU OFF CACHE BOOL "" FORCE)
    set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        nvrhi
        GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
        GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906
        GIT_PROGRESS TRUE
        SYSTEM
    )
    FetchContent_MakeAvailable(nvrhi)

    set(RG_SHADERS
        Shaders/Common.slang
        Shaders/PBR.slang
        Shaders/Fullscreen.slang
        Shaders/Scene.slang
        Shaders/Shadow.slang
        Shaders/Lights.slang
        Shaders/Lighting.slang
        Shaders/Forward.slang
        Shaders/DebugNormals.slang
        Shaders/PostProcess.slang
        Shaders/ImGui.slang
    )
    set_source_files_properties(${RG_SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

    add_executable(RenderGraphPrototype
        Application/Main.cpp
        Application/Win32Application.h
        Application/Win32Application.cpp
        Application/ImGuiDraw.h
        Application/ImGuiDraw.cpp

        Renderer/Renderer.h
        Renderer/Renderer.cpp
        Renderer/DescriptorTableManager.h
        Renderer/DescriptorTableManager.cpp
        Renderer/ShaderCompiler.h
        Renderer/ShaderCompiler.cpp

        Scene/MaterialXImporter.h
        Scene/MaterialXImporter.cpp

        Backends/Backend.h
        Backends/D3D12/D3D12Backend.cpp
        Backends/Vulkan/VulkanBackend.cpp

        ${RG_SHADERS}
    )
    target_link_libraries(RenderGraphPrototype PRIVATE
        RenderGraphCore
        nvrhi
        nvrhi_d3d12
        $<$<BOOL:${RG_BACKEND_VULKAN}>:nvrhi_vk>
        imgui::imgui
        Freetype::Freetype
        slang::slang
        MaterialXCore
        MaterialXFormat
        Microsoft::DirectXShaderCompiler
        d3d12
        dxgi
    )
    target_include_directories(RenderGraphPrototype PRIVATE ${Stb_INCLUDE_DIR})
    target_compile_definitions(RenderGraphPrototype PRIVATE
        RG_SOURCE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
        RG_MATERIALX_FILE="${RG_MATERIALX_DIR}/Dark_Glass.mtlx")
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${RG_SHADERS})

    # Runtime files next to the executable: shaders + Slang / DXC DLLs.
    set(RG_EXTRA_DLLS "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        file(GLOB RG_EXTRA_DLLS
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/slang*.dll"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxcompiler.dll"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxil.dll")
    endif()
    add_custom_command(TARGET RenderGraphPrototype POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Shaders $<TARGET_FILE_DIR:RenderGraphPrototype>/Shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RenderGraphPrototype> ${RG_EXTRA_DLLS} $<TARGET_FILE_DIR:RenderGraphPrototype>
        COMMAND_EXPAND_LISTS
    )

    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT RenderGraphPrototype)
    set_target_properties(RenderGraphPrototype PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:RenderGraphPrototype>)
endif()
