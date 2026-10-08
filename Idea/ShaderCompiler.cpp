#include "ShaderCompiler.h"

#include <Core/Utils/Debug.h>

#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

#include <cstring>
#include <stdexcept>


namespace LynxEngine
{

    class ShaderCompiler::Impl
    {
    public:

        explicit Impl(const ShaderCompilerConfig& config)
            : Config(config)
        {
        }


        bool Initialize();


        ShaderBlob Compile(
            const ShaderCompileRequest& request);



        static void DiagnosticCallback(
            const char* message,
            void* userData)
        {
            auto pImpl =
                reinterpret_cast<ShaderCompiler::Impl*>(userData);


            if (!pImpl ||
                !pImpl->Config.pExceptionHandler)
            {
                return;
            }


            MessageSeverity severity =
                MessageSeverity::Info;


            if (std::strstr(message, "error:"))
            {
                severity = MessageSeverity::Error;
            }
            else if (std::strstr(message, "warning:"))
            {
                severity = MessageSeverity::Warning;
            }


            pImpl->Config.pExceptionHandler->Message(
                severity,
                message);
        }



    private:


        Slang::ComPtr<slang::ISession> CreateSession(
            const ShaderCompileRequest& request);



        Slang::ComPtr<slang::IModule> LoadModule(
            slang::ISession* session,
            const ShaderCompileRequest& request);



        Slang::ComPtr<slang::IEntryPoint> LoadEntryPoint(
            slang::IModule* module,
            const ShaderCompileRequest& request);



        Slang::ComPtr<slang::IComponentType> LinkProgram(
            slang::ISession* session,
            slang::IModule* module,
            slang::IEntryPoint* entryPoint);



        ShaderBlob BuildShaderBlob(
            slang::IComponentType* linkedProgram);



        void BuildReflection(
            slang::ProgramLayout* layout,
            ShaderReflection& reflection);



        void ExtractResources(
            slang::ProgramLayout* layout,
            ShaderReflection& reflection);



        void ExtractVariables(
            slang::TypeLayoutReflection* typeLayout,
            std::vector<ShaderVariable>& variables);

        ShaderType GetShaderType(
            slang::ProgramLayout* layout) const;

        ShaderValue GetShaderValue(
            slang::TypeReflection* type);

        SlangCompileTarget GetCompileTarget() const;


        SlangProfileID GetProfile() const;



    private:


        ShaderCompilerConfig Config;


        // Singura instanta care trebuie pastrata
        Slang::ComPtr<slang::IGlobalSession> GlobalSession;


        Slang::ComPtr<slang::IBlob> Diagnostics;

    };





    bool ShaderCompiler::Impl::Initialize()
    {

        SlangResult result =
            slang::createGlobalSession(
                GlobalSession.writeRef());


        return SLANG_SUCCEEDED(result);
    }







    Slang::ComPtr<slang::ISession>
        ShaderCompiler::Impl::CreateSession(
            const ShaderCompileRequest& request)
    {

        slang::TargetDesc target = {};

        target.format =
            GetCompileTarget();


        target.profile =
            GetProfile();



        std::vector<std::string> searchStorage;


        searchStorage.push_back(
            request.Source.parent_path().string());



        for (const auto& include :
            request.IncludeDirectories)
        {
            searchStorage.push_back(
                include.string());
        }



        std::vector<const char*> searchPaths;


        for (auto& path : searchStorage)
        {
            searchPaths.push_back(
                path.c_str());
        }



        slang::SessionDesc desc = {};


        desc.targets =
            &target;


        desc.targetCount =
            1;



        desc.searchPaths =
            searchPaths.data();


        desc.searchPathCount =
            (SlangInt)searchPaths.size();



        desc.defaultMatrixLayoutMode =
            SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;



        Slang::ComPtr<slang::ISession> session;



        SlangResult result =
            GlobalSession->createSession(
                desc,
                session.writeRef());



        if (SLANG_FAILED(result) ||
            !session)
        {
            throw std::runtime_error(
                "Failed creating Slang session.");
        }



        return session;
    }








    Slang::ComPtr<slang::IModule>
        ShaderCompiler::Impl::LoadModule(
            slang::ISession* session,
            const ShaderCompileRequest& request)
    {

        Diagnostics = nullptr;



        std::string moduleName =
            request.Source.stem().string();



        printf(
            "Loading module: %s\n",
            moduleName.c_str());



        Slang::ComPtr<slang::IModule> module;



        module =
            session->loadModule(
                moduleName.c_str(),
                Diagnostics.writeRef());



        if (!module)
        {

            if (Diagnostics)
            {
                DiagnosticCallback(
                    static_cast<const char*>(
                        Diagnostics->getBufferPointer()),
                    this);
            }


            throw std::runtime_error(
                "Failed loading shader module.");
        }



        return module;
    }








    Slang::ComPtr<slang::IEntryPoint>
        ShaderCompiler::Impl::LoadEntryPoint(
            slang::IModule* module,
            const ShaderCompileRequest& request)
    {

        Diagnostics = nullptr;


        Slang::ComPtr<slang::IEntryPoint> entryPoint;



        SlangResult result =
            module->findEntryPointByName(
                request.EntryPoint.c_str(),
                entryPoint.writeRef());



        if (SLANG_FAILED(result) ||
            !entryPoint)
        {

            throw std::runtime_error(
                "Entry point not found.");
        }



        return entryPoint;
    }








    Slang::ComPtr<slang::IComponentType>
        ShaderCompiler::Impl::LinkProgram(
            slang::ISession* session,
            slang::IModule* module,
            slang::IEntryPoint* entryPoint)
    {

        slang::IComponentType* components[] =
        {
            module,
            entryPoint
        };



        Diagnostics = nullptr;


        Slang::ComPtr<slang::IComponentType> program;



        SlangResult result =
            session->createCompositeComponentType(
                components,
                2,
                program.writeRef(),
                Diagnostics.writeRef());



        if (SLANG_FAILED(result) ||
            !program)
        {
            throw std::runtime_error(
                "Failed creating composite program.");
        }




        Diagnostics = nullptr;



        Slang::ComPtr<slang::IComponentType> linkedProgram;



        result =
            program->link(
                linkedProgram.writeRef(),
                Diagnostics.writeRef());



        if (SLANG_FAILED(result) ||
            !linkedProgram)
        {
            throw std::runtime_error(
                "Failed linking shader.");
        }



        return linkedProgram;
    }








    ShaderBlob ShaderCompiler::Impl::Compile(
        const ShaderCompileRequest& request)
    {

        auto session =
            CreateSession(request);



        auto module =
            LoadModule(
                session,
                request);



        auto entryPoint =
            LoadEntryPoint(
                module,
                request);



        auto linkedProgram =
            LinkProgram(
                session,
                module,
                entryPoint);



        return BuildShaderBlob(
            linkedProgram);
    }
    ShaderBlob ShaderCompiler::Impl::BuildShaderBlob(
        slang::IComponentType* linkedProgram)
    {
        ShaderBlob blob;


        slang::ProgramLayout* layout =
            linkedProgram->getLayout();


        if (!layout)
        {
            throw std::runtime_error(
                "Invalid Slang program layout.");
        }



        blob.Type =
            GetShaderType(layout);



        BuildReflection(
            layout,
            blob.Reflection);



        Slang::ComPtr<slang::IBlob> code;



        Diagnostics = nullptr;



        SlangResult result =
            linkedProgram->getEntryPointCode(
                0,
                0,
                code.writeRef(),
                Diagnostics.writeRef());



        if (SLANG_FAILED(result) ||
            !code)
        {

            if (Diagnostics)
            {
                DiagnosticCallback(
                    static_cast<const char*>(
                        Diagnostics->getBufferPointer()),
                    this);
            }


            throw std::runtime_error(
                "Failed generating shader bytecode.");
        }



        blob.Bytecode.resize(
            code->getBufferSize());



        std::memcpy(
            blob.Bytecode.data(),
            code->getBufferPointer(),
            code->getBufferSize());



        return blob;
    }








    void ShaderCompiler::Impl::BuildReflection(
        slang::ProgramLayout* layout,
        ShaderReflection& reflection)
    {
        if (!layout)
            return;

        ExtractResources(layout, reflection);
    }








    void ShaderCompiler::Impl::ExtractResources(
        slang::ProgramLayout* layout,
        ShaderReflection& reflection)
    {
        const int32_t parameterCount = layout->getParameterCount();

        for (int32_t i = 0; i < parameterCount; ++i)
        {
            slang::VariableLayoutReflection* parameter =
                layout->getParameterByIndex(i);

            if (!parameter)
                continue;


            ShaderResource resource{};


            if (parameter->getName())
                resource.Name = parameter->getName();


            resource.Binding = parameter->getBindingIndex();
            resource.Space = parameter->getBindingSpace();


            switch (parameter->getCategory())
            {
            case slang::ParameterCategory::ConstantBuffer:
            {
                resource.Type = ResourceType::ConstantBuffer;

                auto typeLayout = parameter->getTypeLayout();

                if (typeLayout)
                {
                    auto elementType = typeLayout->getElementTypeLayout();

                    if (elementType)
                    {
                        resource.ElementSize = typeLayout->getSize();

                        ExtractVariables(
                            elementType,
                            resource.Variables);
                    }
                }

                break;
            }

            case slang::ParameterCategory::ShaderResource:
            {
                auto typeLayout = parameter->getTypeLayout();

                if (!typeLayout)
                    break;


                auto type = typeLayout->getType();

                auto shape = type->getResourceShape();


                switch (shape)
                {
                case SLANG_TEXTURE_2D:
                case SLANG_TEXTURE_3D:
                case SLANG_TEXTURE_CUBE:
                    resource.Type = ResourceType::Texture_SRV;
                    break;


                case SLANG_STRUCTURED_BUFFER:
                {
                    resource.Type = ResourceType::StructuredBuffer_SRV;


                    auto elementTypeLayout =
                        typeLayout->getElementTypeLayout();


                    if (elementTypeLayout)
                    {
                        resource.ElementSize =
                            elementTypeLayout->getSize();


                        ExtractVariables(
                            elementTypeLayout,
                            resource.Variables);
                    }

                    break;
                }


                default:
                    resource.Type = ResourceType::None;
                    break;
                }

                break;
            }


            case slang::ParameterCategory::SamplerState:
                resource.Type = ResourceType::Sampler;
                break;


            case slang::ParameterCategory::UnorderedAccess:
                resource.Type = ResourceType::Texture_UAV;
                break;


            default:
                continue;
            }


            reflection.Resources.push_back(
                std::move(resource));
        }
    }






    void ShaderCompiler::Impl::ExtractVariables(
        slang::TypeLayoutReflection* typeLayout,
        std::vector<ShaderVariable>& variables)
    {
        if (!typeLayout)
            return;


        for (uint32_t i = 0; i < typeLayout->getFieldCount(); i++)
        {
            auto field = typeLayout->getFieldByIndex(i);

            if (!field)
                continue;


            ShaderVariable var;

            var.Name = field->getName()
                ? field->getName()
                : "";


            auto fieldType = field->getTypeLayout();

            if (!fieldType)
                continue;


            var.Offset = field->getOffset();
            var.Size = fieldType->getSize();

            var.Rows = fieldType->getRowCount();
            var.Columns = fieldType->getColumnCount();


            // Struct
            if (fieldType->getFieldCount() > 0)
            {
                var.Type = ShaderValue::Struct;

                ExtractVariables(
                    fieldType,
                    var.Members);
            }
            else
            {
                auto fieldType = field->getTypeLayout();

                var.Type = GetShaderValue(fieldType->getType());

                if (fieldType->getKind() == slang::TypeReflection::Kind::Array)
                {
                    var.ArrayCount = fieldType->getElementCount();
                }
            }


            variables.push_back(std::move(var));
        }
    }








    ShaderType ShaderCompiler::Impl::GetShaderType(
        slang::ProgramLayout* layout) const
    {

        if (!layout ||
            layout->getEntryPointCount() == 0)
        {
            return ShaderType::Unknown;
        }



        slang::EntryPointLayout* entry =
            layout->getEntryPointByIndex(0);



        if (!entry)
            return ShaderType::Unknown;



        switch (entry->getStage())
        {

        case SLANG_STAGE_VERTEX:
            return ShaderType::Vertex;


        case SLANG_STAGE_FRAGMENT:
            return ShaderType::Pixel;


        case SLANG_STAGE_COMPUTE:
            return ShaderType::Compute;


        case SLANG_STAGE_HULL:
            return ShaderType::Hull;


        case SLANG_STAGE_DOMAIN:
            return ShaderType::Domain;


        case SLANG_STAGE_GEOMETRY:
            return ShaderType::Geometry;


        case SLANG_STAGE_MESH:
            return ShaderType::Mesh;


        case SLANG_STAGE_AMPLIFICATION:
            return ShaderType::Amplification;


        default:
            return ShaderType::Unknown;
        }
    }

    ShaderValue ShaderCompiler::Impl::GetShaderValue(
        slang::TypeReflection* type)
    {
        if (!type)
            return ShaderValue::Unknown;


        switch (type->getScalarType())
        {
        case slang::TypeReflection::ScalarType::Bool:
            return ShaderValue::Bool;

        case slang::TypeReflection::ScalarType::Int32:
            return ShaderValue::Int;

        case slang::TypeReflection::ScalarType::UInt32:
            return ShaderValue::UInt;

        case slang::TypeReflection::ScalarType::Float32:
            return ShaderValue::Float;

        case slang::TypeReflection::ScalarType::Float64:
            return ShaderValue::Double;

        default:
            return ShaderValue::Unknown;
        }
    }



    SlangCompileTarget ShaderCompiler::Impl::GetCompileTarget() const
    {

        switch (Config.API)
        {

        case GraphicsAPI::D3D12:

            return SLANG_DXIL;



        case GraphicsAPI::Vulkan:

            return SLANG_SPIRV;



        default:

            return SLANG_DXIL;
        }
    }








    SlangProfileID ShaderCompiler::Impl::GetProfile() const
    {

        switch (Config.API)
        {

        case GraphicsAPI::D3D12:

            return GlobalSession->findProfile(
                "sm_6_5");



        case GraphicsAPI::Vulkan:

            return GlobalSession->findProfile(
                "spirv_1_6");



        default:

            return GlobalSession->findProfile(
                "sm_6_5");
        }
    }








    ShaderCompiler::ShaderCompiler()
    {
    }



    ShaderCompiler::~ShaderCompiler() = default;





    bool ShaderCompiler::Init(
        const ShaderCompilerConfig& config)
    {

        m_pImpl =
            std::make_unique<Impl>(
                config);



        return m_pImpl->Initialize();
    }





    ShaderBlob ShaderCompiler::Compile(
        const ShaderCompileRequest& request)
    {
        return m_pImpl->Compile(
            request);
    }
    static const char* ShaderTypeToString(ShaderType type)
    {
        switch (type)
        {
        case ShaderType::Vertex:  return "Vertex";
        case ShaderType::Pixel:   return "Pixel";
        case ShaderType::Compute: return "Compute";
        default: return "Unknown";
        }
    }


    static const char* ResourceTypeToString(ResourceType type)
    {
        switch (type)
        {
        case ResourceType::ConstantBuffer:
            return "ConstantBuffer";

        case ResourceType::Texture_SRV:
            return "Texture SRV";

        case ResourceType::Texture_UAV:
            return "Texture UAV";

        case ResourceType::Sampler:
            return "Sampler";

        case ResourceType::StructuredBuffer_SRV:
            return "StructuredBuffer SRV";

        case ResourceType::StructuredBuffer_UAV:
            return "StructuredBuffer UAV";

        default:
            return "Unknown";
        }
    }


    static const char* ShaderValueToString(ShaderValue value)
    {
        switch (value)
        {
        case ShaderValue::Bool:
            return "Bool";

        case ShaderValue::Int:
            return "Int";

        case ShaderValue::UInt:
            return "UInt";

        case ShaderValue::Float:
            return "Float";

        case ShaderValue::Double:
            return "Double";

        case ShaderValue::Struct:
            return "Struct";

        default:
            return "Unknown";
        }
    }

    void ShaderCompiler::PrintReflection(const ShaderBlob& blob)
    {
        printf("\n========== Shader Reflection ==========\n");
        printf("Type      : %s\n", ShaderTypeToString(blob.Type));
        printf("Bytecode  : %zu bytes\n", blob.Bytecode.size());


        auto printVar = [&](const auto& self,
            const ShaderVariable& var,
            uint32_t indent) -> void
            {
                for (uint32_t i = 0; i < indent; i++)
                    printf(" ");


                char typeName[32];


                if (var.Type == ShaderValue::Struct)
                {
                    strcpy_s(typeName, "Struct");
                }
                else if (var.Rows > 1 && var.Columns > 1)
                {
                    sprintf_s(typeName,
                        "Float%ux%u",
                        var.Rows,
                        var.Columns);
                }
                else if (var.Columns > 1)
                {
                    sprintf_s(typeName,
                        "Float%u",
                        var.Columns);
                }
                else
                {
                    strcpy_s(typeName,
                        ShaderValueToString(var.Type));
                }


                printf("%-20s Type:%-10s Offset:%-4u Size:%-4u",
                    var.Name.c_str(),
                    typeName,
                    var.Offset,
                    var.Size);


                if (var.ArrayCount > 1)
                {
                    printf(" Array[%u]",
                        var.ArrayCount);
                }


                printf("\n");


                for (const auto& member : var.Members)
                {
                    self(self, member, indent + 4);
                }
            };


        for (const auto& res : blob.Reflection.Resources)
        {
            printf("\n---------------------------------------\n");

            printf("Resource : %s\n",
                res.Name.c_str());

            printf("Type     : %s\n",
                ResourceTypeToString(res.Type));

            printf("Binding  : %u\n",
                res.Binding);

            printf("Space    : %u\n",
                res.Space);


            if (res.ElementSize)
            {
                printf("Element  : %u bytes\n",
                    res.ElementSize);
            }


            if (!res.Variables.empty())
            {
                printf("\nVariables:\n");

                for (const auto& var : res.Variables)
                {
                    printVar(printVar, var, 4);
                }
            }
        }


        printf("\n---------------------------------------\n");
        printf("=======================================\n");
    }
}