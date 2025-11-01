#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "ConfigParser.h" // 包含 loadWorkloadConfigFromString 和所有 struct
#include "TaskManager.h"  // 包含我们要测试的 TaskManager

// in tests/test_TaskManager.cpp

// in tests/test_TaskManager.cpp

TEST_CASE("TaskManager handles 'on_timestep_modulo' trigger correctly", "[TaskManager]") {
    TaskManager task_manager;
    WorkloadConfig config;

    // --- 1. 准备测试配置 (Setup) ---
    // [核心修正] 使用我们最终确定的、基于列表的 "delta" 格式
    const std::string yaml_str = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 32
        delta_events:
          - trigger: { on_timestep_modulo: [16, 0] }
            name: "FILL"
            # "delta" 是一个列表，包含一个原子动作
            delta:
              - { data_space: "Weights", size: 100, target_group: "1,2,3" }
              - { data_space: "Weights", size: 200, target_group: "4,5" }
            multicast: true

          - trigger: { on_timestep: "fallback" }
            name: "DELTA"
            # "delta" 是一个列表，包含一个原子动作
            delta:
              - { data_space: "Inputs", size: 10, target_group: "2,3" }
            multicast: true
            
)";

const std::string unicast_yaml_str = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      # [修正] schedule_template 应该和 role 对齐，
      # 它们共同属于 data_flow_specs 列表的第一个元素。
      schedule_template:
        total_timesteps: 32
        delta_events:
          - trigger: { on_timestep_modulo: [16, 0] }
            name: "FILL"
            delta:
              - { data_space: "Weights", size: 100, target_group: "1,2,3" }
              - { data_space: "Weights", size: 200, target_group: "4,5"}
            
          - trigger: { on_timestep: "default" }
            name: "DEFAULT_EVENT"
            delta:
              - { data_space: "Outputs", size: 50, target_group: "1,2" , multicast: false}
              - { data_space: "Weights", size: 50, target_group: "3,4" }
)";
    
    // --- 2. 解析与配置 (Act) ---
    // 我们的 ConfigParser 现在应该能完美解析这个格式
    REQUIRE_NOTHROW(config = loadWorkloadConfigFromString(yaml_str));

    // 假设 TaskManager 的 Configure 现在只需要 workload 和 role
    // 或者 TaskManager 内部自己查找 role
    task_manager.Configure(config, "ROLE_GLB"); 

    // --- 3. 断言 (Asserts) ---
    REQUIRE(task_manager.get_total_timesteps() == 32);

    // [辅助 lambda，保持不变]
    auto find_sub_task = [](const DispatchTask& task, DataType type) -> const DataDispatchInfo* {
        for (const auto& sub_task : task.sub_tasks) {
            if (sub_task.type == type) {
                return &sub_task;
            }
        }
        return nullptr;
    };

    auto find_multiple_sub_tasks = [](const DispatchTask& task, DataType type) -> std::vector<const DataDispatchInfo*> {
        std::vector<const DataDispatchInfo*> results;
        for (const auto& sub_task : task.sub_tasks) {
            if (sub_task.type == type) {
                results.push_back(&sub_task);
            }
        }
        return results;
    };

    // 验证 timestep 0 (FILL)
    SECTION("Timestep 0 should be a FILL task") {
        DispatchTask task0 = task_manager.get_task_for_timestep(0);
        
        const DataDispatchInfo* weights_task = find_sub_task(task0, DataType::WEIGHT);
        const DataDispatchInfo* inputs_task = find_sub_task(task0, DataType::INPUT);
        
        REQUIRE(weights_task != nullptr);
        REQUIRE(weights_task->size == 100);
        REQUIRE(weights_task->target_ids.size() == 3); // 假设 GROUP_A 被正确解析
        REQUIRE(inputs_task == nullptr);

        DataDispatchInfo weights_task_200 = task0.sub_tasks[1];
        REQUIRE(weights_task_200.size == 200);
        REQUIRE(weights_task_200.target_ids.size() == 2); // 假设
    }

    SECTION("Timestep 1 should also have DEFAULT_EVENT task"){
        WorkloadConfig unicast_config;
        REQUIRE_NOTHROW(unicast_config = loadWorkloadConfigFromString(unicast_yaml_str));
        TaskManager unicast_task_manager;
        unicast_task_manager.Configure(unicast_config, "ROLE_GLB");

        DispatchTask task0 = unicast_task_manager.get_task_for_timestep(1);

        auto outputs_task = find_multiple_sub_tasks(task0, DataType::OUTPUT);
        REQUIRE(outputs_task.size() == 2);
        for(const auto* out_task : outputs_task)
        {
            REQUIRE(out_task->size == 50);
            REQUIRE(out_task->target_ids.size() == 1);
        }

        auto weights_task = find_sub_task(task0, DataType::WEIGHT);
        REQUIRE(weights_task != nullptr);
        REQUIRE(weights_task->size == 50);
        REQUIRE(weights_task->target_ids.size() == 2);
        
    }

    // 验证 timestep 1 (DELTA)
    SECTION("Timestep 1 should be a DELTA task") {
        DispatchTask task1 = task_manager.get_task_for_timestep(1);

        const DataDispatchInfo* weights_task = find_sub_task(task1, DataType::WEIGHT);
        const DataDispatchInfo* inputs_task = find_sub_task(task1, DataType::INPUT);

        REQUIRE(weights_task == nullptr);
        REQUIRE(inputs_task != nullptr);
        REQUIRE(inputs_task->size == 10);
        REQUIRE(inputs_task->target_ids.size() == 2); // 假设 GROUP_B 被正确解析
    }

    // 验证 timestep 16 (FILL)
    SECTION("Timestep 16 should be a FILL task again") {
        DispatchTask task16 = task_manager.get_task_for_timestep(16);
        
        const DataDispatchInfo* weights_task = find_sub_task(task16, DataType::WEIGHT);
        REQUIRE(weights_task != nullptr);
        REQUIRE(weights_task->size == 100);
    }
}


TEST_CASE("DispatchTask record_completion logic", "[DispatchTask]") {

    SECTION("Basic completion of a single target") {
        // Setup: 创建一个 DispatchTask，为其 sub_tasks 添加一个 INPUT 任务，size=10，target_ids={1, 2, 3}
        DispatchTask task;

        DataDispatchInfo input_task;
        input_task.type = DataType::INPUT;
        input_task.size = 10;
        input_task.target_ids = {1, 2, 3};

        task.sub_tasks.push_back(input_task);

        // 初始状态验证
        REQUIRE(task.sub_tasks.size() == 1);
        REQUIRE(task.sub_tasks[0].target_ids.size() == 3);
        REQUIRE(task.is_complete() == false);

        // Act: 调用 task.record_completion(DataType::INPUT, 2, 10)
        task.record_completion(DataType::INPUT, 2, 10);

        // Asserts
        REQUIRE(task.sub_tasks.size() == 1); // 子任务不应被删除
        REQUIRE(task.sub_tasks[0].target_ids.size() == 2); // 目标数量减少了1

        // 检查 target_ids 中不再包含 2
        auto& targets = task.sub_tasks[0].target_ids;
        REQUIRE(std::find(targets.begin(), targets.end(), 2) == targets.end());
        REQUIRE(std::find(targets.begin(), targets.end(), 1) != targets.end());
        REQUIRE(std::find(targets.begin(), targets.end(), 3) != targets.end());
        REQUIRE(task.is_complete() == false);
    }

    SECTION("Completion of the last target should remove the sub-task") {
        // Setup: 创建一个 DispatchTask，为其 sub_tasks 添加一个 WEIGHT 任务，size=100，target_ids={5} (只有一个目标)
        DispatchTask task;

        DataDispatchInfo weight_task;
        weight_task.type = DataType::WEIGHT;
        weight_task.size = 100;
        weight_task.target_ids = {5};

        task.sub_tasks.push_back(weight_task);

        // 初始状态验证
        REQUIRE(task.sub_tasks.size() == 1);
        REQUIRE(task.is_complete() == false);

        // Act: 调用 task.record_completion(DataType::WEIGHT, 5, 100)
        task.record_completion(DataType::WEIGHT, 5, 100);

        // Asserts
        REQUIRE(task.sub_tasks.empty() == true); // 子任务应该被完全删除
        REQUIRE(task.is_complete() == true);
    }

    SECTION("Handling multiple distinct sub-tasks") {
        // Setup: 创建一个 DispatchTask，为其 sub_tasks 添加三个不同的子任务
        DispatchTask task;

        // INPUT, size=10, targets={1, 2}
        DataDispatchInfo input_task1;
        input_task1.type = DataType::INPUT;
        input_task1.size = 10;
        input_task1.target_ids = {1, 2};
        task.sub_tasks.push_back(input_task1);

        // WEIGHT, size=50, targets={1}
        DataDispatchInfo weight_task;
        weight_task.type = DataType::WEIGHT;
        weight_task.size = 50;
        weight_task.target_ids = {1};
        task.sub_tasks.push_back(weight_task);

        // INPUT, size=20, targets={2} (与第一个 INPUT 任务 size 不同)
        DataDispatchInfo input_task2;
        input_task2.type = DataType::INPUT;
        input_task2.size = 20;
        input_task2.target_ids = {2};
        task.sub_tasks.push_back(input_task2);

        // 初始状态验证
        REQUIRE(task.sub_tasks.size() == 3);
        REQUIRE(task.is_complete() == false);

        // Act: 调用 task.record_completion(DataType::INPUT, 2, 10)
        task.record_completion(DataType::INPUT, 2, 10);

        // Asserts
        REQUIRE(task.sub_tasks.size() == 3);

        // 检查第一个 INPUT 任务 (index 0) 的目标列表现在只剩下 {1}
        REQUIRE(task.sub_tasks[0].type == DataType::INPUT);
        REQUIRE(task.sub_tasks[0].size == 10);
        REQUIRE(task.sub_tasks[0].target_ids.size() == 1);
        REQUIRE(task.sub_tasks[0].target_ids.find(1) != task.sub_tasks[0].target_ids.end());

        // 检查 WEIGHT 任务 (index 1) 完全不受影响
        REQUIRE(task.sub_tasks[1].type == DataType::WEIGHT);
        REQUIRE(task.sub_tasks[1].size == 50);
        REQUIRE(task.sub_tasks[1].target_ids.size() == 1);
        REQUIRE(task.sub_tasks[1].target_ids.find(1) != task.sub_tasks[0].target_ids.end());

        // 检查第二个 INPUT 任务 (index 2) 完全不受影响
        REQUIRE(task.sub_tasks[2].type == DataType::INPUT);
        REQUIRE(task.sub_tasks[2].size == 20);
        REQUIRE(task.sub_tasks[2].target_ids.size() == 1);
        REQUIRE(task.sub_tasks[2].target_ids.find(2) != task.sub_tasks[0].target_ids.end());

        REQUIRE(task.is_complete() == false);
    }

    SECTION("Handling multiple identical sub-tasks (edge case)") {
        // 目的: 验证实现是否能正确处理"多个 (type, size) 完全相同的任务"
        // Setup: 创建一个 DispatchTask，为其 sub_tasks 添加两个 (type, size) 完全相同的子任务
        DispatchTask task;

        // INPUT, size=10, targets={1, 2}
        DataDispatchInfo input_task1;
        input_task1.type = DataType::INPUT;
        input_task1.size = 10;
        input_task1.target_ids = {1, 2};
        task.sub_tasks.push_back(input_task1);

        // INPUT, size=10, targets={3, 4}
        DataDispatchInfo input_task2;
        input_task2.type = DataType::INPUT;
        input_task2.size = 10;
        input_task2.target_ids = {3, 4};
        task.sub_tasks.push_back(input_task2);

        // 初始状态验证
        REQUIRE(task.sub_tasks.size() == 2);
        REQUIRE(task.is_complete() == false);

        // Act: 调用 task.record_completion(DataType::INPUT, 3, 10)
        task.record_completion(DataType::INPUT, 3, 10);

        // Asserts
        REQUIRE(task.sub_tasks.size() == 2);

        // 检查第一个 INPUT 任务 (targets={1,2}) 完全不受影响
        REQUIRE(task.sub_tasks[0].type == DataType::INPUT);
        REQUIRE(task.sub_tasks[0].size == 10);
        REQUIRE(task.sub_tasks[0].target_ids.size() == 2);
        REQUIRE(std::find(task.sub_tasks[0].target_ids.begin(),
                        task.sub_tasks[0].target_ids.end(), 1) != task.sub_tasks[0].target_ids.end());
        REQUIRE(std::find(task.sub_tasks[0].target_ids.begin(),
                        task.sub_tasks[0].target_ids.end(), 2) != task.sub_tasks[0].target_ids.end());

        // 检查第二个 INPUT 任务 (targets={3,4}) 现在只剩下 {4}
        REQUIRE(task.sub_tasks[1].type == DataType::INPUT);
        REQUIRE(task.sub_tasks[1].size == 10);
        REQUIRE(task.sub_tasks[1].target_ids.size() == 1);
        REQUIRE(task.sub_tasks[1].target_ids.find(4) != task.sub_tasks[0].target_ids.end());

        REQUIRE(task.is_complete() == false);
    }

    SECTION("Calling with non-existent target should not crash") {
        // Setup: 创建一个包含 INPUT, size=10, targets={1, 2} 的 DispatchTask
        DispatchTask task;

        DataDispatchInfo input_task;
        input_task.type = DataType::INPUT;
        input_task.size = 10;
        input_task.target_ids = {1, 2};
        task.sub_tasks.push_back(input_task);

        // 初始状态验证
        REQUIRE(task.sub_tasks.size() == 1);

        // Act & Asserts
        REQUIRE_NOTHROW(task.record_completion(DataType::INPUT, 99, 10)); // 目标不存在
        REQUIRE(task.sub_tasks.size() == 1); // 确认状态没有被意外修改
        REQUIRE(task.sub_tasks[0].target_ids.size() == 2); // 目标列表保持不变
        REQUIRE(task.is_complete() == false);
    }

    SECTION("Calling with non-existent task should not crash") {
        // Setup: 创建一个包含 INPUT, size=10, targets={1, 2} 的 DispatchTask
        DispatchTask task;

        DataDispatchInfo input_task;
        input_task.type = DataType::INPUT;
        input_task.size = 10;
        input_task.target_ids = {1, 2};
        task.sub_tasks.push_back(input_task);

        // 初始状态验证
        REQUIRE(task.sub_tasks.size() == 1);

        // Act & Asserts
        REQUIRE_NOTHROW(task.record_completion(DataType::WEIGHT, 1, 50)); // 任务不存在
        REQUIRE(task.sub_tasks.size() == 1); // 确认状态没有被意外修改
        REQUIRE(task.sub_tasks[0].type == DataType::INPUT); // 任务保持不变
        REQUIRE(task.is_complete() == false);
    }

    SECTION("Multiple completions leading to empty sub-tasks") {
        // Setup: 创建一个包含多个目标的任务
        DispatchTask task;

        DataDispatchInfo output_task;
        output_task.type = DataType::OUTPUT;
        output_task.size = 5;
        output_task.target_ids = {10, 20, 30};
        task.sub_tasks.push_back(output_task);

        // 初始状态验证
        REQUIRE(task.sub_tasks.size() == 1);
        REQUIRE(task.sub_tasks[0].target_ids.size() == 3);
        REQUIRE(task.is_complete() == false);

        // Act: 逐个完成所有目标
        task.record_completion(DataType::OUTPUT, 10, 5);
        REQUIRE(task.sub_tasks.size() == 1);
        REQUIRE(task.sub_tasks[0].target_ids.size() == 2);

        task.record_completion(DataType::OUTPUT, 20, 5);
        REQUIRE(task.sub_tasks.size() == 1);
        REQUIRE(task.sub_tasks[0].target_ids.size() == 1);

        task.record_completion(DataType::OUTPUT, 30, 5);

        // Asserts: 所有目标完成后，子任务应该被删除
        REQUIRE(task.sub_tasks.empty() == true);
        REQUIRE(task.is_complete() == true);
    }

    SECTION("Empty task edge case") {
        // Setup: 创建一个空的 DispatchTask
        DispatchTask task;

        // 初始状态验证
        REQUIRE(task.sub_tasks.empty() == true);
        REQUIRE(task.is_complete() == true);

        // Act & Asserts: 在空任务上调用 record_completion 不应该崩溃
        REQUIRE_NOTHROW(task.record_completion(DataType::INPUT, 1, 10));
        REQUIRE(task.sub_tasks.empty() == true);
        REQUIRE(task.is_complete() == true);
    }
}

int sc_main(int argc, char* argv[]) {
  return 0;
}