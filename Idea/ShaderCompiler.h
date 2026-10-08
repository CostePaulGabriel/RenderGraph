#pragma once

#include "../Rendering/Types.h"

#include <vector>
#include <filesystem>
#include <string_view>
#include <Core/Concepts/ExceptionHandler.h>
#include <Core/Concepts/API.h>

namespace LynxEngine
{
    enum class ShaderValue : uint8_t
    {
        Unknown = 0,
        Bool,
        Int,
        UInt,
        Float,
        Double,
        Struct
    };

    struct ShaderMacro
    {
        std::string Name;
        std::string Value;
    };

    struct ShaderVariable
    {
        std::string Name;
        ShaderValue Type = ShaderValue::Unknown;
        uint32_t Offset = 0;
        uint32_t Size = 0;
        uint32_t Rows = 1;
        uint32_t Columns = 1;
        uint32_t ArrayCount = 0;
        std::vector<ShaderVariable> Members;
    };

    struct ShaderResource
    {
        std::string Name;
        ResourceType Type = ResourceType::None;
        uint32_t Binding = 0;
        uint32_t Space = 0;
        uint32_t ElementSize = 0;
        std::vector<ShaderVariable> Variables;
    };

    struct ShaderReflection
    {
        std::vector<ShaderResource> Resources;
    };

    struct ShaderBlob
    {
        ShaderType Type = ShaderType::Unknown;
        ShaderReflection Reflection;
        std::vector<std::byte> Bytecode;
    };

    struct ShaderCompilerConfig
    {
        GraphicsAPI API;
        IExceptionHandler* pExceptionHandler = nullptr;
    };

    struct ShaderCompileRequest
    {
        std::filesystem::path Source;
        std::string EntryPoint;
        std::vector<ShaderMacro> Macros;
        std::vector<std::filesystem::path> IncludeDirectories;
    };

    class ShaderCompiler
    {
    public:
        ShaderCompiler();
        ~ShaderCompiler();

        bool Init(const ShaderCompilerConfig& config);

        ShaderBlob Compile(const ShaderCompileRequest& request);

        static void PrintReflection(const ShaderBlob& blob);

    private:
        class Impl;
        std::unique_ptr<Impl> m_pImpl;
    };
}