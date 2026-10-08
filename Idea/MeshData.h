#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace LynxEngine
{

    enum class VertexSemantic : uint8_t
    {
        None = 0,
        Position,
        Normal,
        Tangent,
        Bitangent,
        TexCoord,
        TexIndex,
        Color,
        BlendIndice,
        BlendWeight
    };

    enum class VertexFormat : uint32_t
    {
        Unknown = 0,

        Float32,
        Float32x2,
        Float32x3,
        Float32x4,

        Float16,
        Float16x2,
        Float16x4,

        Int32,
        Int32x2,
        Int32x3,
        Int32x4,

        UInt32,
        UInt32x2,
        UInt32x3,
        UInt32x4,

        UInt8x4,
        UInt8x4Normalized,

        Int8x4,
        Int8x4Normalized
    };

    enum class IndexFormat : uint8_t
    {
        UInt16,
        UInt32
    };

    enum class PrimitiveTopology : uint8_t
    {
        TriangleList,
        TriangleStrip,
        LineList,
        LineStrip,
        PointList
    };


    inline uint32_t GetIndexSize(IndexFormat format)
    {
        switch (format)
        {
        case IndexFormat::UInt16: return sizeof(uint16_t);
        case IndexFormat::UInt32: return sizeof(uint32_t);
        default: return 0;
        }
    }

    inline uint32_t GetVertexFormatSize(VertexFormat format)
    {
        switch (format)
        {
        case VertexFormat::Float32: return 4;
        case VertexFormat::Float32x2: return 8;
        case VertexFormat::Float32x3: return 12;
        case VertexFormat::Float32x4: return 16;
        case VertexFormat::Float16: return 2;
        case VertexFormat::Float16x2: return 4;
        case VertexFormat::Float16x4: return 8;
        case VertexFormat::Int32: return 4;
        case VertexFormat::Int32x2: return 8;
        case VertexFormat::Int32x3: return 12;
        case VertexFormat::Int32x4: return 16;
        case VertexFormat::UInt32: return 4;
        case VertexFormat::UInt32x2: return 8;
        case VertexFormat::UInt32x3: return 12;
        case VertexFormat::UInt32x4: return 16;
        case VertexFormat::UInt8x4:
        case VertexFormat::UInt8x4Normalized:
        case VertexFormat::Int8x4:
        case VertexFormat::Int8x4Normalized: return 4;
        default: return 0;
        }
    }

    struct VertexAttribute
    {
        VertexSemantic Semantic = VertexSemantic::None;
        VertexFormat Format = VertexFormat::Unknown;
        uint8_t Index = 0;
        uint32_t Offset = 0;

        VertexAttribute() = default;

        VertexAttribute(
            VertexSemantic semantic,
            VertexFormat format,
            uint8_t index = 0,
            uint32_t offset = 0)
            : Semantic(semantic)
            , Format(format)
            , Index(index)
            , Offset(offset)
        {
        }
    };

    struct VertexStream
    {
        std::vector<std::byte> Data;
        std::vector<VertexAttribute> Attributes;
        uint32_t Stride = 0;
        uint32_t VertexCount = 0;

        bool IsValid() const
        {
            if (Stride == 0)
                return false;

            if (VertexCount == 0)
                return true;

            return Data.size() >= static_cast<size_t>(Stride) * VertexCount;
        }

        const VertexAttribute* FindAttribute(VertexSemantic semantic, uint8_t index = 0) const
        {
            for (const auto& attribute : Attributes)
            {
                if (attribute.Semantic == semantic &&
                    attribute.Index == index)
                {
                    return &attribute;
                }
            }

            return nullptr;
        }

        bool HasAttribute(VertexSemantic semantic, uint8_t index = 0) const
        {
            return FindAttribute(semantic, index) != nullptr;
        }
    
        void CalculateLayout()
        {
            Stride = 0;

            for (auto& attribute : Attributes)
            {
                attribute.Offset = Stride;
                Stride += GetVertexFormatSize(attribute.Format);
            }
        }
    };

    struct SubMesh
    {
        PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        int32_t VertexOffset = 0;

        bool IsValid(uint32_t totalIndexCount) const
        {
            return FirstIndex + IndexCount <= totalIndexCount;
        }
    };

    struct MeshData
    {
        std::vector<VertexStream> VertexStreams;
        std::vector<std::byte> IndexBuffer;
        IndexFormat IndexFormat = IndexFormat::UInt32;
        std::vector<SubMesh> SubMeshes;

        uint32_t GetVertexCount() const
        {
            if (VertexStreams.empty())
                return 0;

            return VertexStreams[0].VertexCount;
        }

        uint32_t GetIndexCount() const
        {
            const uint32_t indexSize = GetIndexSize(IndexFormat);

            if (indexSize == 0)
                return 0;

            return static_cast<uint32_t>(IndexBuffer.size() / indexSize);
        }

        const VertexAttribute* FindAttribute(VertexSemantic semantic, uint8_t index = 0) const
        {
            for (const auto& stream : VertexStreams)
            {
                if (const auto* attribute =
                    stream.FindAttribute(semantic, index))
                {
                    return attribute;
                }
            }

            return nullptr;
        }

        const VertexStream* FindVertexStream(VertexSemantic semantic, uint8_t index = 0) const
        {
            for (const auto& stream : VertexStreams)
            {
                if (stream.HasAttribute(semantic, index))
                    return &stream;
            }

            return nullptr;
        }

        bool HasAttribute(VertexSemantic semantic, uint8_t index = 0) const
        {
            return FindAttribute(semantic, index) != nullptr;
        }

        bool IsValid() const
        {
            if (VertexStreams.empty())
                return false;

            const uint32_t vertexCount = VertexStreams[0].VertexCount;

            for (const auto& stream : VertexStreams)
            {
                if (!stream.IsValid())
                    return false;

                if (stream.VertexCount != vertexCount)
                    return false;
            }

            const uint32_t indexCount = GetIndexCount();

            for (const auto& subMesh : SubMeshes)
            {
                if (!subMesh.IsValid(indexCount))
                    return false;
            }

            return true;
        }
    };
}
