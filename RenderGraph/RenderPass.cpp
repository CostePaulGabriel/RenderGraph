#include "RenderGraph/RenderPass.h"

#include <utility>

namespace LynxEngine
{
    RenderPass::RenderPass(PassType type)
        : m_Type(type)
    {
    }

    PassField& RenderPass::AddField(PassField field)
    {
        m_Fields.push_back(std::move(field));
        return m_Fields.back();
    }

    PassField& RenderPass::Input(std::string name, ResourceUsage usage)
    {
        PassField field;
        field.Name = std::move(name);
        field.Access = FieldAccess::Read;
        field.Usage = usage;
        field.Kind = ResourceKind::Texture;
        return AddField(std::move(field));
    }

    PassField& RenderPass::InputBuffer(std::string name, ResourceUsage usage)
    {
        PassField field;
        field.Name = std::move(name);
        field.Access = FieldAccess::Read;
        field.Usage = usage;
        field.Kind = ResourceKind::Buffer;
        return AddField(std::move(field));
    }

    PassField& RenderPass::Output(std::string name, const TextureDesc& desc, ResourceUsage usage)
    {
        PassField field;
        field.Name = std::move(name);
        field.Access = FieldAccess::Write;
        field.Usage = usage;
        field.Kind = ResourceKind::Texture;
        field.Texture = desc;
        return AddField(std::move(field));
    }

    PassField& RenderPass::OutputBuffer(std::string name, const BufferDesc& desc, ResourceUsage usage)
    {
        PassField field;
        field.Name = std::move(name);
        field.Access = FieldAccess::Write;
        field.Usage = usage;
        field.Kind = ResourceKind::Buffer;
        field.Buffer = desc;
        return AddField(std::move(field));
    }

    PassField& RenderPass::InOut(std::string name, ResourceUsage usage)
    {
        PassField field;
        field.Name = std::move(name);
        field.Access = FieldAccess::ReadWrite;
        field.Usage = usage;
        field.Kind = ResourceKind::Texture;
        return AddField(std::move(field));
    }

    void RenderPass::AddShader(ShaderType type, std::string file, std::string entryPoint)
    {
        ShaderDesc desc;
        desc.Type = type;
        desc.DebugName = file + ":" + entryPoint;
        desc.File = std::move(file);
        desc.EntryPoint = std::move(entryPoint);
        m_Shaders.push_back(std::move(desc));
    }

    void RenderPass::AddSampler(const SamplerDesc& desc)
    {
        m_Samplers.push_back(desc);
    }

    const PassField* RenderPass::FindField(std::string_view name) const
    {
        for (const PassField& field : m_Fields)
        {
            if (field.Name == name)
                return &field;
        }
        return nullptr;
    }

    PassField* RenderPass::FindField(std::string_view name)
    {
        return const_cast<PassField*>(std::as_const(*this).FindField(name));
    }

    const ShaderDesc* RenderPass::FindShader(ShaderType type) const
    {
        for (const ShaderDesc& shader : m_Shaders)
        {
            if (shader.Type == type)
                return &shader;
        }
        return nullptr;
    }
}
