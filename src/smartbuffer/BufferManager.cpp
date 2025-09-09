# include "BufferManager.h"
# include <stdexcept>
#include <algorithm>
BufferManager::BufferManager(size_t capacity, const EvictionSchedule& schedule)
    // 使用成员初始化列表来初始化成员变量
    // 这对于 const 成员 (eviction_schedule_) 和引用成员是必需的，
    // 对其他成员也是一种高效且推荐的做法。
    // 注意：初始化顺序必须与头文件中成员变量的声明顺序一致
    : capacity_(capacity),
      current_size_(0),
      eviction_schedule_(schedule) 
{
    // 在构造函数体中，我们确保所有数据类型的初始大小都被设置为0。
    // 这可以防止在第一次查询一个从未存过数据的类型时出现问题。
    internal_buffer_sizes_[DataType::INPUT] = 0;
    internal_buffer_sizes_[DataType::WEIGHT] = 0;
    internal_buffer_sizes_[DataType::OUTPUT] = 0;
}

// --- 查询接口 (Getters) 实现 ---

size_t BufferManager::GetCapacity() const {
    return capacity_;
}

size_t BufferManager::GetCurrentSize() const {
    return current_size_;
}

bool BufferManager::IsFull() const {
    // 如果当前大小大于或等于容量，则认为缓冲区已满。
    return current_size_ >= capacity_;
}

size_t BufferManager::GetDataSize(DataType type) const {
    // 使用 .at() 而不是 [] 来访问 map 元素。
    // .at() 会进行边界检查，如果 key 不存在会抛出异常。
    // 因为我们在构造函数中已经确保了所有 key 都存在，所以这里是安全的。
    // 在 const 方法中，只能使用 .at() 或 .find()。`
    try {
        return internal_buffer_sizes_.at(type);
    } catch (const std::out_of_range& oor) {
        // 这是一个健壮性措施，理论上不应该发生，
        // 因为构造函数已经初始化了所有类型。
        // 但如果未来 DataType 增加了而构造函数未更新，这里能提供有用的错误信息。
        // 在高性能代码中，可以考虑移除 try-catch 并依赖 .find()。
        // 对于我们的目的，保持健壮性更好。
        return 0; 
    }
}

#include <iostream> // 临时用于调试输出，可以后续移除

// --- 核心功能实现 ---

bool BufferManager::OnDataReceived(DataType type, size_t size_added) {
    // 步骤 1: 检查是否有足够的剩余空间来容纳新数据。
    if (current_size_ + size_added > capacity_) {
        // 空间不足，操作失败。
        // 可以选择在此处添加日志记录，例如：
        // std::cerr << "Error: Buffer capacity exceeded. Cannot add " << size_added 
        //           << " bytes. Available: " << (capacity_ - current_size_) << std::endl;
        return false;
    }

    // 步骤 2: 空间充足，更新缓冲区状态。
    // 更新指定数据类型的空间占用。
    internal_buffer_sizes_[type] += size_added;

    // 更新缓冲区的总已用空间。
    current_size_ += size_added;

    // 操作成功。
    return true;
}

/**
 * @brief 从缓冲区中安全地驱逐指定类型和大小的数据。
 */
size_t BufferManager::evict_data(DataType type, size_t size_to_evict) {
    // 如果要驱逐的大小为0，则无需执行任何操作。
    if (size_to_evict == 0) {
        return 0;
    }

    // 获取该数据类型当前在缓冲区中的实际大小。
    const size_t current_data_size = internal_buffer_sizes_.at(type);

    // 计算实际可以驱逐的大小。不能驱逐比当前拥有量更多的数据。
    // 这是为了防止因驱逐计划错误而导致缓冲区大小变为负数的关键保护措施。
    const size_t actual_evicted_size = std::min(size_to_evict, current_data_size);

    // 更新状态
    internal_buffer_sizes_[type] -= actual_evicted_size;
    current_size_ -= actual_evicted_size;

    return actual_evicted_size; // 返回实际驱逐的大小
}

// --- 核心功能实现 (续) ---

/**
 * @brief 根据驱逐计划执行精确的、大小可变的“垃圾回收”。
 */
size_t BufferManager::OnComputeFinished(int timestep) {
    // 步骤 1: 在驱逐计划中查找当前时间步。
    auto it = eviction_schedule_.find(timestep);

    // 步骤 2: 如果当前时间步没有对应的驱逐条目，则直接返回。
    if (it == eviction_schedule_.end()) {
        return 0;
    }

    // 步骤 3: 获取该时间步的驱逐详情 (即 map<DataType, size_t>)。
    const auto& evictions_for_this_step = it->second;

    size_t total_evicted_size = 0;
    // 计算本次驱逐的总大小。

    // 步骤 4: 遍历该时间步的所有驱逐指令。
    for (const auto& eviction_pair : evictions_for_this_step) {
        DataType type_to_evict = eviction_pair.first;
        size_t size_to_evict   = eviction_pair.second;

        // 调用私有辅助函数执行具体的驱逐操作。
        total_evicted_size+=evict_data(type_to_evict, size_to_evict);
    }

    return total_evicted_size; // 返回本次驱逐的总大小
}


bool BufferManager::AreDataTypesReady(const std::vector<DataType>& required_types) const {
    // 遍历所有必需的数据类型
    for (const DataType& type : required_types) {
        // 只要发现有一种所需的数据不存在 (大小为0)，就立刻返回 false
        if (GetDataSize(type) == 0) {
            return false;
        }
    }
    // 如果循环正常结束，说明所有需要的数据类型都已就位
    return true;
}