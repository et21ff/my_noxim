#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "ConfigParser.h" // 包含 loadWorkloadConfigFromString 和所有 struct
#include "TaskManager.h"  // 包含我们要测试的 TaskManager

TEST_CASE("DispatchTask struct methods work correctly", "[TaskManager][DispatchTask]") {
    DispatchTask task;

    // 为其 sub_tasks 添加内容
    task.sub_tasks[DataType::INPUT] = DataDispatchInfo(5, {1, 2, 5});
    task.sub_tasks[DataType::WEIGHT] = DataDispatchInfo(10, {1, 2});

    REQUIRE(task.is_complete() == false);

    // 测试完成部分 INPUT 目标
    task.record_completion(DataType::INPUT, 2);
    REQUIRE(task.sub_tasks.at(DataType::INPUT).target_ids.size() == 2);
    REQUIRE(task.sub_tasks.at(DataType::INPUT).target_ids == std::vector<int>{1, 5});

    // 完成 INPUT 任务
    task.record_completion(DataType::INPUT, 1);
    task.record_completion(DataType::INPUT, 5);

    // 此时 Inputs 的 target_ids 应该为空，record_completion 应该已经将 Inputs 这个子任务删除了
    REQUIRE(task.sub_tasks.count(DataType::INPUT) == 0);
    REQUIRE(task.is_complete() == false); // 因为 Weights 任务还在

    // 完成 WEIGHT 任务
    task.record_completion(DataType::WEIGHT, 1);
    task.record_completion(DataType::WEIGHT, 2);

    REQUIRE(task.sub_tasks.count(DataType::WEIGHT) == 0);
    REQUIRE(task.is_complete() == true);
}

TEST_CASE("Basic configuration loading and expansion (Happy Path)", "[TaskManager][Configure]") {
    // 创建测试用的 YAML 内容
    const std::string test_yaml_content = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 16
        delta_events:
          - name: "FILL"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: 0
            delta:
              weights: 6
              inputs: 3
              outputs: 2
          - name: "DELTA"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: "default"
            delta:
              weights: 0
              inputs: 1
              outputs: 2
)";

    // 解析配置
    WorkloadConfig config = loadWorkloadConfigFromString(test_yaml_content);

    // 配置 TaskManager
    TaskManager task_manager;
    task_manager.Configure(config);

    // 基本配置验证
    REQUIRE(task_manager.is_configured() == true);
    REQUIRE(task_manager.get_total_timesteps() == 16);

    // 验证 timestep 0 (FILL 事件)
    DispatchTask task0 = task_manager.get_task_for_timestep(0);
    REQUIRE(task0.sub_tasks.count(DataType::WEIGHT) == 1);
    REQUIRE(task0.sub_tasks.at(DataType::WEIGHT).size == 6);
    REQUIRE(task0.sub_tasks.at(DataType::INPUT).size == 3);
    REQUIRE(task0.sub_tasks.at(DataType::OUTPUT).size == 2);

    // 验证 timestep 1 (DELTA 事件)
    DispatchTask task1 = task_manager.get_task_for_timestep(1);
    REQUIRE(task1.sub_tasks.count(DataType::WEIGHT) == 0); // 因为 delta.weights 是0，所以不应有这个子任务
    REQUIRE(task1.sub_tasks.at(DataType::INPUT).size == 1);
    REQUIRE(task1.sub_tasks.at(DataType::OUTPUT).size == 2);

    // 验证 timestep 15 (最后一个 DELTA 步)
    DispatchTask task15 = task_manager.get_task_for_timestep(15);
    REQUIRE(task15.sub_tasks.count(DataType::WEIGHT) == 0);
    REQUIRE(task15.sub_tasks.at(DataType::INPUT).size == 1);
    REQUIRE(task15.sub_tasks.at(DataType::OUTPUT).size == 2);
}

TEST_CASE("on_timestep_modulo trigger", "[TaskManager][Trigger]") {
    // 创建测试用的 YAML 内容，专门测试 modulo 触发器
    const std::string test_yaml_content = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 32
        delta_events:
          - name: "FILL"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep_modulo: [16, 0]
            delta:
              weights: 100
          - name: "DELTA"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: "default"
            delta:
              inputs: 10
)";

    // 解析配置
    WorkloadConfig config = loadWorkloadConfigFromString(test_yaml_content);

    // 配置 TaskManager
    TaskManager task_manager;
    task_manager.Configure(config);

    // 验证 timestep 0: 应该包含 Weights，大小为 100 (因为 0 % 16 == 0)
    DispatchTask task0 = task_manager.get_task_for_timestep(0);
    REQUIRE(task0.sub_tasks.count(DataType::WEIGHT) == 1);
    REQUIRE(task0.sub_tasks.at(DataType::WEIGHT).size == 100);
    REQUIRE(task0.sub_tasks.count(DataType::INPUT) == 0);

    // 验证 timestep 1: 应该只包含 Inputs，大小为 10
    DispatchTask task1 = task_manager.get_task_for_timestep(1);
    REQUIRE(task1.sub_tasks.count(DataType::WEIGHT) == 0);
    REQUIRE(task1.sub_tasks.at(DataType::INPUT).size == 10);

    // 验证 timestep 15: 应该与 timestep 1 相同
    DispatchTask task15 = task_manager.get_task_for_timestep(15);
    REQUIRE(task15.sub_tasks.count(DataType::WEIGHT) == 0);
    REQUIRE(task15.sub_tasks.at(DataType::INPUT).size == 10);

    // 验证 timestep 16: 应该再次包含 Weights，大小为 100 (因为 16 % 16 == 0)
    DispatchTask task16 = task_manager.get_task_for_timestep(16);
    REQUIRE(task16.sub_tasks.count(DataType::WEIGHT) == 1);
    REQUIRE(task16.sub_tasks.at(DataType::WEIGHT).size == 100);
    REQUIRE(task16.sub_tasks.count(DataType::INPUT) == 0);

    // 验证 timestep 17: 应该与 timestep 1 相同
    DispatchTask task17 = task_manager.get_task_for_timestep(17);
    REQUIRE(task17.sub_tasks.count(DataType::WEIGHT) == 0);
    REQUIRE(task17.sub_tasks.at(DataType::INPUT).size == 10);
}

TEST_CASE("Edge cases and error handling", "[TaskManager][ErrorHandling]") {

    SECTION("Empty data_flow_specs") {
        // 创建空的配置
        const std::string empty_yaml_content = R"(
workload:
  data_flow_specs: []
)";

        WorkloadConfig config = loadWorkloadConfigFromString(empty_yaml_content);
        TaskManager task_manager;

        // 空配置应该不会导致配置成功
        task_manager.Configure(config);
        REQUIRE(task_manager.is_configured() == false);
        REQUIRE(task_manager.get_total_timesteps() == 0);
    }

    SECTION("No ROLE_GLB spec") {
        // 创建没有 GLB 规格的配置
        const std::string no_glb_yaml_content = R"(
workload:
  data_flow_specs:
    - role: "ROLE_COMPUTE"
      schedule_template:
        total_timesteps: 10
        delta_events:
          - name: "COMPUTE_EVENT"
            target_group: "ALL"
            trigger:
              on_timestep: "default"
            delta:
              inputs: 1
)";

        WorkloadConfig config = loadWorkloadConfigFromString(no_glb_yaml_content);
        TaskManager task_manager;

        // 没有 GLB 规格的配置应该不会导致配置成功
        task_manager.Configure(config);
        REQUIRE(task_manager.is_configured() == false);
        REQUIRE(task_manager.get_total_timesteps() == 0);
    }

    SECTION("Invalid timestep queries") {
        // 使用有效配置
        const std::string valid_yaml_content = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 10
        delta_events:
          - name: "TEST_EVENT"
            target_group: "ALL"
            trigger:
              on_timestep: "default"
            delta:
              inputs: 1
)";

        WorkloadConfig config = loadWorkloadConfigFromString(valid_yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config);

        // 测试负数时间步
        DispatchTask task_neg = task_manager.get_task_for_timestep(-1);
        REQUIRE(task_neg.is_complete() == true);

        // 测试超出范围的时间步
        DispatchTask task_over = task_manager.get_task_for_timestep(10); // 等于 total_timesteps
        REQUIRE(task_over.is_complete() == true);

        DispatchTask task_way_over = task_manager.get_task_for_timestep(100);
        REQUIRE(task_way_over.is_complete() == true);
    }

    SECTION("clear() method") {
        const std::string valid_yaml_content = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 16
        delta_events:
          - name: "TEST_EVENT"
            target_group: "ALL"
            trigger:
              on_timestep: "default"
            delta:
              inputs: 1
)";

        WorkloadConfig config = loadWorkloadConfigFromString(valid_yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config);

        // 验证配置成功
        REQUIRE(task_manager.is_configured() == true);
        REQUIRE(task_manager.get_total_timesteps() == 16);

        // 调用 clear()
        task_manager.clear();

        // 验证已清空
        REQUIRE(task_manager.is_configured() == false);
        REQUIRE(task_manager.get_total_timesteps() == 0);
    }
}

TEST_CASE("Target group resolution", "[TaskManager][Targets]") {
    SECTION("ALL_COMPUTE_PES group") {
        const std::string yaml_content = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 1
        delta_events:
          - name: "TEST_EVENT"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: "default"
            delta:
              inputs: 5
)";

        WorkloadConfig config = loadWorkloadConfigFromString(yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config);

        DispatchTask task = task_manager.get_task_for_timestep(0);

        // 验证 ALL_COMPUTE_PES 被解析为具体的目标ID
        REQUIRE(task.sub_tasks.at(DataType::INPUT).target_ids.size() > 0);
        // 根据实现，应该包含ID 1-15
        std::vector<int> expected_targets = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        REQUIRE(task.sub_tasks.at(DataType::INPUT).target_ids == expected_targets);
    }

    SECTION("Multiple data types with same target group") {
        const std::string yaml_content = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 1
        delta_events:
          - name: "MULTI_TYPE_EVENT"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: "default"
            delta:
              weights: 8
              inputs: 4
              outputs: 6
)";

        WorkloadConfig config = loadWorkloadConfigFromString(yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config);

        DispatchTask task = task_manager.get_task_for_timestep(0);

        // 验证所有数据类型都有相同的目标组
        REQUIRE(task.sub_tasks.at(DataType::WEIGHT).target_ids == task.sub_tasks.at(DataType::INPUT).target_ids);
        REQUIRE(task.sub_tasks.at(DataType::WEIGHT).target_ids == task.sub_tasks.at(DataType::OUTPUT).target_ids);

        // 验证数据大小
        REQUIRE(task.sub_tasks.at(DataType::WEIGHT).size == 8);
        REQUIRE(task.sub_tasks.at(DataType::INPUT).size == 4);
        REQUIRE(task.sub_tasks.at(DataType::OUTPUT).size == 6);
    }
}

TEST_CASE("Complex trigger scenarios", "[TaskManager][Trigger][Complex]") {
    SECTION("Multiple FILL events with different conditions") {
        const std::string yaml_content = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 32
        delta_events:
          - name: "FILL_0"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: 0
            delta:
              weights: 10
          - name: "FILL_8"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: 8
            delta:
              weights: 20
          - name: "DELTA"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: "default"
            delta:
              inputs: 1
)";

        WorkloadConfig config = loadWorkloadConfigFromString(yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config);

        // 验证 timestep 0: 特定的 FILL_0 事件
        DispatchTask task0 = task_manager.get_task_for_timestep(0);
        REQUIRE(task0.sub_tasks.at(DataType::WEIGHT).size == 10);

        // 验证 timestep 8: 特定的 FILL_8 事件
        DispatchTask task8 = task_manager.get_task_for_timestep(8);
        REQUIRE(task8.sub_tasks.at(DataType::WEIGHT).size == 20);

        // 验证 timestep 1: 默认 DELTA 事件
        DispatchTask task1 = task_manager.get_task_for_timestep(1);
        REQUIRE(task1.sub_tasks.count(DataType::WEIGHT) == 0);
        REQUIRE(task1.sub_tasks.at(DataType::INPUT).size == 1);

        // 验证 timestep 16: 默认 DELTA 事件 (不匹配任何特定 on_timestep)
        DispatchTask task16 = task_manager.get_task_for_timestep(16);
        REQUIRE(task16.sub_tasks.count(DataType::WEIGHT) == 0);
        REQUIRE(task16.sub_tasks.at(DataType::INPUT).size == 1);
    }
}

// 用于 Catch2 框架的 sc_main 函数（如果需要）
int sc_main(int argc, char* argv[]) {
    // 这个函数永远不会被调用，因为程序的入口是 Catch2 生生的 main()
    // 它存在的唯一目的就是为了让链接器满意
    return 0;
}