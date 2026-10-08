#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace LynxEngine
{
    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;

        // Layout of the demo scene (see CreateDemoScene).
        enum DemoMesh : uint32_t { kPlaneMesh, kCubeMesh, kSphereMesh };
        constexpr uint32_t kCenterCube = 1;      // instance index
        constexpr uint32_t kFirstOrbitCube = 2;  // instance index
        constexpr uint32_t kOrbitCubeCount = 6;
        constexpr uint32_t kSphereCount = 8;

        struct Vertex
        {
            float3 Position;
            float3 Normal;
            float2 TexCoord;
        };

        MeshData BuildMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
        {
            VertexStream stream;
            stream.Attributes = {
                { VertexSemantic::Position, VertexFormat::Float32x3 },
                { VertexSemantic::Normal, VertexFormat::Float32x3 },
                { VertexSemantic::TexCoord, VertexFormat::Float32x2 },
            };
            stream.CalculateLayout();
            stream.VertexCount = uint32_t(vertices.size());
            stream.Data.resize(vertices.size() * sizeof(Vertex));
            std::memcpy(stream.Data.data(), vertices.data(), stream.Data.size());

            MeshData mesh;
            mesh.VertexStreams.push_back(std::move(stream));
            mesh.IndexFormat = IndexFormat::UInt32;
            mesh.IndexBuffer.resize(indices.size() * sizeof(uint32_t));
            std::memcpy(mesh.IndexBuffer.data(), indices.data(), mesh.IndexBuffer.size());
            mesh.SubMeshes.push_back({ .Topology = PrimitiveTopology::TriangleList, .FirstIndex = 0, .IndexCount = uint32_t(indices.size()) });
            return mesh;
        }

        TextureData MakeTexture(uint32_t size, TextureFormat format)
        {
            TextureData texture;
            texture.Width = size;
            texture.Height = size;
            texture.Format = format;

            TextureSubresource level;
            level.Width = size;
            level.Height = size;
            level.RowPitch = size * 4;
            level.SlicePitch = level.RowPitch * size;
            level.Pixels.resize(level.SlicePitch);
            texture.Subresources.push_back(std::move(level));
            return texture;
        }

        MaterialData Material(float r, float g, float b, float metallic, float roughness)
        {
            MaterialData material;
            material.BaseColor[0] = r;
            material.BaseColor[1] = g;
            material.BaseColor[2] = b;
            material.Metallic = metallic;
            material.Roughness = roughness;
            return material;
        }
    }

    MeshData CreateCubeMesh()
    {
        struct Face { float3 Normal, U, V; };
        const Face faces[] = {
            { { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
            { { -1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
            { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
            { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
            { { 0, 0, 1 }, { -1, 0, 0 }, { 0, 1, 0 } },
            { { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        };

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        for (const Face& face : faces)
        {
            const uint32_t base = uint32_t(vertices.size());
            const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
            for (const auto& c : corners)
            {
                const float3 p = (face.Normal + face.U * c[0] + face.V * c[1]) * 0.5f;
                vertices.push_back({ p, face.Normal, { c[0] * 0.5f + 0.5f, 0.5f - c[1] * 0.5f } });
            }
            indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
        return BuildMesh(vertices, indices);
    }

    MeshData CreatePlaneMesh(float halfSize, float uvScale)
    {
        const std::vector<Vertex> vertices = {
            { { -halfSize, 0, -halfSize }, { 0, 1, 0 }, { 0, uvScale } },
            { { halfSize, 0, -halfSize }, { 0, 1, 0 }, { uvScale, uvScale } },
            { { halfSize, 0, halfSize }, { 0, 1, 0 }, { uvScale, 0 } },
            { { -halfSize, 0, halfSize }, { 0, 1, 0 }, { 0, 0 } },
        };
        return BuildMesh(vertices, { 0, 2, 1, 0, 3, 2 });
    }

    // UV sphere of radius 0.5. Rows of vertices go from the north pole (v = 0) to the south pole.
    MeshData CreateSphereMesh(uint32_t segments, uint32_t rings)
    {
        segments = std::max(3u, segments);
        rings = std::max(2u, rings);

        std::vector<Vertex> vertices;
        for (uint32_t r = 0; r <= rings; ++r)
        {
            const float v = float(r) / float(rings);
            const float theta = v * kPi;
            for (uint32_t s = 0; s <= segments; ++s)
            {
                const float u = float(s) / float(segments);
                const float phi = u * 2.0f * kPi;
                const float3 normal{ std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) };
                vertices.push_back({ normal * 0.5f, normal, { u, v } });
            }
        }

        std::vector<uint32_t> indices;
        for (uint32_t r = 0; r < rings; ++r)
        {
            for (uint32_t s = 0; s < segments; ++s)
            {
                const uint32_t a = r * (segments + 1) + s;
                const uint32_t b = a + segments + 1;
                indices.insert(indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
            }
        }
        return BuildMesh(vertices, indices);
    }

    TextureData CreateCheckerTexture(uint32_t size, uint32_t cells, uint32_t colorA, uint32_t colorB, TextureFormat format)
    {
        TextureData texture = MakeTexture(size, format);
        std::vector<std::byte>& pixels = texture.Subresources[0].Pixels;

        const uint32_t cellSize = std::max(1u, size / cells);
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                const uint32_t color = ((x / cellSize + y / cellSize) & 1) ? colorA : colorB;
                std::memcpy(&pixels[(y * size + x) * 4], &color, 4); // 0xAABBGGRR -> R, G, B, A
            }
        }
        return texture;
    }

    TextureData CreateTileNormalTexture(uint32_t size, uint32_t cells, float bevel, float strength)
    {
        TextureData texture = MakeTexture(size, TextureFormat::RGBA8_UNORM);
        std::vector<std::byte>& pixels = texture.Subresources[0].Pixels;

        // Height: 0 in the grooves between tiles, rising to 1 over 'bevel' (fraction of a tile).
        const float cellSize = float(size) / float(std::max(1u, cells));
        const auto height = [&](int x, int y)
        {
            const float fx = std::fmod(float((x + int(size)) % int(size)) + 0.5f, cellSize) / cellSize;
            const float fy = std::fmod(float((y + int(size)) % int(size)) + 0.5f, cellSize) / cellSize;
            const float edge = std::min({ fx, 1.0f - fx, fy, 1.0f - fy });
            const float t = std::clamp(edge / std::max(bevel, 1e-4f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };

        for (int y = 0; y < int(size); ++y)
        {
            for (int x = 0; x < int(size); ++x)
            {
                // Rows go down the image, the normal map's +Y goes up: dh/dup = -dh/drow.
                const float dx = (height(x + 1, y) - height(x - 1, y)) * 0.5f * strength;
                const float dup = -(height(x, y + 1) - height(x, y - 1)) * 0.5f * strength;
                const float3 n = Normalize({ -dx, -dup, 1.0f });

                std::byte* p = &pixels[(size_t(y) * size + size_t(x)) * 4];
                p[0] = std::byte(uint8_t(std::lround((n.x * 0.5f + 0.5f) * 255.0f)));
                p[1] = std::byte(uint8_t(std::lround((n.y * 0.5f + 0.5f) * 255.0f)));
                p[2] = std::byte(uint8_t(std::lround((n.z * 0.5f + 0.5f) * 255.0f)));
                p[3] = std::byte(255);
            }
        }
        return texture;
    }

    Scene CreateDemoScene()
    {
        Scene scene;
        scene.Meshes.push_back(CreatePlaneMesh(12.0f, 12.0f)); // kPlaneMesh
        scene.Meshes.push_back(CreateCubeMesh());               // kCubeMesh
        scene.Meshes.push_back(CreateSphereMesh(48, 24));       // kSphereMesh

        // Textures (indices into Scene::Textures).
        const auto addTexture = [&](TextureData texture)
        {
            scene.Textures.push_back(std::move(texture));
            return uint32_t(scene.Textures.size() - 1);
        };
        const uint32_t floorColor = addTexture(CreateCheckerTexture(256, 8, 0xFF505050, 0xFF8A8A8A));
        const uint32_t floorNormal = addTexture(CreateTileNormalTexture(256, 8, 0.08f, 6.0f));
        // G = roughness, B = metallic: polished and rough tiles, no metal.
        const uint32_t floorRoughness = addTexture(CreateCheckerTexture(256, 8, 0xFF003000, 0xFF00C000, TextureFormat::RGBA8_UNORM));
        const uint32_t orangeChecker = addTexture(CreateCheckerTexture(128, 4, 0xFF2080F0, 0xFFE0E0E0));
        const uint32_t blueChecker = addTexture(CreateCheckerTexture(128, 4, 0xFFE07020, 0xFFE0E0E0));
        const uint32_t greenChecker = addTexture(CreateCheckerTexture(128, 2, 0xFF40C040, 0xFF206020));
        const uint32_t cubeTiles = addTexture(CreateTileNormalTexture(128, 4, 0.12f, 4.0f));

        // Materials (indices into Scene::Materials).
        const auto addMaterial = [&](MaterialData material)
        {
            scene.Materials.push_back(material);
            return uint32_t(scene.Materials.size() - 1);
        };

        MaterialData floor = Material(1.0f, 1.0f, 1.0f, 1.0f, 1.0f); // factors multiply the textures
        floor.BaseColorTexture = floorColor;
        floor.NormalTexture = floorNormal;
        floor.MetallicRoughnessTexture = floorRoughness;
        const uint32_t floorMaterial = addMaterial(floor);

        const uint32_t goldMaterial = addMaterial(Material(1.0f, 0.766f, 0.336f, 1.0f, 0.3f));

        MaterialData orange = Material(1.0f, 1.0f, 1.0f, 0.0f, 0.35f);
        orange.BaseColorTexture = orangeChecker;
        MaterialData blue = Material(1.0f, 1.0f, 1.0f, 0.0f, 0.8f);
        blue.BaseColorTexture = blueChecker;
        MaterialData green = Material(1.0f, 1.0f, 1.0f, 0.0f, 0.5f);
        green.BaseColorTexture = greenChecker;
        green.NormalTexture = cubeTiles;
        MaterialData copper = Material(0.955f, 0.638f, 0.538f, 1.0f, 0.45f);
        copper.NormalTexture = cubeTiles;
        MaterialData glowing = Material(0.05f, 0.05f, 0.05f, 0.0f, 0.6f);
        glowing.Emissive[0] = 4.0f;
        glowing.Emissive[1] = 1.2f;
        glowing.Emissive[2] = 0.2f;
        const uint32_t cubeMaterials[] = { addMaterial(orange), addMaterial(blue), addMaterial(green), addMaterial(copper), addMaterial(glowing) };

        // Instances: floor, center cube, orbiting cubes, then a ring of spheres sweeping roughness
        // (first half metallic silver, second half red plastic).
        scene.Instances.push_back({ .Mesh = kPlaneMesh, .Material = floorMaterial, .Transform = float4x4::Identity() });
        scene.Instances.push_back({ .Mesh = kCubeMesh, .Material = goldMaterial, .Transform = float4x4::Identity() });
        for (uint32_t i = 0; i < kOrbitCubeCount; ++i)
            scene.Instances.push_back({ .Mesh = kCubeMesh, .Material = cubeMaterials[i % std::size(cubeMaterials)], .Transform = float4x4::Identity() });

        for (uint32_t i = 0; i < kSphereCount; ++i)
        {
            const uint32_t half = kSphereCount / 2;
            const float roughness = 0.08f + 0.84f * float(i % half) / float(half - 1);
            const uint32_t material = i < half
                ? addMaterial(Material(0.97f, 0.96f, 0.91f, 1.0f, roughness))
                : addMaterial(Material(0.8f, 0.05f, 0.05f, 0.0f, roughness));

            const float angle = (float(i) + 0.5f) * 2.0f * kPi / float(kSphereCount);
            const float3 position{ std::cos(angle) * 6.5f, 0.6f, std::sin(angle) * 6.5f };
            scene.Instances.push_back({ .Mesh = kSphereMesh, .Material = material,
                .Transform = float4x4::Translation(position) * float4x4::Scale({ 1.2f, 1.2f, 1.2f }) });
        }

        scene.MainCamera.SetPerspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        AnimateDemoScene(scene, 0.0f);
        return scene;
    }

    void AnimateDemoScene(Scene& scene, float time)
    {
        // The center cube spins, the others circle around it. The floor and the spheres are static.
        scene.Instances[kCenterCube].Transform = float4x4::Translation({ 0, 1.0f, 0 }) * float4x4::RotationY(time * 0.5f) * float4x4::Scale({ 2, 2, 2 });

        for (uint32_t i = 0; i < kOrbitCubeCount; ++i)
        {
            const float angle = time * 0.3f + float(i) * (2.0f * kPi / float(kOrbitCubeCount));
            const float3 position{ std::cos(angle) * 4.0f, 0.9f + 0.25f * std::sin(time * 2.0f + float(i)), std::sin(angle) * 4.0f };
            scene.Instances[kFirstOrbitCube + i].Transform =
                float4x4::Translation(position) * float4x4::RotationY(-angle * 2.0f) * float4x4::RotationX(time + float(i));
        }

        const float cameraAngle = time * 0.1f;
        scene.MainCamera.LookAt({ std::sin(cameraAngle) * 9.0f, 4.0f, -std::cos(cameraAngle) * 9.0f }, { 0.0f, 0.5f, 0.0f });
    }
}
