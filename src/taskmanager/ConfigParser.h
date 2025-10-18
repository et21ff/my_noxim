#ifndef __CONFIGPARSER_H__
#define __CONFIGPARSER_H__

#include <yaml-cpp/yaml.h>
#include <iostream>
#include <stdexcept>

// 前向声明数据结构（在 DataStructs.h 中定义）
#include "WorkloadStructs.h"
#include "DataStructs.h"


namespace YAML {

//========================================================================
// 底层数据结构的 YAML 转换器
//========================================================================

// DataDelta 转换器
template<>
struct convert<DataDelta> {
    static bool decode(const Node& node, DataDelta& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 解析 weights 字段
        if (node["Weights"]) {
            rhs.weights = node["Weights"].as<size_t>();
        } else {
            rhs.weights = 0; // 默认值
        }

        // 解析 inputs 字段
        if (node["Inputs"]) {
            rhs.inputs = node["Inputs"].as<size_t>();
        } else {
            rhs.inputs = 0; // 默认值
        }

        // 解析 outputs 字段
        if (node["Outputs"]) {
            rhs.outputs = node["Outputs"].as<size_t>();
        } else {
            rhs.outputs = 0; // 默认值
        }

        return true;
    }
};

// Trigger 转换器
template<>
struct convert<Trigger> {
    static bool decode(const Node& node, Trigger& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 解析不同类型的触发器
        if (node["on_timestep_modulo"]) {
            rhs.type = "on_timestep_modulo";
            rhs.params = node["on_timestep_modulo"].as<std::vector<int>>();
        } else if (node["on_timestep"]) {
            std::string timestep_value = node["on_timestep"].as<std::string>();
            if (timestep_value == "default") {
                rhs.type = "default";
                rhs.params.clear();
            } else {
                rhs.type = "on_timestep";
                rhs.params.push_back(std::stoi(timestep_value));
            }
        } else {
            return false; // 未知的触发器类型
        }

        return true;
    }
};

// RoleProperties 转换器
template<>
struct convert<RoleProperties> {
    static bool decode(const Node& node, RoleProperties& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 可选字段：compute_latency
        if (node["compute_latency"]) {
            rhs.compute_latency = node["compute_latency"].as<int>();
        } else {
            rhs.compute_latency = 0; // 默认值
        }

        return true;
    }
};

// CommandDefinition 转换器
template<>
struct convert<CommandDefinition> {
    static bool decode(const Node& node, CommandDefinition& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 必需字段检查
        if (!node["command_id"] || !node["name"]) {
            return false;
        }

        rhs.command_id = node["command_id"].as<int>();
        rhs.name = node["name"].as<std::string>();

        // 可选字段：evict_payload
        if (node["evict_payload"]) {
            rhs.evict_payload = node["evict_payload"].as<DataDelta>();
        } else {
            rhs.evict_payload = DataDelta(); // 默认为空
        }

        return true;
    }
};

// WorkspaceData 转换器
template<>
struct convert<WorkspaceData> {
    static bool decode(const Node& node, WorkspaceData& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 必需字段检查
        if (!node["data_space"] || !node["size"] || !node["reuse_strategy"]) {
            return false;
        }

        rhs.data_space = node["data_space"].as<std::string>();
        rhs.size = node["size"].as<size_t>();
        rhs.reuse_strategy = node["reuse_strategy"].as<std::string>();

        return true;
    }
};

//========================================================================
// 中层复合结构的 YAML 转换器
//========================================================================

// DeltaEvent 转换器
template<>
struct convert<DeltaEvent> {
    static bool decode(const Node& node, DeltaEvent& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 必需字段检查
        if (!node["name"] || !node["trigger"] || !node["delta"] || !node["target_group"]) {
            return false;
        }

        // 解析各个字段
        rhs.name = node["name"].as<std::string>();
        rhs.trigger = node["trigger"].as<Trigger>();
        rhs.delta = node["delta"].as<DataDelta>();
        rhs.target_group = node["target_group"].as<std::string>();

        return true;
    }
};

// ScheduleTemplate 转换器
template<>
struct convert<ScheduleTemplate> {
    static bool decode(const Node& node, ScheduleTemplate& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 必需字段检查
        if (!node["total_timesteps"] || !node["delta_events"]) {
            return false;
        }

        rhs.total_timesteps = node["total_timesteps"].as<int>();
        rhs.delta_events = node["delta_events"].as<std::vector<DeltaEvent>>();

        return true;
    }
};

// RoleWorkingSet 转换器
template<>
struct convert<RoleWorkingSet> {
    static bool decode(const Node& node, RoleWorkingSet& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 必需字段检查
        if (!node["role"] || !node["data"]) {
            return false;
        }

        rhs.role = node["role"].as<std::string>();
        rhs.data = node["data"].as<std::vector<WorkspaceData>>();

        return true;
    }
};

// DataFlowSpec 转换器
template<>
struct convert<DataFlowSpec> {
    static bool decode(const Node& node, DataFlowSpec& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 必需字段检查：role 是必需的
        if (!node["role"]) {
            return false;
        }

        rhs.role = node["role"].as<std::string>();

        // 可选字段：properties
        if (node["properties"]) {
            rhs.properties = node["properties"].as<RoleProperties>();
        } else {
            rhs.properties = RoleProperties(); // 默认构造
        }

        // 可选字段：schedule_template
        if (node["schedule_template"]) {
            rhs.schedule_template = std::unique_ptr<ScheduleTemplate>(new ScheduleTemplate());
            *rhs.schedule_template = node["schedule_template"].as<ScheduleTemplate>();
        } else {
            rhs.schedule_template = nullptr; // 没有 schedule template
        }

        // 可选字段：command_definitions
        if (node["command_definitions"]) {
            rhs.command_definitions = node["command_definitions"].as<std::vector<CommandDefinition>>();
        } else {
            rhs.command_definitions.clear(); // 没有 command definitions
        }

        return true;
    }
};

//========================================================================
// 顶层结构的 YAML 转换器
//========================================================================

// WorkloadConfig 转换器
template<>
struct convert<WorkloadConfig> {
    static bool decode(const Node& node, WorkloadConfig& rhs) {
        if (!node.IsMap()) {
            return false;
        }

        // 解析 working_set（可选字段）
        if (node["working_set"]) {
            rhs.working_set = node["working_set"].as<std::vector<RoleWorkingSet>>();
        }

        // 解析 data_flow_specs（必需字段）
        if (node["data_flow_specs"]) {
            rhs.data_flow_specs = node["data_flow_specs"].as<std::vector<DataFlowSpec>>();
        } else {
            return false; // data_flow_specs 是必需的
        }

        return true;
    }
};

} // namespace YAML

//========================================================================
// 辅助加载函数
//========================================================================

/**
 * @brief 从 YAML 文件加载工作负载配置
 * @param filepath YAML 文件路径
 * @return 解析后的 WorkloadConfig 对象
 * @throws std::runtime_error 当文件不存在或解析失败时
 */
inline WorkloadConfig loadWorkloadConfigFromFile(const std::string& filepath) {
    try {
        YAML::Node root = YAML::LoadFile(filepath);

        if (!root["workload"]) {
            throw std::runtime_error("YAML file does not contain 'workload' root node");
        }

        return root["workload"].as<WorkloadConfig>();

    } catch (const YAML::Exception& e) {
        throw std::runtime_error("YAML parsing error in file '" + filepath + "': " + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error("Error loading file '" + filepath + "': " + e.what());
    }
}

/**
 * @brief 从 YAML 字符串加载工作负载配置
 * @param yaml_content YAML 内容字符串
 * @return 解析后的 WorkloadConfig 对象
 * @throws std::runtime_error 当解析失败时
 */
inline WorkloadConfig loadWorkloadConfigFromString(const std::string& yaml_content) {
    try {
        YAML::Node root = YAML::Load(yaml_content);

        if (!root["workload"]) {
            throw std::runtime_error("YAML content does not contain 'workload' root node");
        }

        return root["workload"].as<WorkloadConfig>();

    } catch (const YAML::Exception& e) {
        throw std::runtime_error(std::string("YAML parsing error: ") + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Error parsing YAML content: ") + e.what());
    }
}

/**
 * @brief 验证 WorkloadConfig 配置的完整性
 * @param config 要验证的配置
 * @return 验证是否通过
 */
inline bool validateWorkloadConfig(const WorkloadConfig& config) {
    // 基本验证：至少要有一个数据流规格
    if (config.data_flow_specs.empty()) {
        std::cerr << "Validation Error: No data flow specifications found" << std::endl;
        return false;
    }

    // 验证每个数据流规格
    for (const auto& spec : config.data_flow_specs) {
        if (spec.role.empty()) {
            std::cerr << "Validation Error: Data flow spec has empty role" << std::endl;
            return false;
        }

        // 验证 schedule_template（如果存在）
        if (spec.has_schedule()) {
            if (spec.schedule_template->total_timesteps <= 0) {
                std::cerr << "Validation Error: Invalid total_timesteps in role '" << spec.role << "'" << std::endl;
                return false;
            }

            if (spec.schedule_template->delta_events.empty()) {
                std::cerr << "Validation Error: No delta events found in role '" << spec.role << "'" << std::endl;
                return false;
            }
        }

        // 验证 command_definitions（如果存在）
        if (spec.has_commands()) {
            for (const auto& cmd : spec.command_definitions) {
                if (!cmd.is_valid()) {
                    std::cerr << "Validation Error: Invalid command definition in role '" << spec.role << "'" << std::endl;
                    return false;
                }
            }
        }

        // 至少要有 schedule_template 或 command_definitions 中的一个
        if (!spec.has_schedule() && !spec.has_commands()) {
            std::cerr << "Validation Error: Role '" << spec.role << "' has neither schedule_template nor command_definitions" << std::endl;
            return false;
        }
    }

    return true;
}

/**
 * @brief 打印 WorkloadConfig 的详细信息（用于调试）
 * @param config 要打印的配置
 */
inline void printWorkloadConfig(const WorkloadConfig& config) {
    std::cout << "=== Workload Configuration ===" << std::endl;

    // 打印工作集
    if (!config.working_set.empty()) {
        std::cout << "\nWorking Set:" << std::endl;
        for (const auto& ws : config.working_set) {
            std::cout << "  Role: " << ws.role << std::endl;
            for (const auto& data : ws.data) {
                std::cout << "    - " << data.data_space << ": " << data.size
                         << " bytes (strategy: " << data.reuse_strategy << ")" << std::endl;
            }
        }
    }

    // 打印数据流规格
    std::cout << "\nData Flow Specifications:" << std::endl;
    for (const auto& spec : config.data_flow_specs) {
        std::cout << "  Role: " << spec.role << std::endl;

        // 打印属性
        if (spec.properties.compute_latency > 0) {
            std::cout << "    Properties:" << std::endl;
            std::cout << "      Compute Latency: " << spec.properties.compute_latency << std::endl;
        }

        // 打印调度模板（如果存在）
        if (spec.has_schedule()) {
            std::cout << "    Schedule Template:" << std::endl;
            std::cout << "      Total Timesteps: " << spec.schedule_template->total_timesteps << std::endl;
            std::cout << "      Delta Events:" << std::endl;

            for (const auto& event : spec.schedule_template->delta_events) {
                std::cout << "        - Event: " << event.name << std::endl;
                std::cout << "          Target Group: " << event.target_group << std::endl;
                std::cout << "          Trigger Type: " << event.trigger.type << std::endl;
                if (!event.trigger.params.empty()) {
                    std::cout << "          Trigger Params: ";
                    for (size_t i = 0; i < event.trigger.params.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << event.trigger.params[i];
                    }
                    std::cout << std::endl;
                }
                std::cout << "          Data Delta: Weights=" << event.delta.weights
                         << ", Inputs=" << event.delta.inputs
                         << ", Outputs=" << event.delta.outputs << std::endl;
            }
        }

        // 打印命令定义（如果存在）
        if (spec.has_commands()) {
            std::cout << "    Command Definitions:" << std::endl;
            for (const auto& cmd : spec.command_definitions) {
                std::cout << "        - Command " << cmd.command_id << ": " << cmd.name << std::endl;
                std::cout << "          Evict Payload: Weights=" << cmd.evict_payload.weights
                         << ", Inputs=" << cmd.evict_payload.inputs
                         << ", Outputs=" << cmd.evict_payload.outputs << std::endl;
            }
        }
    }

    std::cout << "===============================" << std::endl;
}

#endif // __CONFIGPARSER_H__