#include "RenderGraph/RenderGraph.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <utility>

namespace LynxEngine
{
    namespace
    {
        bool SplitFieldPath(std::string_view path, std::string& pass, std::string& field)
        {
            const size_t dot = path.find('.');
            if (dot == std::string_view::npos || dot == 0 || dot + 1 >= path.size())
                return false;

            pass = std::string(path.substr(0, dot));
            field = std::string(path.substr(dot + 1));
            return true;
        }

        uint32_t UsageBit(ResourceUsage usage)
        {
            return 1u << uint32_t(usage);
        }

        bool IsAttachment(ResourceUsage usage)
        {
            return usage == ResourceUsage::RenderTarget || usage == ResourceUsage::DepthStencil || usage == ResourceUsage::DepthRead;
        }

        std::string JoinErrors(const std::vector<std::string>& errors)
        {
            std::string text = "RenderGraph compilation failed:";
            for (const std::string& error : errors)
                text += "\n  - " + error;
            return text;
        }

        template <typename T>
        RhiHandle FindOrAdd(std::vector<T>& table, const T& value)
        {
            for (size_t i = 0; i < table.size(); ++i)
            {
                if (table[i] == value)
                    return RhiHandle(i);
            }
            table.push_back(value);
            return RhiHandle(table.size() - 1);
        }
    }

    // ----------------------------------------------------------------------------------------
    // Editing
    // ----------------------------------------------------------------------------------------

    RenderPass& RenderGraph::AddPass(std::string name, std::unique_ptr<RenderPass> pass)
    {
        // Adding a pass with an existing name replaces it; its edges stay connected.
        if (const int index = FindPassIndex(name); index >= 0)
        {
            m_Passes[size_t(index)].Pass = std::move(pass);
            return *m_Passes[size_t(index)].Pass;
        }

        m_Passes.push_back({ std::move(name), std::move(pass) });
        return *m_Passes.back().Pass;
    }

    bool RenderGraph::RemovePass(std::string_view name)
    {
        const int index = FindPassIndex(name);
        if (index < 0)
            return false;

        std::erase_if(m_Edges, [&](const GraphEdge& e) { return e.SrcPass == name || e.DstPass == name; });
        if (m_OutputPass == name)
        {
            m_OutputPass.clear();
            m_OutputField.clear();
        }
        m_Passes.erase(m_Passes.begin() + index);
        return true;
    }

    RenderPass* RenderGraph::GetPass(std::string_view name) const
    {
        const int index = FindPassIndex(name);
        return index >= 0 ? m_Passes[size_t(index)].Pass.get() : nullptr;
    }

    void RenderGraph::AddEdge(std::string_view src, std::string_view dst)
    {
        GraphEdge edge;
        if (!SplitFieldPath(src, edge.SrcPass, edge.SrcField))
            edge.SrcPass = std::string(src); // reported by Compile()
        if (!SplitFieldPath(dst, edge.DstPass, edge.DstField))
            edge.DstPass = std::string(dst);
        m_Edges.push_back(std::move(edge));
    }

    bool RenderGraph::RemoveEdge(std::string_view dst)
    {
        std::string pass, field;
        if (!SplitFieldPath(dst, pass, field))
            return false;

        return std::erase_if(m_Edges, [&](const GraphEdge& e) { return e.DstPass == pass && e.DstField == field; }) > 0;
    }

    void RenderGraph::MarkOutput(std::string_view field)
    {
        if (!SplitFieldPath(field, m_OutputPass, m_OutputField))
        {
            m_OutputPass = std::string(field);
            m_OutputField.clear();
        }
    }

    std::vector<std::string> RenderGraph::GetPassNames() const
    {
        std::vector<std::string> names;
        for (const PassEntry& entry : m_Passes)
            names.push_back(entry.Name);
        return names;
    }

    int RenderGraph::FindPassIndex(std::string_view name) const
    {
        for (size_t i = 0; i < m_Passes.size(); ++i)
        {
            if (m_Passes[i].Name == name)
                return int(i);
        }
        return -1;
    }

    // ----------------------------------------------------------------------------------------
    // Compilation
    // ----------------------------------------------------------------------------------------

    std::expected<CompiledGraph, std::string> RenderGraph::Compile(const GraphCompileDesc& desc) const
    {
        std::vector<std::string> errors;
        const size_t passCount = m_Passes.size();
        const auto passName = [&](int index) -> const std::string& { return m_Passes[size_t(index)].Name; };

        // 1. Resolve and validate edges. ------------------------------------------------------
        struct ResolvedEdge
        {
            int Src = -1;
            int Dst = -1;
            const PassField* SrcField = nullptr;
            const PassField* DstField = nullptr;
        };

        std::vector<ResolvedEdge> edges;
        std::map<std::pair<int, std::string>, size_t> incoming; // (dst pass, dst field) -> edge

        for (const GraphEdge& e : m_Edges)
        {
            const std::string label = std::format("{}.{} -> {}.{}", e.SrcPass, e.SrcField, e.DstPass, e.DstField);
            if (e.SrcField.empty() || e.DstField.empty())
            {
                errors.push_back(std::format("Edge '{}': fields must be written as \"Pass.field\"", label));
                continue;
            }

            const int src = FindPassIndex(e.SrcPass);
            const int dst = FindPassIndex(e.DstPass);
            if (src < 0 || dst < 0)
            {
                errors.push_back(std::format("Edge '{}': unknown pass '{}'", label, src < 0 ? e.SrcPass : e.DstPass));
                continue;
            }

            const PassField* srcField = m_Passes[size_t(src)].Pass->FindField(e.SrcField);
            const PassField* dstField = m_Passes[size_t(dst)].Pass->FindField(e.DstField);
            if (!srcField || !dstField)
            {
                errors.push_back(std::format("Edge '{}': unknown field '{}'", label, !srcField ? e.SrcField : e.DstField));
                continue;
            }
            if (src == dst)
            {
                errors.push_back(std::format("Edge '{}': a pass cannot feed itself", label));
                continue;
            }
            if (srcField->Access == FieldAccess::Read)
            {
                errors.push_back(std::format("Edge '{}': source '{}.{}' is an input, not an output", label, e.SrcPass, e.SrcField));
                continue;
            }
            if (dstField->Access == FieldAccess::Write)
            {
                errors.push_back(std::format("Edge '{}': destination '{}.{}' is an output and creates its own resource", label, e.DstPass, e.DstField));
                continue;
            }
            if (srcField->Kind != dstField->Kind)
            {
                errors.push_back(std::format("Edge '{}': connects a texture with a buffer", label));
                continue;
            }
            if (!incoming.try_emplace({ dst, e.DstField }, edges.size()).second)
            {
                errors.push_back(std::format("'{}.{}' has more than one incoming edge", e.DstPass, e.DstField));
                continue;
            }

            edges.push_back({ src, dst, srcField, dstField });
        }

        // 2. Output. -------------------------------------------------------------------------
        const int outputPass = FindPassIndex(m_OutputPass);
        const PassField* outputField = outputPass >= 0 ? m_Passes[size_t(outputPass)].Pass->FindField(m_OutputField) : nullptr;

        if (m_OutputPass.empty())
            errors.push_back("No output marked. Call MarkOutput(\"Pass.field\").");
        else if (!outputField)
            errors.push_back(std::format("Output '{}.{}' does not exist", m_OutputPass, m_OutputField));
        else if (outputField->Access == FieldAccess::Read || outputField->Kind != ResourceKind::Texture)
            errors.push_back(std::format("Output '{}.{}' must be a texture output of its pass", m_OutputPass, m_OutputField));

        if (!errors.empty())
            return std::unexpected(JoinErrors(errors));

        // 3. Culling: keep only passes the output depends on. ------------------------------
        std::vector<bool> active(passCount, false);
        std::vector<int> stack{ outputPass };
        active[size_t(outputPass)] = true;
        while (!stack.empty())
        {
            const int pass = stack.back();
            stack.pop_back();
            for (const ResolvedEdge& e : edges)
            {
                if (e.Dst == pass && !active[size_t(e.Src)])
                {
                    active[size_t(e.Src)] = true;
                    stack.push_back(e.Src);
                }
            }
        }

        CompiledGraph graph;
        graph.Width = desc.BackBufferWidth;
        graph.Height = desc.BackBufferHeight;

        for (size_t i = 0; i < passCount; ++i)
        {
            if (!active[i])
            {
                graph.CulledPasses.push_back(m_Passes[i].Name);
                graph.Warnings.push_back(std::format("Pass '{}' does not contribute to the output and was culled", m_Passes[i].Name));
            }
        }

        // 4. Execution order: topological sort of the edges (Kahn). Ties are broken by the
        //    order in which passes were added, so the result is deterministic.
        std::vector<std::vector<int>> successors(passCount);
        std::vector<int> inDegree(passCount, 0);
        std::set<std::pair<int, int>> uniquePairs;
        for (const ResolvedEdge& e : edges)
        {
            if (active[size_t(e.Src)] && active[size_t(e.Dst)] && uniquePairs.insert({ e.Src, e.Dst }).second)
            {
                successors[size_t(e.Src)].push_back(e.Dst);
                ++inDegree[size_t(e.Dst)];
            }
        }

        std::set<int> ready;
        size_t activeCount = 0;
        for (size_t i = 0; i < passCount; ++i)
        {
            if (!active[i])
                continue;
            ++activeCount;
            if (inDegree[i] == 0)
                ready.insert(int(i));
        }

        std::vector<int> order;
        while (!ready.empty())
        {
            const int pass = *ready.begin();
            ready.erase(ready.begin());
            order.push_back(pass);
            for (const int next : successors[size_t(pass)])
            {
                if (--inDegree[size_t(next)] == 0)
                    ready.insert(next);
            }
        }

        if (order.size() != activeCount)
        {
            std::string cycle;
            for (size_t i = 0; i < passCount; ++i)
            {
                if (active[i] && inDegree[i] > 0)
                    cycle += (cycle.empty() ? "" : ", ") + m_Passes[i].Name;
            }
            errors.push_back("Cycle detected between passes: " + cycle);
            return std::unexpected(JoinErrors(errors));
        }

        std::vector<uint32_t> orderIndex(passCount, ~0u);
        for (size_t i = 0; i < order.size(); ++i)
            orderIndex[size_t(order[i])] = uint32_t(i);

        // 5. Resources and bindings, walking the passes in execution order. Write fields create
        //    resources; Read / ReadWrite fields take the resource of their incoming edge.
        std::map<std::pair<int, std::string>, RhiHandle> fieldResource;

        for (size_t position = 0; position < order.size(); ++position)
        {
            const int passIndex = order[position];
            const PassEntry& entry = m_Passes[size_t(passIndex)];
            const RenderPass& pass = *entry.Pass;

            CompiledPass compiled;
            compiled.Name = entry.Name;
            compiled.Pass = &pass;

            for (const PassField& field : pass.GetFields())
            {
                RhiHandle handle = kInvalidHandle;

                if (field.Access == FieldAccess::Write)
                {
                    CompiledResource resource;
                    resource.Handle = RhiHandle(graph.Resources.size());
                    resource.Name = entry.Name + "." + field.Name;
                    resource.Kind = field.Kind;
                    resource.FirstPass = uint32_t(position);

                    if (field.Kind == ResourceKind::Texture)
                    {
                        resource.Texture = field.Texture;
                        if (field.Texture.SizeMode == TextureSizeMode::BackBufferRelative)
                        {
                            resource.Texture.Width = std::max(1u, uint32_t(float(desc.BackBufferWidth) * field.Texture.Scale));
                            resource.Texture.Height = std::max(1u, uint32_t(float(desc.BackBufferHeight) * field.Texture.Scale));
                        }
                        if (resource.Texture.Format == TextureFormat::Unknown || resource.Texture.Width == 0 || resource.Texture.Height == 0)
                            errors.push_back(std::format("'{}' has an invalid texture description", resource.Name));
                    }
                    else
                    {
                        resource.Buffer = field.Buffer;
                        if (field.Buffer.ElementCount == 0 || field.Buffer.ElementSize == 0)
                            errors.push_back(std::format("'{}' has an invalid buffer description", resource.Name));
                    }

                    handle = resource.Handle;
                    graph.Resources.push_back(std::move(resource));
                }
                else
                {
                    const auto edge = incoming.find({ passIndex, field.Name });
                    if (edge == incoming.end())
                    {
                        errors.push_back(std::format("Input '{}.{}' is not connected", entry.Name, field.Name));
                        continue;
                    }

                    const ResolvedEdge& e = edges[edge->second];
                    const auto source = fieldResource.find({ e.Src, e.SrcField->Name });
                    if (source == fieldResource.end())
                        continue; // the source itself failed; already reported

                    handle = source->second;
                }

                fieldResource[{ passIndex, field.Name }] = handle;

                CompiledResource& resource = graph.Resources[handle];
                resource.UsageMask |= UsageBit(field.Usage);
                resource.LastPass = uint32_t(position);

                compiled.Bindings.push_back({ field.Name, handle, field.Access, field.Usage, field.Clear });
            }

            for (const ShaderDesc& shader : pass.GetShaders())
                compiled.Shaders.push_back(FindOrAdd(graph.Shaders, shader));
            for (const SamplerDesc& sampler : pass.GetSamplers())
                compiled.Samplers.push_back(FindOrAdd(graph.Samplers, sampler));

            for (const ResolvedEdge& e : edges)
            {
                if (e.Dst == passIndex)
                    compiled.DependsOn.push_back(orderIndex[size_t(e.Src)]);
            }
            std::ranges::sort(compiled.DependsOn);
            compiled.DependsOn.erase(std::unique(compiled.DependsOn.begin(), compiled.DependsOn.end()), compiled.DependsOn.end());

            graph.Passes.push_back(std::move(compiled));
        }

        // 6. Validation. ---------------------------------------------------------------------

        // A resource modified in place (ReadWrite) must not be read by anybody else from the
        // same producer, otherwise it is ambiguous which version the other reader sees.
        for (const ResolvedEdge& e : edges)
        {
            if (!active[size_t(e.Dst)] || e.DstField->Access != FieldAccess::ReadWrite)
                continue;

            for (const ResolvedEdge& other : edges)
            {
                if (&other != &e && active[size_t(other.Dst)] && other.Src == e.Src && other.SrcField == e.SrcField)
                {
                    errors.push_back(std::format(
                        "'{}.{}' is modified in place by '{}.{}' and also read by '{}.{}'; the order is ambiguous",
                        passName(e.Src), e.SrcField->Name, passName(e.Dst), e.DstField->Name,
                        passName(other.Dst), other.DstField->Name));
                }
            }
        }

        for (const CompiledPass& pass : graph.Passes)
        {
            uint32_t colorCount = 0;
            uint32_t depthCount = 0;
            uint32_t copySrc = 0;
            uint32_t copyDst = 0;
            uint32_t width = 0;
            uint32_t height = 0;

            for (const CompiledBinding& binding : pass.Bindings)
            {
                const CompiledResource& resource = graph.Resources[binding.Resource];
                const bool isTexture = resource.Kind == ResourceKind::Texture;
                const bool isDepth = isTexture && IsDepthFormat(resource.Texture.Format);
                const std::string where = pass.Name + "." + binding.Field;

                switch (binding.Usage)
                {
                case ResourceUsage::RenderTarget:
                    ++colorCount;
                    if (!isTexture || isDepth)
                        errors.push_back(std::format("'{}' is used as render target but is not a color texture", where));
                    break;
                case ResourceUsage::DepthStencil:
                case ResourceUsage::DepthRead:
                    ++depthCount;
                    if (!isDepth)
                        errors.push_back(std::format("'{}' is used as depth attachment but has no depth format", where));
                    break;
                case ResourceUsage::UnorderedAccess:
                    if (isDepth)
                        errors.push_back(std::format("'{}' cannot use a depth texture as UAV", where));
                    break;
                case ResourceUsage::CopySource: ++copySrc; break;
                case ResourceUsage::CopyDest:   ++copyDst; break;
                case ResourceUsage::ShaderResource: break;
                }

                if (IsAttachment(binding.Usage) && isTexture)
                {
                    if (width != 0 && (resource.Texture.Width != width || resource.Texture.Height != height))
                        errors.push_back(std::format("Pass '{}': all attachments must have the same size", pass.Name));
                    width = resource.Texture.Width;
                    height = resource.Texture.Height;
                }
            }

            const RenderPass& rp = *pass.Pass;
            switch (rp.GetType())
            {
            case PassType::Graphics:
                if (colorCount + depthCount == 0)
                    errors.push_back(std::format("Graphics pass '{}' has no render target or depth attachment", pass.Name));
                if (colorCount > 8 || depthCount > 1)
                    errors.push_back(std::format("Graphics pass '{}' has too many attachments", pass.Name));
                if (!rp.FindShader(ShaderType::Vertex) && !rp.GetShaders().empty())
                    errors.push_back(std::format("Graphics pass '{}' declares shaders but no vertex shader", pass.Name));                break;
            case PassType::Compute:
                if (!rp.FindShader(ShaderType::Compute))
                    errors.push_back(std::format("Compute pass '{}' declares no compute shader", pass.Name));
                if (colorCount + depthCount != 0)
                    errors.push_back(std::format("Compute pass '{}' cannot use render targets", pass.Name));
                break;
            case PassType::Copy:
                if (copySrc != 1 || copyDst != 1 || pass.Bindings.size() != 2)
                    errors.push_back(std::format("Copy pass '{}' needs exactly one CopySource and one CopyDest field", pass.Name));
                break;
            }
        }

        // The output is presented, so it has to look like the back buffer.
        if (const auto output = fieldResource.find({ outputPass, m_OutputField }); output != fieldResource.end())
        {
            CompiledResource& resource = graph.Resources[output->second];
            resource.IsGraphOutput = true;
            graph.Output = resource.Handle;

            if (resource.Texture.Format != desc.BackBufferFormat ||
                resource.Texture.Width != desc.BackBufferWidth || resource.Texture.Height != desc.BackBufferHeight)
            {
                errors.push_back(std::format("Output '{}' must match the back buffer ({}x{} {}), it is {}x{} {}",
                    resource.Name, desc.BackBufferWidth, desc.BackBufferHeight, ToString(desc.BackBufferFormat),
                    resource.Texture.Width, resource.Texture.Height, ToString(resource.Texture.Format)));
            }
            if (resource.HasUsage(ResourceUsage::UnorderedAccess))
                errors.push_back(std::format("Output '{}' cannot be used as UAV (it is the swap chain image)", resource.Name));
        }

        for (const CompiledResource& resource : graph.Resources)
        {
            if (!resource.IsGraphOutput && resource.FirstPass == resource.LastPass)
                graph.Warnings.push_back(std::format("Resource '{}' is written but never read", resource.Name));
        }

        if (!errors.empty())
            return std::unexpected(JoinErrors(errors));

        return graph;
    }

    std::string DescribeGraph(const CompiledGraph& graph)
    {
        static constexpr const char* kAccess[] = { "read", "write", "read-write" };

        std::string text = std::format("Execution order ({} passes, {}x{}):\n", graph.Passes.size(), graph.Width, graph.Height);
        for (size_t i = 0; i < graph.Passes.size(); ++i)
        {
            const CompiledPass& pass = graph.Passes[i];
            std::string deps;
            for (const uint32_t dep : pass.DependsOn)
                deps += (deps.empty() ? "" : ", ") + graph.Passes[dep].Name;

            text += std::format("  {}. {:<13} depends on [{}]\n", i, pass.Name, deps);
            for (const CompiledBinding& binding : pass.Bindings)
            {
                text += std::format("       {:<8} {:<10} {:<15} -> #{} {}\n", binding.Field, kAccess[size_t(binding.Access)],
                    ToString(binding.Usage), binding.Resource, graph.Resources[binding.Resource].Name);
            }
        }

        text += std::format("Resources ({}):\n", graph.Resources.size());
        for (const CompiledResource& r : graph.Resources)
        {
            const std::string desc = r.Kind == ResourceKind::Texture
                ? std::format("texture {}x{} {}", r.Texture.Width, r.Texture.Height, ToString(r.Texture.Format))
                : std::format("buffer  {} x {} bytes", r.Buffer.ElementCount, r.Buffer.ElementSize);
            text += std::format("  #{} {:<20} {:<32} passes [{}..{}]{}\n", r.Handle, r.Name, desc, r.FirstPass, r.LastPass,
                r.IsGraphOutput ? "  <- graph output (back buffer)" : "");
        }

        text += std::format("Shaders: {}, samplers: {}\n", graph.Shaders.size(), graph.Samplers.size());
        for (const std::string& warning : graph.Warnings)
            text += "Warning: " + warning + "\n";
        return text;
    }
}
