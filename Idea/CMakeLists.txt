cmake_minimum_required(VERSION 3.24)

# Runtime options.
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_VERBOSE_MAKEFILE OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Global properties.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
cmake_host_system_information(RESULT CPU_CORES QUERY NUMBER_OF_PHYSICAL_CORES)
set(CMAKE_BUILD_PARALLEL_LEVEL ${CPU_CORES} CACHE STRING "Number of cores used for CMake build." FORCE)

project(LynxEngine VERSION "1.0.0.0" DESCRIPTION "DX12 C++ Game engine." LANGUAGES C CXX)

# CMake dependencies.
include(CTest)
include(GNUInstallDirs)
include(CheckCXXCompilerFlag)
include(CMakePrintHelpers)
include(CMakeDependentOption)
include(CMakePackageConfigHelpers)

# Download options.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "FetchContent download dir." FORCE)

# CMake modules.
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake")

# Project dirs.
set(PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# set(PROJECT_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
# set(PROJECT_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Plugins")

# Linker options.
set(CMAKE_LINK_DEPENDS_NO_SHARED OFF)

# Build options.
option(BUILD_SHARED_LIBS "Build shared libs." OFF)
option(BUILD_TESTS "Build tests." OFF)
option(BUILD_PLUGINS "Build plugins." OFF)
cmake_dependent_option(SHARED_RT
	"MSVC: Build shared runtime libs (/MD)" ON
	"CMAKE_SYSTEM_NAME MATCHES Windows" ON
)

# RHI options.
option(RHI_VULKAN "Use Vulkan as the Rhi backend." ON)
cmake_dependent_option(RHI_DX11 "Use D3D11 as the Rhi backend." ON "WIN32" OFF)
cmake_dependent_option(RHI_DX12 "Use D3D12 as the Rhi backend." ON "WIN32" OFF)

# Output options.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})
set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR}/install)

# Platform detection.
if(WIN32)
    set(PLATFORM_NAME Windows)
elseif(UNIX)
    set(PLATFORM_NAME Linux)
endif()

# The engine build configuration.
add_library(LynxEngineBuildConfig INTERFACE)
add_library(LynxEngine::BuildConfig ALIAS LynxEngineBuildConfig)
target_compile_features(LynxEngineBuildConfig 
	INTERFACE 
		cxx_std_23
)
target_compile_definitions(LynxEngineBuildConfig 
    INTERFACE
        $<$<CONFIG:Debug>:LYNX_ENGINE_DEBUG>
        $<$<PLATFORM_ID:Windows>:LYNX_ENGINE_PLATFORM_WINDOWS>
        $<$<PLATFORM_ID:Linux>:LYNX_ENGINE_PLATFORM_LINUX>
        $<$<BOOL:${BUILD_SHARED_LIBS}>:LYNX_ENGINE_EXPORTS>
        $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:LYNX_ENGINE_STATIC>
)
set_target_properties(LynxEngineBuildConfig
	PROPERTIES
		WIN32_EXECUTABLE $<$<PLATFORM_ID:Windows>:TRUE>
)

# Compiler options.
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    message(STATUS "Configuring for MSVC")
	target_compile_options(LynxEngineBuildConfig 
		INTERFACE
			# All
			/MP		# Compile using multiple cores.
			/W4		# Lv4 warnings are errors.
			/EHsc	# Exception handling model.
			# Debug
			$<$<CONFIG:Debug>:/Od>
			$<$<CONFIG:Debug>:/D_DEBUG>
			#$<$<CONFIG:Debug>:/fsanitize=address>
			# Release
			$<$<CONFIG:Release>:/O2>
			$<$<CONFIG:Release>:/DNDEBUG>
			# RelWithDebInfo
			$<$<CONFIG:RelWithDebInfo>:/O2>
			$<$<CONFIG:RelWithDebInfo>:/DNDEBUG>
	)

# Enable policy for MT.
	if(POLICY CMP0091)
		cmake_policy(SET CMP0091 NEW)
		if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
			set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<BOOL:${SHARED_RT}>:DLL> CACHE INTERNAL "Msvc runtime library")
		endif()
	endif()

    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT $<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>)

elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Configuring for Clang")
    # General Clang flags
    target_compile_options(LynxEngineBuildConfig
		INTERFACE
			# All
			-Wall
			-Wextra
			-Wpedantic
			-Wno-c++98-compat
			-Wno-pre-c++14-compat
			-Wno-sign-compare
			-Wno-old-style-cast
			-Wno-unused-parameter
			-Wno-unused-function
			-Wno-unused-local-typedefs
			-Wno-unused-but-set-variable
			-Wunsafe-buffer-usage
			# Debug
			$<$<CONFIG:Debug>:-g>
			#$<$<CONFIG:Debug>:-fsanitize=address>
			# Release
			$<$<CONFIG:Release>:-O2>
			$<$<CONFIG:Release>:-DNDEBUG>
			# RelWithDebInfo
			$<$<CONFIG:RelWithDebInfo>:-g>
			$<$<CONFIG:RelWithDebInfo>:-O2>
			$<$<CONFIG:RelWithDebInfo>:-DNDEBUG>
    )
else()
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# Config Vcpkg.
include(FindVcpkg)
FindVcpkg()

# Find vulkan backend.
find_package(Vulkan REQUIRED)

# Find Stb package with vcpkg.
find_package(Stb REQUIRED)
add_library(Stb INTERFACE IMPORTED GLOBAL)
target_include_directories(Stb INTERFACE ${Stb_INCLUDE_DIR})

# Find Slang shader-compiler with vcpkg.
find_package(slang CONFIG REQUIRED)

# Find DirectX-Compiler with vcpkg.
find_package(directx-dxc CONFIG REQUIRED)

# Find freetype with vcpkg.
find_package(Freetype CONFIG REQUIRED)

# Config nvrhi.
message(STATUS "Using DX11 : ${RHI_DX11}")
message(STATUS "Using DX12 : ${RHI_DX12}")
message(STATUS "Using Vulkan : ${RHI_VULKAN}")
FetchContent_Declare(
	nvrhi
	SYSTEM
	GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
	GIT_TAG main
	GIT_PROGRESS TRUE
	SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/nvrhi"
	CMAKE_ARGS
	    -DNVRHI_WITH_RTXMU=ON
	    -DNVRHI_WITH_D3D11=${RHI_DX11}
	    -DNVRHI_WITH_D3D12=${RHI_DX12}
	    -DNVRHI_WITH_VULKAN=${RHI_VULKAN}
)
FetchContent_MakeAvailable(nvrhi)

# Build LynxEngine::Core library.
add_library(LynxEngineCore)
add_library(LynxEngine::Core ALIAS LynxEngineCore)
set(LYNX_ENGINE_CORE_HEADERS
	# app
	"Core/App/Application.h"
	"Core/App/EventDispacher.h"
	"Core/App/EventQueue.h"
	"Core/App/MouseEvent.h"
	"Core/App/KeyEvent.h"
	"Core/App/InputSystem.h"
	# asset
	"Core/Asset/Asset.h"
	"Core/Asset/AssetImporter.h"
	"Core/Asset/AssetManager.h"
	"Core/Asset/MeshData.h"
	"Core/Asset/MaterialData.h"
	"Core/Asset/TextureData.h"
	"Core/Asset/Font.h"
	"Core/Asset/SpriteData.h"
	"Core/Asset/TextureImporter.h"
	"Core/Asset/FontImporter.h"
	# math
	"Core/Math/Constants.h"
	"Core/Math/Rect.h"
	"Core/Math/Color.h"
	"Core/Math/Viewport.h"
	"Core/Math/Vec2.h"
	"Core/Math/Vec3.h"
	"Core/Math/Vec4.h"
	"Core/Math/Mat3.h"
	"Core/Math/Mat4.h"
	"Core/Math/Camera2D.h"
	# concepts
	"Core/Concepts/API.h"
	"Core/Concepts/Event.h"
	"Core/Concepts/Delegate.h"
	"Core/Concepts/Flag.h"
	"Core/Concepts/Hash.h"
	"Core/Concepts/Input.h"
	"Core/Concepts/MulticastDelegate.h"
	"Core/Concepts/NonCopyable.h"
	"Core/Concepts/NonMovable.h"
	"Core/Concepts/Singleton.h"
	# PAL
	"Core/Platform/PlatformWindow.h"
	"Core/Platform/VirtualKeys.h"
	"Core/Platform/${PLATFORM_NAME}/Environment.h"
	#Utils
	"Core/Utils/IO.h"
	"Core/Utils/Debug.h"
	"Core/Utils/Timer.h"
)
set(LYNX_ENGINE_CORE_SRC
	# app
	"Core/App/Application.cpp"
	"Core/App/EventDispacher.cpp"
	"Core/App/EventQueue.cpp"
	"Core/App/InputSystem.cpp"
	# asset
	"Core/Asset/AssetManager.cpp"
	"Core/Asset/TextureImporter.cpp"
	"Core/Asset/Font.cpp"
	"Core/Asset/FontImporter.cpp"
	# math
	"Core/Math/Rect.cpp"
	"Core/Math/Color.cpp"
	"Core/Math/Viewport.cpp"
	"Core/Math/Vec2.cpp"
	"Core/Math/Vec3.cpp"
	"Core/Math/Vec4.cpp"
	"Core/Math/Mat3.cpp"
	"Core/Math/Mat4.cpp"
	"Core/Math/Camera2D.cpp"
	# PAL
	"Core/Platform/${PLATFORM_NAME}/VirtualKeys.cpp"
	"Core/Platform/${PLATFORM_NAME}/PlatformWindow.cpp"
	#Asset
)

target_sources(LynxEngineCore
    PUBLIC
        FILE_SET HEADERS
            FILES ${LYNX_ENGINE_CORE_HEADERS}
    PRIVATE
        ${LYNX_ENGINE_CORE_SRC}
)
target_include_directories(LynxEngineCore
	PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Engine/Core>
	$<INSTALL_INTERFACE:include/Engine/Core>
)
target_link_libraries(LynxEngineCore
    PUBLIC
        LynxEngine::BuildConfig
    PRIVATE
        Stb
		Freetype::Freetype
        $<$<PLATFORM_ID:Linux>:X11>
        $<$<PLATFORM_ID:Windows>:Dwmapi>
)

# Build LynxEngine::Graphics library.
add_library(LynxEngineGraphics)
add_library(LynxEngine::Graphics ALIAS LynxEngineGraphics)
set(LYNX_ENGINE_GRAPHICS_HEADERS
	"Graphics/Shader/ShaderCompiler.h"
	"Graphics/Rendering/RHI/DescriptorTableManager.h"
	"Graphics/Rendering/RHI/Backend.h"
	"Graphics/Rendering/RHI/D3D12Backend.h"
	"Graphics/Rendering/Renderer.h"
	"Graphics/Rendering/Types.h"
	"Graphics/Rendering/RenderPass.h"
)
set(LYNX_ENGINE_GRAPHICS_SRC
	"Graphics/Rendering/RHI/Backend.cpp"
	"Graphics/Shader/ShaderCompiler.cpp"
	"Graphics/Rendering/RHI/DescriptorTableManager.cpp"
	"Graphics/Rendering/RHI/D3D12DeviceManager.cpp"
	"Graphics/Rendering/RHI/Renderer.cpp"
	"Graphics/Rendering/RHI/D3D12Swapchain.cpp"
)
target_sources(LynxEngineGraphics
	PUBLIC 
		FILE_SET HEADERS
			FILES ${LYNX_ENGINE_GRAPHICS_HEADERS}
	PRIVATE ${LYNX_ENGINE_GRAPHICS_SRC}
)
target_include_directories(LynxEngineGraphics
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Engine/Graphics>
		$<INSTALL_INTERFACE:include/Engine/Graphics>
)
target_link_libraries(LynxEngineGraphics
	PUBLIC
		LynxEngine::Core
		LynxEngine::BuildConfig
	PRIVATE
		nvrhi
		Microsoft::DirectXShaderCompiler
		slang::slang
	PRIVATE
        $<$<BOOL:${RHI_VULKAN}>:nvrhi_vk;Vulkan::Vulkan>
        $<$<BOOL:${RHI_DX11}>:nvrhi_d3d11>
		$<$<BOOL:${RHI_DX12}>:nvrhi_d3d12>
		$<$<OR:$<BOOL:${RHI_DX12}>,$<BOOL:${RHI_DX11}>>:dxgi;dxguid;dcomp>
)
target_compile_definitions(LynxEngineGraphics 
	PUBLIC
        $<$<BOOL:${RHI_VULKAN}>:LYNX_ENGINE_RHI_VULKAN>
        $<$<BOOL:${RHI_DX11}>:LYNX_ENGINE_RHI_D3D11>
        $<$<BOOL:${RHI_DX12}>:LYNX_ENGINE_RHI_D3D12>
)

if(BUILD_PLUGINS)
	# Find imgui with vcpkg.
	find_package(imgui CONFIG REQUIRED)

	# Build ImGui plugin.
	include(LynxEnginePlugin)
	LynxEngine_AddPlugin(
	    NAME "ImGuiPlugin"
		DEVELOPER ${PROJECT_NAME}
		DESCRIPTION "ImGui extension."
        SOURCES "${PROJECT_PLUGIN_DIR}/imgui/src/ImGuiPlugin.cpp"
		HEADERS "${PROJECT_PLUGIN_DIR}/imgui/include/ImGuiPlugin.h"
		INCLUDE_DIRS "${PROJECT_PLUGIN_DIR}/imgui/include"
		LINK_LIBRARIES
			imgui::imgui
			LynxEngine::Core
			LynxEngine::Graphics
	)
endif()

if(BUILD_TESTS)
	# Find GTest with vcpkg.
	find_package(GTest CONFIG REQUIRED)

	# Build tests.
	include(LynxEngineTest)
	LynxEngine_AddTest(
        NAME CoreTest
        SOURCES "${PROJECT_TEST_DIR}/CoreTest.cpp"
        LINK_LIBRARIES LynxEngine::Core
    )
	
    LynxEngine_AddTest(
        NAME GraphicsTest
        SOURCES "${PROJECT_TEST_DIR}/GraphicsTest.cpp"
        LINK_LIBRARIES 
			LynxEngine::Core
			LynxEngine::Graphics
			LynxEngine::ImGuiPlugin
    )
endif()

# Build LynxEngine app.
add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} 
	PRIVATE 
		"Runtime/Editor.cpp"
		"Runtime/GameInstance.h"
		"Runtime/GameInstance.cpp")
target_link_libraries(${PROJECT_NAME}
	PUBLIC
		LynxEngine::BuildConfig
	PRIVATE
		LynxEngine::Core
		LynxEngine::Graphics
)

# Install runtime dependencies based on platform
if(TARGET ${PROJECT_NAME})
    if(WIN32)
        install(FILES
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxcompiler.dll"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxil.dll"
            DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            PERMISSIONS OWNER_READ OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE
        )
	endif()
endif()