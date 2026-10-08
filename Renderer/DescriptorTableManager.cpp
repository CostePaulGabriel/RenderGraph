#include "Renderer/DescriptorTableManager.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace LynxEngine
{
    // ---- DescriptorHandle -------------------------------------------------------------------

    DescriptorHandle::DescriptorHandle(const std::shared_ptr<DescriptorTableManager>& manager, DescriptorIndex index)
        : m_Manager(manager), m_Index(index)
    {
    }

    DescriptorHandle::~DescriptorHandle()
    {
        Reset();
    }

    DescriptorHandle::DescriptorHandle(DescriptorHandle&& other) noexcept
        : m_Manager(std::move(other.m_Manager)), m_Index(std::exchange(other.m_Index, kInvalidDescriptor))
    {
    }

    DescriptorHandle& DescriptorHandle::operator=(DescriptorHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Manager = std::move(other.m_Manager);
            m_Index = std::exchange(other.m_Index, kInvalidDescriptor);
        }
        return *this;
    }

    void DescriptorHandle::Reset()
    {
        if (m_Index != kInvalidDescriptor)
        {
            if (const auto manager = m_Manager.lock())
                manager->ReleaseDescriptor(m_Index);
        }
        m_Index = kInvalidDescriptor;
        m_Manager.reset();
    }

    // ---- DescriptorTableManager -------------------------------------------------------------

    DescriptorTableManager::DescriptorTableManager(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout, uint32_t initialCapacity)
        : m_Device(device)
    {
        m_DescriptorTable = m_Device->createDescriptorTable(layout);
        m_Device->resizeDescriptorTable(m_DescriptorTable, std::max(1u, initialCapacity), false);

        const uint32_t capacity = m_DescriptorTable->getCapacity();
        m_Descriptors.resize(capacity, nvrhi::BindingSetItem::None());
        m_RefCounts.resize(capacity, 0);
    }

    DescriptorTableManager::~DescriptorTableManager()
    {
        if (m_AllocatedCount != 0)
            std::fprintf(stderr, "[Bindless] %u descriptors still allocated at destruction\n", m_AllocatedCount);
    }

    DescriptorIndex DescriptorTableManager::CreateDescriptor(nvrhi::BindingSetItem item)
    {
        item.slot = 0; // the slot is the table index, decided here
        item.arrayElement = 0;

        if (const auto found = m_DescriptorIndexMap.find(item); found != m_DescriptorIndexMap.end())
        {
            ++m_RefCounts[found->second];
            return found->second;
        }

        DescriptorIndex index = kInvalidDescriptor;
        for (DescriptorIndex i = m_SearchStart; i < GetCapacity(); ++i)
        {
            if (m_RefCounts[i] == 0)
            {
                index = i;
                break;
            }
        }
        if (index == kInvalidDescriptor)
        {
            index = GetCapacity();
            Grow();
        }

        m_Descriptors[index] = item;
        m_RefCounts[index] = 1;
        m_DescriptorIndexMap[item] = index;
        m_SearchStart = index + 1;
        ++m_AllocatedCount;

        nvrhi::BindingSetItem write = item;
        write.slot = index;
        m_Device->writeDescriptorTable(m_DescriptorTable, write);
        return index;
    }

    DescriptorHandle DescriptorTableManager::CreateDescriptorHandle(nvrhi::BindingSetItem item)
    {
        const DescriptorIndex index = CreateDescriptor(item);
        return DescriptorHandle(shared_from_this(), index);
    }

    void DescriptorTableManager::ReleaseDescriptor(DescriptorIndex index)
    {
        if (index >= GetCapacity() || m_RefCounts[index] == 0)
            return;
        if (--m_RefCounts[index] != 0)
            return;

        m_DescriptorIndexMap.erase(m_Descriptors[index]);
        m_Descriptors[index] = nvrhi::BindingSetItem::None();
        m_SearchStart = std::min(m_SearchStart, index);
        --m_AllocatedCount;

        // Leave a null descriptor behind so a stale index never reads a destroyed resource.
        m_Device->writeDescriptorTable(m_DescriptorTable, nvrhi::BindingSetItem::None(index));
    }

    void DescriptorTableManager::Grow()
    {
        const uint32_t capacity = std::max(64u, GetCapacity() * 2);
        m_Device->resizeDescriptorTable(m_DescriptorTable, capacity, true);
        m_Descriptors.resize(capacity, nvrhi::BindingSetItem::None());
        m_RefCounts.resize(capacity, 0);
    }
}
