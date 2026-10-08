#include "Renderer/ShaderCompiler.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <cstring>

namespace LynxEngine
{
    namespace
    {
        std::string ToText(slang::IBlob* blob)
        {
            if (!blob || blob->getBufferSize() == 0)
                return {};
            return std::string(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
        }
    }

    class ShaderCompiler::Impl
    {
    public:
        explicit Impl(GraphicsAPI api)
            : m_Api(api)
        {
            if (SLANG_FAILED(slang::createGlobalSession(m_GlobalSession.writeRef())))
                m_GlobalSession = nullptr;
        }

        bool IsValid() const { return m_GlobalSession != nullptr; }

        std::expected<ShaderBlob, std::string> Compile(const ShaderCompileRequest& request)
        {
            if (!m_GlobalSession)
                return std::unexpected("Slang global session could not be created");

            // Session: one target, the shader's directory + include directories as search paths.
            slang::TargetDesc target{};
            target.format = m_Api == GraphicsAPI::Vulkan ? SLANG_SPIRV : SLANG_DXIL;
            target.profile = m_GlobalSession->findProfile(m_Api == GraphicsAPI::Vulkan ? "spirv_1_5" : "sm_6_5");

            std::vector<std::string> searchStorage{ request.Source.parent_path().string() };
            for (const auto& include : request.IncludeDirectories)
                searchStorage.push_back(include.string());
            std::vector<const char*> searchPaths;
            for (const std::string& path : searchStorage)
                searchPaths.push_back(path.c_str());

            std::vector<slang::PreprocessorMacroDesc> macros;
            for (const ShaderMacro& macro : request.Macros)
                macros.push_back({ macro.Name.c_str(), macro.Value.c_str() });

            slang::SessionDesc sessionDesc{};
            sessionDesc.targets = &target;
            sessionDesc.targetCount = 1;
            sessionDesc.searchPaths = searchPaths.data();
            sessionDesc.searchPathCount = SlangInt(searchPaths.size());
            sessionDesc.preprocessorMacros = macros.data();
            sessionDesc.preprocessorMacroCount = SlangInt(macros.size());
            sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;

            Slang::ComPtr<slang::ISession> session;
            if (SLANG_FAILED(m_GlobalSession->createSession(sessionDesc, session.writeRef())) || !session)
                return std::unexpected("Failed to create a Slang session");

            // Module + entry point.
            Slang::ComPtr<slang::IBlob> diagnostics;
            const std::string moduleName = request.Source.stem().string();
            Slang::ComPtr<slang::IModule> module(session->loadModule(moduleName.c_str(), diagnostics.writeRef()));
            if (!module)
                return std::unexpected(std::string("Failed to load '") + request.Source.string() + "':\n" + ToText(diagnostics));

            Slang::ComPtr<slang::IEntryPoint> entryPoint;
            if (SLANG_FAILED(module->findEntryPointByName(request.EntryPoint.c_str(), entryPoint.writeRef())) || !entryPoint)
                return std::unexpected("Entry point '" + request.EntryPoint + "' not found in " + request.Source.string() +
                    " (did you mark it with [shader(\"...\")]?)");

            // Compose, link and generate code.
            slang::IComponentType* components[] = { module, entryPoint };
            Slang::ComPtr<slang::IComponentType> program;
            diagnostics = nullptr;
            if (SLANG_FAILED(session->createCompositeComponentType(components, 2, program.writeRef(), diagnostics.writeRef())))
                return std::unexpected("Slang composition failed:\n" + ToText(diagnostics));

            Slang::ComPtr<slang::IComponentType> linked;
            diagnostics = nullptr;
            if (SLANG_FAILED(program->link(linked.writeRef(), diagnostics.writeRef())))
                return std::unexpected("Slang link failed:\n" + ToText(diagnostics));

            Slang::ComPtr<slang::IBlob> code;
            diagnostics = nullptr;
            if (SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())) || !code)
                return std::unexpected("Code generation failed for " + request.Source.string() + ":" + request.EntryPoint + ":\n" + ToText(diagnostics));

            ShaderBlob blob;
            blob.Bytecode.resize(code->getBufferSize());
            std::memcpy(blob.Bytecode.data(), code->getBufferPointer(), code->getBufferSize());
            return blob;
        }

    private:
        GraphicsAPI m_Api;
        Slang::ComPtr<slang::IGlobalSession> m_GlobalSession;
    };

    ShaderCompiler::ShaderCompiler(GraphicsAPI api)
        : m_Impl(std::make_unique<Impl>(api))
    {
    }

    ShaderCompiler::~ShaderCompiler() = default;

    bool ShaderCompiler::IsValid() const
    {
        return m_Impl->IsValid();
    }

    std::expected<ShaderBlob, std::string> ShaderCompiler::Compile(const ShaderCompileRequest& request)
    {
        return m_Impl->Compile(request);
    }
}
