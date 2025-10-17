#ifndef __WORKLOAD_STRUCTS_H__
#define __WORKLOAD_STRUCTS_H__

#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <memory>
#include <algorithm>

//========================================================================
// 基础数据类型枚举
//========================================================================

// enum class DataType {
//     WEIGHT = 0,
//     INPUT = 1,
//     OUTPUT = 2,
//     UNKNOWN = 99
// };

// // 辅助函数：将 DataType 转换为字符串
// inline const char* DataType_to_str(DataType type) {
//     switch (type) {
//         case DataType::WEIGHT: return "WEIGHT";
//         case DataType::INPUT: return "INPUT";
//         case DataType::OUTPUT: return "OUTPUT";
//         default: return "UNKNOWN";
//     }
// }

//========================================================================
// 底层数据结构
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

    bool is_empty() const {
        return weights == 0 && inputs == 0 && outputs == 0;
    }

    size_t total_size() const {
        return weights + inputs + outputs;
    }
};

/**
 * @brief 触发器结构体
 * 定义事件触发条件
 */
struct Trigger {
    std::string type;                             // 触发类型："on_timestep", "on_timestep_modulo", "default"
    std::vector<int> params;                      // 触发参数
    bool is_default() const { return type == "default"; }
    bool matches(int timestep) const;
};

/**
 * @brief 命令定义结构体
 * 定义角色可以执行的命令和驱逐策略
 */
struct CommandDefinition {
    int command_id;                               // 命令ID
    std::string name;                             // 命令名称
    DataDelta evict_payload;                      // 驱逐的数据负载

    CommandDefinition() : command_id(-1) {}
    CommandDefinition(int id, const std::string& cmd_name, const DataDelta& payload)
        : command_id(id), name(cmd_name), evict_payload(payload) {}

    bool is_valid() const {
        return command_id >= 0 && !name.empty();
    }
};

/**
 * @brief 角色属性结构体
 * 定义角色的静态属性
 */
struct RoleProperties {
    int compute_latency;                          // 计算延迟（仅对 COMPUTE 角色有效）

    RoleProperties() : compute_latency(0) {}
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

    DeltaEvent() = default;

    bool is_valid() const {
        return !name.empty() && !target_group.empty() && !trigger.type.empty();
    }
};

/**
 * @brief 调度模板结构体
 * 定义周期性的调度行为
 */
struct ScheduleTemplate {
    int total_timesteps;                          // 总时间步数
    std::vector<DeltaEvent> delta_events;        // Delta 事件列表

    ScheduleTemplate() : total_timesteps(0) {}

    bool is_valid() const {
        return total_timesteps > 0 && !delta_events.empty();
    }
};

/**
 * @brief 数据流规格结构体
 * 定义特定角色的数据流行为
 */
struct DataFlowSpec {
    std::string role;                             // 角色名称
    std::unique_ptr<ScheduleTemplate> schedule_template;  // 调度模板（可选）
    RoleProperties properties;                    // 角色属性
    std::vector<CommandDefinition> command_definitions; // 命令定义（可选）

    DataFlowSpec() = default;

    bool has_schedule() const {
        return schedule_template != nullptr && schedule_template->is_valid();
    }

    bool has_commands() const {
        return !command_definitions.empty();
    }

    bool is_valid() const {
        return !role.empty() && (has_schedule() || has_commands());
    }
};

/**
 * @brief 工作空间数据结构体
 * 定义角色可用的数据空间
 */
struct WorkspaceData {
    std::string data_space;                       // 数据空间名称
    size_t size;                                  // 数据大小
    std::string reuse_strategy;                   // 复用策略

    WorkspaceData() : size(0) {}

    bool is_valid() const {
        return !data_space.empty() && size > 0 && !reuse_strategy.empty();
    }
};

/**
 * @brief 角色工作集结构体
 * 定义角色的数据工作集
 */
struct RoleWorkingSet {
    std::string role;                             // 角色名称
    std::vector<WorkspaceData> data;             // 数据列表

    bool is_valid() const {
        if (role.empty()) return false;

        for (const auto& workspace : data) {
            if (!workspace.is_valid()) return false;
        }

        return !data.empty();
    }

    size_t get_total_data_size() const {
        size_t total = 0;
        for (const auto& workspace : data) {
            total += workspace.size;
        }
        return total;
    }
};

/**
 * @brief 完整的工作负载配置结构体
 * 对应 YAML 文件的完整结构
 */
struct WorkloadConfig {
    std::vector<RoleWorkingSet> working_set;     // 角色工作集
    std::vector<DataFlowSpec> data_flow_specs;   // 数据流规格

    bool is_valid() const {
        // 验证工作集
        for (const auto& ws : working_set) {
            if (!ws.is_valid()) return false;
        }

        // 验证数据流规格
        for (const auto& spec : data_flow_specs) {
            if (!spec.is_valid()) return false;
        }

        return !working_set.empty() && !data_flow_specs.empty();
    }

    // 查找特定角色的数据流规格
    const DataFlowSpec* find_spec_for_role(const std::string& role) const {
        for (const auto& spec : data_flow_specs) {
            if (spec.role == role) {
                return &spec;
            }
        }
        return nullptr;
    }

    // 查找特定角色的工作集
    const RoleWorkingSet* find_working_set_for_role(const std::string& role) const {
        for (const auto& ws : working_set) {
            if (ws.role == role) {
                return &ws;
            }
        }
        return nullptr;
    }

    // 获取所有角色列表
    std::vector<std::string> get_all_roles() const {
        std::vector<std::string> roles;

        for (const auto& ws : working_set) {
            roles.push_back(ws.role);
        }

        return roles;
    }
};

//========================================================================
// 辅助函数实现
//========================================================================

inline bool Trigger::matches(int timestep) const {
    if (is_default()) {
        return true;
    }

    if (type == "on_timestep_modulo" && params.size() >= 2) {
        int modulo = params[0];
        int value = params[1];
        return (timestep % modulo) == value;
    }

    if (type == "on_timestep" && !params.empty()) {
        return timestep == params[0];
    }

    return false;
}

#endif // __WORKLOAD_STRUCTS_H__