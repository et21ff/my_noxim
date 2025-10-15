#include "TaskManager.h"
#include "ConfigParser.h"
#include "GlobalParams.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <iterator>

//========================================================================
// TaskManager 类实现（支持新 YAML 格式）
//========================================================================

void TaskManager::ConfigureFromYAML(const std::string& yaml_file_path) {
    std::cout << "TaskManager: Loading configuration from YAML file: " << yaml_file_path << std::endl;

    try {
        // 使用 ConfigParser 加载配置
        WorkloadConfig config = loadWorkloadConfigFromFile(yaml_file_path);

        // 验证配置
        if (!validateWorkloadConfig(config)) {
            throw std::runtime_error("Loaded configuration failed validation");
        }

        std::cout << "TaskManager: YAML configuration loaded and validated successfully" << std::endl;

        // 调用现有的配置方法
        Configure(config);

    } catch (const std::exception& e) {
        std::cerr << "TaskManager: Error loading YAML configuration: " << e.what() << std::endl;
        throw;
    }
}

void TaskManager::Configure(const WorkloadConfig& config) {
    std::cout << "TaskManager: Starting configuration with new YAML format" << std::endl;

    // 存储配置
    config_ = config;

    // 清空现有任务
    all_tasks_.clear();

    // 查找 GLB 角色的数据流规格
    const DataFlowSpec* glb_spec = find_data_flow_spec("ROLE_GLB");
    if (!glb_spec) {
        std::cout << "TaskManager: Warning - No ROLE_GLB data flow spec found" << std::endl;
        return;
    }

    const ScheduleTemplate& schedule = glb_spec->schedule_template;
    std::cout << "TaskManager: Found GLB schedule with " << schedule.total_timesteps
              << " timesteps and " << schedule.delta_events.size() << " delta events" << std::endl;

    // 调整任务向量大小以匹配总时间步数
    all_tasks_.resize(schedule.total_timesteps);

    // 遍历所有时间步，创建对应的DispatchTask
    for (int t = 0; t < schedule.total_timesteps; ++t) {
        DispatchTask task;

        // 查找匹配当前时间步的事件
        for (const auto& event : schedule.delta_events) {
            if (matches_trigger_condition(event.trigger, t)) {
                std::cout << "TaskManager: Timestep " << t << " matches event '"
                          << event.name << "'" << std::endl;

                // 根据事件创建分发任务
                create_dispatch_task_from_event(task, event, t);
                break; // 假设每个时间步只匹配一个事件
            }
        }

        // 将创建的任务存储到时间线中
        all_tasks_[t] = task;

        // 调试输出
        if (!task.is_complete()) {
            std::cout << "TaskManager: Timestep " << t << " configured: "
                      << task.to_string() << std::endl;
        }
    }

    std::cout << "TaskManager: Configuration completed with " << all_tasks_.size()
              << " tasks total" << std::endl;
}

DispatchTask TaskManager::get_task_for_timestep(int timestep) const {
    // 边界检查
    if (timestep < 0 || static_cast<size_t>(timestep) >= all_tasks_.size()) {
        std::cout << "TaskManager: Warning - Invalid timestep " << timestep
                  << " requested, returning empty task" << std::endl;
        return DispatchTask(); // 返回空任务
    }

    // 返回任务副本
    return all_tasks_[timestep];
}

//========================================================================
// 私有辅助函数实现（新格式）
//========================================================================

const DataFlowSpec* TaskManager::find_data_flow_spec(const std::string& role) const {
    for (const auto& spec : config_.data_flow_specs) {
        if (spec.role == role) {
            return &spec;
        }
    }
    return nullptr;
}

std::vector<int> TaskManager::resolve_target_group(const std::string& target_group) const {
    std::vector<int> targets;

    if (target_group == "ALL_COMPUTE_PES") {
        // 假设计算PE的ID从某个范围开始
        // 这里使用简单的假设：ID 1-15 是计算PE
        for (int i = 1; i <= 15; ++i) {
            targets.push_back(i);
        }
    } else {
        // 尝试解析为具体的ID列表
        std::stringstream ss(target_group);
        std::string item;
        while (std::getline(ss, item, ',')) {
            try {
                int id = std::stoi(item);
                targets.push_back(id);
            } catch (const std::exception&) {
                // 忽略无效的ID
            }
        }
    }

    return targets;
}

void TaskManager::create_dispatch_task_from_event(DispatchTask& task, const DeltaEvent& event, int timestep) const {
    std::cout << "TaskManager: Creating dispatch task for event '" << event.name
              << "' at timestep " << timestep << std::endl;

    // 解析目标组
    std::vector<int> target_ids = resolve_target_group(event.target_group);
    if (target_ids.empty()) {
        std::cout << "TaskManager: Warning - No valid targets for group '" << event.target_group << "'" << std::endl;
        return;
    }

    // 创建 Weights 数据分发任务
    if (event.delta.weights > 0) {
        DataDispatchInfo weights_info;
        weights_info.size = event.delta.weights;
        weights_info.target_ids = target_ids; // 复制目标列表
        task.sub_tasks[DataType::WEIGHT] = weights_info;

        std::cout << "TaskManager: Added Weights dispatch: " << weights_info.size
                  << " bytes to " << weights_info.target_ids.size() << " targets" << std::endl;
    }

    // 创建 Inputs 数据分发任务
    if (event.delta.inputs > 0) {
        DataDispatchInfo inputs_info;
        inputs_info.size = event.delta.inputs;
        inputs_info.target_ids = target_ids; // 复制目标列表
        task.sub_tasks[DataType::INPUT] = inputs_info;

        std::cout << "TaskManager: Added Inputs dispatch: " << inputs_info.size
                  << " bytes to " << inputs_info.target_ids.size() << " targets" << std::endl;
    }

    // 创建 Outputs 数据分发任务
    if (event.delta.outputs > 0) {
        DataDispatchInfo outputs_info;
        outputs_info.size = event.delta.outputs;
        outputs_info.target_ids = target_ids; // 复制目标列表
        task.sub_tasks[DataType::OUTPUT] = outputs_info;

        std::cout << "TaskManager: Added Outputs dispatch: " << outputs_info.size
                  << " bytes to " << outputs_info.target_ids.size() << " targets" << std::endl;
    }
}

bool TaskManager::matches_trigger_condition(const Trigger& trigger, int timestep) const {
    if (trigger.is_default()) {
        return true; // 默认触发器总是匹配
    }

    if (trigger.type == "on_timestep_modulo" && trigger.params.size() >= 2) {
        int modulo = trigger.params[0];
        int value = trigger.params[1];
        return (timestep % modulo) == value;
    }

    if (trigger.type == "on_timestep" && !trigger.params.empty()) {
        return timestep == trigger.params[0];
    }

    return false;
}

// //========================================================================
// // Trigger 类方法实现
// //========================================================================

// bool Trigger::matches(int timestep) const {
//     if (is_default()) {
//         return true;
//     }

//     if (type == "on_timestep_modulo" && params.size() >= 2) {
//         int modulo = params[0];
//         int value = params[1];
//         return (timestep % modulo) == value;
//     }

//     if (type == "on_timestep" && !params.empty()) {
//         return timestep == params[0];
//     }

//     return false;
// }