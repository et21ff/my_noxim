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
        if (node["weights"]) {
            rhs.weights = node["weights"].as<size_t>();
        } else {
            rhs.weights = 0; // 默认值
        }

        // 解析 inputs 字段
        if (node["inputs"]) {
            rhs.inputs = node["inputs"].as<size_t>();
        } else {
            rhs.inputs = 0; // 默认值
        }

        // 解析 outputs 字段
        if (node["outputs"]) {
            rhs.outputs = node["outputs"].as<size_t>();
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

        // 必需字段检查
        if (!node["role"] || !node["schedule_template"]) {
            return false;
        }

        rhs.role = node["role"].as<std::string>();
        rhs.schedule_template = node["schedule_template"].as<ScheduleTemplate>();

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

        if (spec.schedule_template.total_timesteps <= 0) {
            std::cerr << "Validation Error: Invalid total_timesteps in role '" << spec.role << "'" << std::endl;
            return false;
        }

        if (spec.schedule_template.delta_events.empty()) {
            std::cerr << "Validation Error: No delta events found in role '" << spec.role << "'" << std::endl;
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
        std::cout << "    Schedule Template:" << std::endl;
        std::cout << "      Total Timesteps: " << spec.schedule_template.total_timesteps << std::endl;
        std::cout << "      Delta Events:" << std::endl;

        for (const auto& event : spec.schedule_template.delta_events) {
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

    std::cout << "===============================" << std::endl;
}

#endif // __CONFIGPARSER_H__