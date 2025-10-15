#ifndef __TASKMANAGER_H__
#define __TASKMANAGER_H__

#include <vector>
#include <map>
#include <string>
#include <iostream>
#include "DataStructs.h"
#include "WorkloadStructs.h"
#include "ConfigParser.h"  // 用于从 YAML 加载配置
//========================================================================
// 第一部分：核心数据结构定义
//========================================================================

/**
 * @brief 数据分发信息结构体
 * 包含特定数据类型的分发目标信息
 */
struct DataDispatchInfo {
    size_t size;                          // 数据大小（字节）
    std::vector<int> target_ids;          // 目标节点ID列表

    DataDispatchInfo() : size(0) {}
    DataDispatchInfo(size_t s, const std::vector<int>& targets)
        : size(s), target_ids(targets) {}

    /**
     * @brief 检查是否还有未完成的目标
     * @return 如果所有目标都已完成则返回true
     */
    bool is_complete() const {
        return target_ids.empty();
    }
};

/**
 * @brief 分发任务结构体
 * 包含一个时间步的完整分发任务信息
 */
struct DispatchTask {
    std::map<DataType, DataDispatchInfo> sub_tasks;  // 子任务映射表

    /**
     * @brief 检查任务是否完成
     * @return 如果所有子任务都已完成则返回true
     */
    bool is_complete() const {
        return sub_tasks.empty();
    }

    /**
     * @brief 记录特定类型和目标的完成状态
     * @param type 数据类型
     * @param target_id 目标节点ID
     */
    void record_completion(DataType type, int target_id) {
        auto it = sub_tasks.find(type);
        if (it != sub_tasks.end()) {
            // 从目标列表中移除已完成的目标
            auto& targets = it->second.target_ids;
            auto target_it = std::find(targets.begin(), targets.end(), target_id);
            if (target_it != targets.end()) {
                targets.erase(target_it);

                // 如果该数据类型的所有目标都已完成，则移除整个子任务
                if (targets.empty()) {
                    sub_tasks.erase(it);
                }
            }
        }
    }

    /**
     * @brief 获取任务描述字符串（用于调试）
     * @return 任务描述字符串
     */
    std::string to_string() const {
        std::string result = "DispatchTask[";
        bool first = true;
        for (const auto& pair : sub_tasks) {
            if (!first) result += ", ";
            result += std::string(DataType_to_str(pair.first)) + "(" + std::to_string(pair.second.size) + "B, " +
                     std::to_string(pair.second.target_ids.size()) + " targets)";
            first = false;
        }
        result += "]";
        return result;
    }
};

//========================================================================
// 第二部分：TaskManager 类定义
//========================================================================

/**
 * @brief 任务管理器类
 * 负责从配置中加载任务模板，并在运行时为每个时间步提供分发任务
 */
class TaskManager {
private:
    std::vector<DispatchTask> all_tasks_;  // 存储整个任务时间线
    WorkloadConfig config_;                 // 存储工作负载配置

    // 私有辅助函数
    const DataFlowSpec* find_data_flow_spec(const std::string& role) const;
    std::vector<int> resolve_target_group(const std::string& target_group) const;
    void create_dispatch_task_from_event(DispatchTask& task, const DeltaEvent& event, int timestep) const;
    bool matches_trigger_condition(const Trigger& trigger, int timestep) const;

public:
    /**
     * @brief 默认构造函数
     */
    TaskManager() = default;

    /**
     * @brief 析构函数
     */
    ~TaskManager() = default;

    /**
     * @brief 禁用拷贝构造和赋值（确保唯一性）
     */
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;

    /**
     * @brief 从工作负载配置中设置任务
     * @param config 工作负载配置
     */
    void Configure(const WorkloadConfig& config);

    /**
     * @brief 从 YAML 文件配置任务
     * @param yaml_file_path YAML 文件路径
     */
    void ConfigureFromYAML(const std::string& yaml_file_path);

    /**
     * @brief 获取指定时间步的任务
     * @param timestep 时间步
     * @return 该时间步的DispatchTask副本，如果无效则返回空任务
     */
    DispatchTask get_task_for_timestep(int timestep) const;

    /**
     * @brief 获取总任务时间步数
     * @return 总时间步数
     */
    size_t get_total_timesteps() const {
        return all_tasks_.size();
    }

    /**
     * @brief 清空所有任务
     */
    void clear() {
        all_tasks_.clear();
    }

    /**
     * @brief 检查任务管理器是否已配置
     * @return 如果已配置则返回true
     */
    bool is_configured() const {
        return !all_tasks_.empty();
    }

    /**
     * @brief 打印任务时间线（用于调试）
     */
    void print_timeline() const {
        std::cout << "=== TaskManager Timeline ===" << std::endl;
        for (size_t i = 0; i < all_tasks_.size(); ++i) {
            std::cout << "Timestep " << i << ": " << all_tasks_[i].to_string() << std::endl;
        }
        std::cout << "=============================" << std::endl;
    }
};

#endif // __TASKMANAGER_H__