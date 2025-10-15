#ifndef __WORKLOAD_STRUCTS_H__
#define __WORKLOAD_STRUCTS_H__

#include <vector>
#include <map>
#include <string>
#include <iostream>

//========================================================================
// 工作负载配置相关数据结构（基于新 YAML 格式）
//========================================================================

/**
 * @brief 数据增量结构体
 * 定义要发送的数据块
 */
struct DataDelta {
    size_t weights;                               // Weights 数据大小
    size_t inputs;                                // Inputs 数据大小
    size_t outputs;                               // Outputs 数据大小

    DataDelta() : weights(0), inputs(0), outputs(0) {}
    DataDelta(size_t w, size_t i, size_t o) : weights(w), inputs(i), outputs(o) {}
};

/**
 * @brief 触发器结构体
 * 定义事件触发条件
 */
struct Trigger {
    std::string type;                             // 触发类型："on_timestep", "on_timestep_modulo", "default"
    std::vector<int> params;                      // 触发参数
    
    bool is_default() const { return type == "default"; }
    
    bool matches(int timestep) const {
        if (type == "on_timestep") {
            return !params.empty() && timestep == params[0];
        } else if (type == "on_timestep_modulo") {
            return params.size() >= 2 && (timestep % params[0]) == params[1];
        } else if (type == "default") {
            return true; // 默认总是匹配
        }
        return false;
    }
};

/**
 * @brief Delta 事件结构体
 * 定义基于条件的数据分发事件
 */
struct DeltaEvent {
    Trigger trigger;                              // 触发条件
    std::string name;                             // 事件名称（如 "FILL", "DELTA"）
    DataDelta delta;                              // 要发送的数据块
    std::string target_group;                     // 目标组（如 "ALL_COMPUTE_PES"）
};

/**
 * @brief 调度模板结构体
 * 定义周期性的调度行为
 */
struct ScheduleTemplate {
    int total_timesteps;                          // 总时间步数
    std::vector<DeltaEvent> delta_events;        // Delta 事件列表
};

/**
 * @brief 数据流规格结构体
 * 定义特定角色的数据流行为
 */
struct DataFlowSpec {
    std::string role;                             // 角色名称
    ScheduleTemplate schedule_template;          // 调度模板
};

/**
 * @brief 工作空间数据结构体
 * 定义角色可用的数据空间
 */
struct WorkspaceData {
    std::string data_space;                       // 数据空间名称
    size_t size;                                  // 数据大小
    std::string reuse_strategy;                   // 复用策略
};

/**
 * @brief 角色工作集结构体
 * 定义角色的数据工作集
 */
struct RoleWorkingSet {
    std::string role;                             // 角色名称
    std::vector<WorkspaceData> data;             // 数据列表
};

/**
 * @brief 完整的工作负载配置结构体
 * 对应 YAML 文件的完整结构
 */
struct WorkloadConfig {
    std::vector<RoleWorkingSet> working_set;     // 角色工作集
    std::vector<DataFlowSpec> data_flow_specs;   // 数据流规格
};

#endif // __WORKLOAD_STRUCTS_H__