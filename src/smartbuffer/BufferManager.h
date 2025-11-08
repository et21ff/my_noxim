#ifndef LIVENESS_AWARE_BUFFER_H
#define LIVENESS_AWARE_BUFFER_H

#include <vector>
#include <map>
#include <cstddef> // for size_t
#include <algorithm> // for std::min
#include "../DataStructs.h"

/**
 * @file BufferManager.h
 * @brief 定义了一个由数据生命周期驱动的智能Buffer模块。
 *
 * 该版本采纳了更精细的“驱逐计划”，能够为每个数据类型在特定时间步
 * 指定需要驱逐的具体大小。
 */

// --- 核心类型定义 ---

// enum class DataType {
//     INPUT,
//     WEIGHT,
//     OUTPUT,
// };

/**
 * @using EvictionSchedule
 * @brief 定义了“驱逐计划”的数据结构，支持部分和可变大小的驱逐。
 *
 * - Key (int): 计算完成的时间步 (timestep)。
 * - Value (std::map<DataType, size_t>): 一个映射，指定在该时间步完成后，
 *   每种数据类型需要驱逐的具体空间大小。
 *   如果一个DataType不在map中，表示该时间步不驱逐此类型数据。
 */
using EvictionSchedule = std::map<int, std::map<DataType, size_t>>;


// --- 智能Buffer模块 ---

class BufferManager {
public:
    BufferManager(size_t capacity, const EvictionSchedule& schedule);

    bool OnDataReceived(DataType type, size_t size_added);


    /**
     * @brief [新] 检查一组必需的数据类型当前是否都存在于缓冲区中。
     *        (即它们的大小是否都大于0)
     * @param required_types 一个包含所有必需数据类型的列表。
     * @return 如果所有必需类型的数据都存在，返回true；否则返回false。
     */
    bool AreDataTypesReady(const std::vector<DataType>& required_types , size_t size) const;
    bool AreDataTypeReady(const DataType& required_types,size_t size) const;
    bool RemoveData(DataType type, size_t size);



    /**
     * @brief 在一个计算时间步完成后调用，根据驱逐计划执行精确的、大小可变的“垃圾回收”。
     *
     * 它会查找当前时间步的驱逐条目，并对每个指定的数据类型，
     * 减少其在缓冲区中占用的空间。
     *
     * @param timestep 刚刚完成的计算时间步。
     */
    size_t OnComputeFinished(int timestep);

    // --- 辅助与测试接口 (保持不变) ---
    size_t GetCapacity() const;
    size_t GetCurrentSize() const;
    size_t GetDataSize(DataType type) const;
    bool IsFull() const;

private:
    /**
     * @brief 从缓冲区中驱逐指定类型和大小的数据。
     * @param type 要被驱逐的数据类型。
     * @param size_to_evict 要驱逐的大小。
     */
    size_t evict_data(DataType type, size_t size_to_evict);

    size_t capacity_;
    size_t current_size_;
    const EvictionSchedule eviction_schedule_;
    std::map<DataType, size_t> internal_buffer_sizes_;
};

#endif // LIVENESS_AWARE_BUFFER_