#pragma once

// Helpers for passes that draw the meshes of the scene. They only use the DrawContext, so a
// pass can draw a subset of the scene, add its own geometry, or ignore the scene entirely.

#include "RenderGraph/RenderPass.h"
#include "Scene/Scene.h"

namespace LynxEngine
{
    // b2 of Shaders/Scene.slang.
    struct SceneDrawConstants
    {
        float4x4 World;
        uint32_t MaterialIndex = 0;
        uint32_t Padding[3] = {};
    };

    // Declares the scene vertex layout and the per-draw constants used by DrawScene.
    inline void DeclareSceneGeometry(RenderPass& pass)
    {
        pass.UseSceneVertexLayout();
        pass.SetPushConstants<SceneDrawConstants>();
    }

    inline void DrawMeshInstance(DrawContext& context, const Scene& scene, const MeshInstance& instance)
    {
        if (instance.Mesh >= scene.Meshes.size())
            return;

        context.BindMesh(instance.Mesh);
        context.SetPushConstants(SceneDrawConstants{ .World = instance.Transform, .MaterialIndex = instance.Material });
        for (const SubMesh& subMesh : scene.Meshes[instance.Mesh].SubMeshes)
        {
            context.DrawIndexed({
                .IndexCount = subMesh.IndexCount,
                .FirstIndex = subMesh.FirstIndex,
                .BaseVertex = subMesh.VertexOffset });
        }
    }

    // Every mesh instance of the scene.
    inline void DrawScene(DrawContext& context)
    {
        const Scene& scene = context.GetScene();
        for (const MeshInstance& instance : scene.Instances)
            DrawMeshInstance(context, scene, instance);
    }

    // Transmissive materials (glass) are drawn by ForwardPass after Lighting.
    inline bool IsTransmissive(const Scene& scene, const MeshInstance& instance)
    {
        return instance.Material < scene.Materials.size() && scene.Materials[instance.Material].Transmission > 0.0f;
    }

    inline void DrawOpaqueScene(DrawContext& context)
    {
        const Scene& scene = context.GetScene();
        for (const MeshInstance& instance : scene.Instances)
        {
            if (!IsTransmissive(scene, instance))
                DrawMeshInstance(context, scene, instance);
        }
    }

    inline void DrawTransmissiveScene(DrawContext& context)
    {
        const Scene& scene = context.GetScene();
        for (const MeshInstance& instance : scene.Instances)
        {
            if (IsTransmissive(scene, instance))
                DrawMeshInstance(context, scene, instance);
        }
    }
}
