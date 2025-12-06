#include "BufferManager.h"
#include <stdexcept>
#include <algorithm>

// 共享模式构造函数
BufferManager::BufferManager(size_t capacity)
    : mode_(BufferMode::SHARED),
      capacity_(capacity),
      current_size_(0)
{
    // 初始化所有数据类型的大小为0
    internal_buffer_sizes_[DataType::INPUT] = 0;
    internal_buffer_sizes_[DataType::WEIGHT] = 0;
    internal_buffer_sizes_[DataType::OUTPUT] = 0;
}

// 独立模式构造函数
BufferManager::BufferManager(const std::map<DataType, size_t> &type_capacities)
    : mode_(BufferMode::INDEPENDENT),
      capacity_(0), // 独立模式下不使用总容量
      current_size_(0),
      type_capacities_(type_capacities)
{
    // 初始化所有数据类型的大小为0
    internal_buffer_sizes_[DataType::INPUT] = 0;
    internal_buffer_sizes_[DataType::WEIGHT] = 0;
    internal_buffer_sizes_[DataType::OUTPUT] = 0;
}

// --- 查询接口实现 ---

size_t BufferManager::GetCapacity(DataType type) const
{
    if (mode_ == BufferMode::SHARED || type == DataType::UNKNOWN)
    {
        return capacity_;
    }
    else
    {
        auto it = type_capacities_.find(type);
        return (it != type_capacities_.end()) ? it->second : 0;
    }
}

size_t BufferManager::GetCurrentSize(DataType type) const
{
    if (type == DataType::UNKNOWN)
    {
        return current_size_;
    }
    else
    {
        auto it = internal_buffer_sizes_.find(type);
        return (it != internal_buffer_sizes_.end()) ? it->second : 0;
    }
}

bool BufferManager::IsFull(DataType type) const
{
    if (mode_ == BufferMode::SHARED || type == DataType::UNKNOWN)
    {
        return current_size_ >= capacity_;
    }
    else
    {
        auto capacity_it = type_capacities_.find(type);
        auto size_it = internal_buffer_sizes_.find(type);
        if (capacity_it != type_capacities_.end() && size_it != internal_buffer_sizes_.end())
        {
            return size_it->second >= capacity_it->second;
        }
        return false;
    }
}

size_t BufferManager::GetDataSize(DataType type) const
{
    try
    {
        return internal_buffer_sizes_.at(type);
    }
    catch (const std::out_of_range &oor)
    {
        return 0;
    }
}

// --- 核心功能实现 ---

bool BufferManager::OnDataReceived(DataType type, size_t size_added)
{
    if (mode_ == BufferMode::SHARED)
    {
        // 共享模式：检查总容量
        if (current_size_ + size_added > capacity_)
        {
            return false;
        }
    }
    else
    {
        // 独立模式：检查该类型的独立容量
        size_t limit = 0;
        auto it = type_capacities_.find(type);
        if (it != type_capacities_.end())
        {
            limit = it->second;
        }

        if (internal_buffer_sizes_[type] + size_added > limit)
        {
            return false;
        }
    }

    // 更新状态
    internal_buffer_sizes_[type] += size_added;
    current_size_ += size_added;
    return true;
}

size_t BufferManager::evict_data(DataType type, size_t size_to_evict)
{
    if (size_to_evict == 0)
    {
        return 0;
    }

    const size_t current_data_size = internal_buffer_sizes_.at(type);
    const size_t actual_evicted_size = std::min(size_to_evict, current_data_size);

    internal_buffer_sizes_[type] -= actual_evicted_size;
    current_size_ -= actual_evicted_size;

    return actual_evicted_size;
}

bool BufferManager::AreDataTypesReady(const std::vector<DataType> &required_types, size_t size) const
{
    for (const DataType &type : required_types)
    {
        if (GetDataSize(type) < size)
        {
            return false;
        }
    }
    return true;
}

bool BufferManager::AreDataTypeReady(const DataType &required_types, size_t size) const
{
    return GetDataSize(required_types) >= size;
}

bool BufferManager::RemoveData(DataType type, size_t size)
{
    if (current_size_ < size || internal_buffer_sizes_[type] < size)
    {
        return false;
    }

    current_size_ -= size;
    internal_buffer_sizes_[type] -= size;

    return true;
}