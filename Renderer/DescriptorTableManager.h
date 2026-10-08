#pragma once

// Bindless descriptor table allocator, modelled on donut::engine::DescriptorTableManager.
//
// It owns one nvrhi::IDescriptorTable created from a bindless layout and hands out slots in it.
// A shader indexes the table with the slot (e.g. Texture2D gBindlessTextures[] in space2), so a
// material only stores uint indices instead of binding its textures.
//
//   - CreateDescriptor writes a resource view into a free slot and returns its index. The same
//     view requested twice gets the same slot; slots are reference counted.
//   - The table grows (doubling) when it is full. Growing reallocates the descriptors, so do it
//     while the GPU is idle (the Renderer only allocates in SetScene).
//   - DescriptorHandle releases its slot when destroyed (donut::engine::DescriptorHandle). It
//     holds a weak reference, so handles may outlive the manager safely.

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace LynxEngine
{
    using DescriptorIndex = uint32_t;
    inline constexpr DescriptorIndex kInvalidDescriptor = ~0u;

    class DescriptorTableManager;

    class DescriptorHandle
    {
    public:
        DescriptorHandle() = default;
        DescriptorHandle(const std::shared_ptr<DescriptorTableManager>& manager, DescriptorIndex index);
        ~DescriptorHandle();

        DescriptorHandle(const DescriptorHandle&) = delete;
        DescriptorHandle& operator=(const DescriptorHandle&) = delete;
        DescriptorHandle(DescriptorHandle&& other) noexcept;
        DescriptorHandle& operator=(DescriptorHandle&& other) noexcept;

        bool IsValid() const { return m_Index != kInvalidDescriptor && !m_Manager.expired(); }
        DescriptorIndex Get() const { return IsValid() ? m_Index : kInvalidDescriptor; }
        void Reset();

    private:
        std::weak_ptr<DescriptorTableManager> m_Manager;
        DescriptorIndex m_Index = kInvalidDescriptor;
    };

    class DescriptorTableManager : public std::enable_shared_from_this<DescriptorTableManager>
    {
    public:
        DescriptorTableManager(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout, uint32_t initialCapacity = 64);
        ~DescriptorTableManager();

        DescriptorTableManager(const DescriptorTableManager&) = delete;
        DescriptorTableManager& operator=(const DescriptorTableManager&) = delete;

        // 'item' is any binding item (e.g. BindingSetItem::Texture_SRV(0, texture)); its slot is ignored.
        DescriptorIndex CreateDescriptor(nvrhi::BindingSetItem item);
        DescriptorHandle CreateDescriptorHandle(nvrhi::BindingSetItem item);
        void ReleaseDescriptor(DescriptorIndex index);

        nvrhi::IDescriptorTable* GetDescriptorTable() const { return m_DescriptorTable; }
        uint32_t GetCapacity() const { return uint32_t(m_Descriptors.size()); }
        uint32_t GetAllocatedCount() const { return m_AllocatedCount; }

    private:
        void Grow();

        nvrhi::DeviceHandle m_Device;
        nvrhi::DescriptorTableHandle m_DescriptorTable;

        std::vector<nvrhi::BindingSetItem> m_Descriptors; // index == slot, slot field normalized to 0
        std::vector<uint32_t> m_RefCounts;                // 0 == free
        std::unordered_map<nvrhi::BindingSetItem, DescriptorIndex> m_DescriptorIndexMap;
        DescriptorIndex m_SearchStart = 0;
        uint32_t m_AllocatedCount = 0;
    };
}
