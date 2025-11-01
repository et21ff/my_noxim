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

void TaskManager::ConfigureFromYAML(const std::string& yaml_file_path, const std::string& role) {
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
        Configure(config, role);

    } catch (const std::exception& e) {
        std::cerr << "TaskManager: Error loading YAML configuration: " << e.what() << std::endl;
        throw;
    }
}

void TaskManager::Configure(const WorkloadConfig& config ,const std::string& role) {
    std::cout << "TaskManager: Starting configuration for role '" << role << "'" << std::endl;

    // 存储配置
    config_ = config;

    // 打印配置摘要
    std::cout << "TaskManager: Configuration summary:" << std::endl;
    std::cout << "  - Working sets: " << config.working_set.size() << std::endl;
    std::cout << "  - Data flow specs: " << config.data_flow_specs.size() << std::endl;

    // 打印所有已配置的角色
    auto roles = config.get_all_roles();
    std::cout << "  - Configured roles: ";
    for (size_t i = 0; i < roles.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << roles[i];
    }
    std::cout << std::endl;

    // 清空现有任务
    all_tasks_.clear();

    // 查找指定角色的数据流规格（使用 TaskManager 的私有辅助函数）
    const DataFlowSpec* spec = find_data_flow_spec(role);
    if (!spec) {
        std::cout << "TaskManager: Warning - No data flow spec found for role '" << role << "'" << std::endl;
        return;
    }

    // 查找角色的工作集（使用 TaskManager 的私有辅助函数）
    role_working_set_ = find_working_set_for_role(role);
    if (!role_working_set_) {
        std::cout << "TaskManager: Warning - No working set found for role '" << role << "'" << std::endl;
    }
    
    // 打印角色的详细信息
    std::cout << "TaskManager: Role '" << role << "' details:" << std::endl;
    std::cout << "  - Has schedule template: " << (spec->has_schedule() ? "Yes" : "No") << std::endl;
    std::cout << "  - Has command definitions: " << (spec->has_commands() ? "Yes" : "No") << std::endl;
    std::cout << "  - Compute latency: " << spec->properties.compute_latency << std::endl;
    if (role_working_set_) {
        std::cout << "  - Working set found for role '" << role << "'" << std::endl;
    }
    

    // 如果有命令定义，打印它们
    if (spec->has_commands()) {
        std::cout << "  - Command definitions (" << spec->command_definitions.size() << "):" << std::endl;
        for (const auto& cmd : spec->command_definitions) {
            std::cout << "    * ID " << cmd.command_id << ": " << cmd.name;
            if (!cmd.evict_payload.is_empty()) {
                std::cout << " (evict: W=" << cmd.evict_payload.weights
                         << ", I=" << cmd.evict_payload.inputs
                         << ", O=" << cmd.evict_payload.outputs << ")";
            }
            std::cout << std::endl;
        }
    }

    // 存储角色的属性和命令定义（使用 TaskManager 的公共接口）
    role_properties_ = get_properties_for_role(role);
    role_commands_ = get_commands_for_role(role);

    // 只有当角色有调度模板时才继续处理
    if (!spec->has_schedule()) {
        std::cout << "TaskManager: Role '" << role << "' has no schedule template, skipping task generation" << std::endl;
        return;
    }

    const ScheduleTemplate& schedule = *spec->schedule_template;
    std::cout << "TaskManager: Found " << role << " schedule with " << schedule.total_timesteps
              << " timesteps and " << schedule.delta_events.size() << " delta events" << std::endl;

    // 调整任务向量大小以匹配总时间步数
    all_tasks_.resize(schedule.total_timesteps);

    // 遍历所有时间步，创建对应的DispatchTask
    for (int t = 0; t < schedule.total_timesteps; ++t) {
        DispatchTask task;

        // 查找匹配当前时间步的事件
        int matched_events = 0;
        DeltaEvent* fallback_event = nullptr;
        for (const auto& event : schedule.delta_events) {
            if(event.trigger.type == "fallback") {
                fallback_event = const_cast<DeltaEvent*>(&event);
                continue;
            }

            if (matches_trigger_condition(event.trigger, t)) {
                std::cout << "TaskManager: Timestep " << t << " matches event '"
                          << event.name << "'" << std::endl;

                // 根据事件创建分发任务
                create_dispatch_task_from_event(task, event, t);
                matched_events++;
            }
        }

        if(matched_events == 0 && fallback_event != nullptr) {
            std::cout << "TaskManager: Timestep " << t << " using fallback event '"
                      << fallback_event->name << "'" << std::endl;
            create_dispatch_task_from_event(task, *fallback_event, t);
        }

        // 将创建的任务存储到时间线中
        all_tasks_[t] = task;

        // 调试输出
        if (!task.is_complete()) {
            std::cout << "TaskManager: Timestep " << t << " configured: "
                      << task.to_string() << std::endl;
        }
    }

    if(!all_tasks_.empty())
    {
        auto example_task = all_tasks_[0];
        size_t output_size = 0;
        std::unordered_set<int> all_output_targets;
        for(const auto& sub_task : example_task.sub_tasks)
        {
            if(sub_task.type == DataType::OUTPUT)
            {
                output_size += sub_task.size;
                for(auto target : sub_task.target_ids)
                {
                    all_output_targets.insert(target);
                }
            } 
    }
        size_t avg_output = (output_size != 0 ? output_size/all_output_targets.size() : 0 );

        if(role_working_set_==nullptr)
        {
            std::cout << "TaskManager: Warning - Role '" << role << "' has no working set defined." << std::endl;
            return;
        }
        
        size_t output_ws_size = 0;
        for(auto workspace : role_working_set_->data)
        {
            if(stringToDataType(workspace.data_space) == DataType::OUTPUT)
            {
                output_ws_size = workspace.size;

            }
        }

        role_output_working_set_size_ = output_ws_size;

        size_t cumulative_outputs_sent = 0;
        for(int t=0; t<schedule.total_timesteps; ++t)
        {
            cumulative_outputs_sent += avg_output;
            if (cumulative_outputs_sent>= output_ws_size)
            {
                sync_points_[t] = true;
                cumulative_outputs_sent = 0;
            } 

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

DataDelta TaskManager::get_command_definition(int command_id) const {
    // 假设 task_manager_ 已经初始化并包含所有命令定义
    for (const auto& cmd_def : *role_commands_) {
        if (cmd_def.command_id == command_id) {
            return cmd_def.evict_payload;
        }
    } 
    std::cerr << "Error: role_commands_ is a nullptr!" << std::endl;


}
    
//========================================================================
// 私有辅助函数实现（新格式）
//========================================================================

const DataFlowSpec* TaskManager::find_data_flow_spec(const std::string& role) const {
    return config_.find_spec_for_role(role);
}

const RoleWorkingSet* TaskManager::find_working_set_for_role(const std::string& role) const {
    return config_.find_working_set_for_role(role);
}

DataDelta TaskManager::get_current_working_set() const {
    DataDelta current_set;

    if (!role_working_set_) {
        return current_set; // 返回空的 DataDelta
    }

    for(auto workspace : role_working_set_->data)
    {
        DataType type = stringToDataType(workspace.data_space);
        switch(type) {
            case DataType::INPUT:
                current_set.inputs += workspace.size;
                break;
            case DataType::WEIGHT:
                current_set.weights += workspace.size;
                break;
            case DataType::OUTPUT:
                current_set.outputs += workspace.size;
                break;
            default:
                // 忽略未知类型
                break;
        }
    }

    return current_set;

        
    }
std::unordered_set<int> TaskManager::resolve_target_group(const std::string& target_group) const {
    std::unordered_set<int> targets;

    if (target_group == "ALL_COMPUTE_PES") {
        // 假设计算PE的ID从某个范围开始
        // 这里使用简单的假设：ID 1-15 是计算PE
        for (int i = 1; i <= 15; ++i) {
            targets.insert(i);
        }
    } else {
        // 尝试解析为具体的ID列表
        std::stringstream ss(target_group);
        std::string item;
        while (std::getline(ss, item, ',')) {
            try {
                int id = std::stoi(item);
                targets.insert(id);
            } catch (const std::exception&) {
                // 忽略无效的ID
            }
        }
    }

    return targets;
}

/**
 * @brief 根据单个 DeltaEvent，为 DispatchTask 填充子任务
 * @param task (输出参数) 将被填充的 DispatchTask 对象
 * @param event 包含规则的 DeltaEvent
 * @param timestep 当前的时间步 (用于调试日志)
 */
void TaskManager::create_dispatch_task_from_event(DispatchTask& task, const DeltaEvent& event, int timestep) const {
    
    dbg(sc_time_stamp(), "TaskManager", "[CONFIG] Processing event '" + event.name + 
        "' for timestep " + std::to_string(timestep));

    // --- [核心修改] 遍历 event 中的所有 actions ---
    // 每个 action 都将成为 DispatchTask 中的一个独立 sub_task
    for (const auto& action : event.actions) {
        
        // 1. 解析数据类型 (data_space -> DataType)
        DataType type = stringToDataType(action.data_space);
        if (type == DataType::UNKNOWN) {
            // 如果 data_space 无效，则跳过这个 action
            dbg(sc_time_stamp(), "TaskManager", "[WARNING] Invalid data_space '" + action.data_space + "' in event '" + event.name + "'. Skipping.");
            continue;
        }

        // 2. 解析目标组 (target_group -> vector<int>)
        // (假设 resolve_target_group 已经实现，它会查询 logical_architecture 配置)
        std::unordered_set<int> target_ids = resolve_target_group(action.target_group);
        if (target_ids.empty()) {
            // 如果目标组为空或未找到，则跳过这个 action
            dbg(sc_time_stamp(), "TaskManager", "[WARNING] No valid targets found for group '" + action.target_group + "'. Skipping.");
            continue;
        }

        if(action.multicast==true)
        {
            DataDispatchInfo sub_task_info;
            sub_task_info.type = type;
            sub_task_info.size = action.size;
            sub_task_info.target_ids = target_ids;
            
            // 4. 将这个子任务添加到 DispatchTask 的 vector 中
            task.sub_tasks.push_back(sub_task_info);
            continue;
        }
        else {
            // 对于非多播，每个目标创建一个独立的子任务
            for (int target_id : target_ids) {
                DataDispatchInfo sub_task_info;
                sub_task_info.type = type;
                sub_task_info.size = action.size;
                sub_task_info.target_ids.insert(target_id); // 仅包含单个目标ID

                // 4. 将这个子任务添加到 DispatchTask 的 vector 中
                task.sub_tasks.push_back(sub_task_info);

            }
        }
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

    if (trigger.type == "fallback") {
        return false;
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