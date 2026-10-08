#pragma once

// Slang based shader compiler, adapted from Idea/ShaderCompiler.h.
// Compiles one entry point to DXIL (D3D12) or SPIR-V (Vulkan). Reflection is not needed
// by this prototype because passes bind resources by convention (see Renderer.h).

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "RenderGraph/Types.h"

namespace LynxEngine
{
    struct ShaderMacro
    {
        std::string Name;
        std::string Value;
    };

    struct ShaderCompileRequest
    {
        std::filesystem::path Source;
        std::string EntryPoint;
        std::vector<ShaderMacro> Macros;
        std::vector<std::filesystem::path> IncludeDirectories;
    };

    struct ShaderBlob
    {
        std::vector<std::byte> Bytecode;
    };

    class ShaderCompiler
    {
    public:
        explicit ShaderCompiler(GraphicsAPI api);
        ~ShaderCompiler();

        bool IsValid() const;
        std::expected<ShaderBlob, std::string> Compile(const ShaderCompileRequest& request);

    private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
