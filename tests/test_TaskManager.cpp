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
              Weights: 6
              Inputs: 3
              Outputs: 2
          - name: "DELTA"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: "default"
            delta:
              Weights: 0
              Inputs: 1
              Outputs: 2
)";

    // 解析配置
    WorkloadConfig config = loadWorkloadConfigFromString(test_yaml_content);

    // 配置 TaskManager
    TaskManager task_manager;
    task_manager.Configure(config, "ROLE_GLB");

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
              Weights: 100
          - name: "DELTA"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: "default"
            delta:
              Inputs: 10
)";

    // 解析配置
    WorkloadConfig config = loadWorkloadConfigFromString(test_yaml_content);

    // 配置 TaskManager
    TaskManager task_manager;
    task_manager.Configure(config, "ROLE_GLB");

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
        task_manager.Configure(config, "ROLE_GLB");
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
              Inputs: 1
)";

        WorkloadConfig config = loadWorkloadConfigFromString(no_glb_yaml_content);
        TaskManager task_manager;

        // 没有 GLB 规格的配置应该不会导致配置成功
        task_manager.Configure(config, "ROLE_GLB");
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
              Inputs: 1
)";

        WorkloadConfig config = loadWorkloadConfigFromString(valid_yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config, "ROLE_GLB");

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
              Inputs: 1
)";

        WorkloadConfig config = loadWorkloadConfigFromString(valid_yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config, "ROLE_GLB");

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
              Inputs: 5
)";

        WorkloadConfig config = loadWorkloadConfigFromString(yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config, "ROLE_GLB");

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
              Weights: 8
              Inputs: 4
              Outputs: 6
)";

        WorkloadConfig config = loadWorkloadConfigFromString(yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config, "ROLE_GLB");

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
              Weights: 10
          - name: "FILL_8"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: 8
            delta:
              Weights: 20
          - name: "DELTA"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep: "default"
            delta:
              Inputs: 1
)";

        WorkloadConfig config = loadWorkloadConfigFromString(yaml_content);
        TaskManager task_manager;
        task_manager.Configure(config, "ROLE_GLB");

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

TEST_CASE("Multi-role configuration with working_set, compute_latency, and command_definitions", "[TaskManager][MultiRole]") {
    // 创建测试用的完整多角色 YAML 内容
    const std::string multi_role_yaml_content = R"(
workload:
  working_set:
    - role: "ROLE_GLB"
      data:
        - { data_space: "Weights", size: 96, reuse_strategy: "resident" }
        - { data_space: "Inputs",  size: 18, reuse_strategy: "resident" }
        - { data_space: "Outputs", size: 512, reuse_strategy: "resident" }

    - role: "ROLE_COMPUTE"
      data:
        - { data_space: "Weights", size: 6, reuse_strategy: "resident" }
        - { data_space: "Inputs",  size: 3, reuse_strategy: "resident" }
        - { data_space: "Outputs", size: 2, reuse_strategy: "resident" }


  data_flow_specs:
    - role: "ROLE_DRAM"
      schedule_template:
        total_timesteps: 1
        delta_events:
          - trigger: { on_timestep: "default" }
            name: "INITIAL_LOAD_TO_GLB"
            delta: { Weights: 96, Inputs: 18, Outputs: 512 }
            target_group: "1" 

    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 256
        delta_events:
          - trigger: { on_timestep_modulo: [16, 0] }
            name: "FILL_TO_PES"
            delta: { Weights: 6, Inputs: 3, Outputs: 2 }
            target_group: "ALL_COMPUTE_PES"
          - trigger: { on_timestep: "default" }
            name: "DELTA_TO_PES"
            delta: { Weights: 0, Inputs: 1, Outputs: 2 }
            target_group: "ALL_COMPUTE_PES"
      command_definitions:
        - command_id: 0
          name: "EVICT_AFTER_INIT_LOAD"
          evict_payload: { Weights: 96, Inputs: 18, Outputs: 512 }

    - role: "ROLE_COMPUTE"
      properties:
        compute_latency: 6
      command_definitions:
        - command_id: 0
          name: "EVICT_DELTA"
          evict_payload: { Weights: 0, Inputs: 1, Outputs: 2 }
        - command_id: 1
          name: "EVICT_FULL_CONTEXT"
          evict_payload: { Weights: 6, Inputs: 3, Outputs: 2 }
)";

    SECTION("GLB role configuration") {
        WorkloadConfig config = loadWorkloadConfigFromString(multi_role_yaml_content);
        TaskManager glb_manager;

        // 配置 GLB 角色
        REQUIRE_NOTHROW(glb_manager.Configure(config, "ROLE_GLB"));

        // 验证 GLB 工作集
        const RoleWorkingSet* glb_ws = glb_manager.get_working_set_for_role("ROLE_GLB");
        REQUIRE(glb_ws != nullptr);
        REQUIRE(glb_ws->role == "ROLE_GLB");
        REQUIRE(glb_ws->data.size() == 3);
        REQUIRE(glb_ws->data[0].data_space == "Weights");
        REQUIRE(glb_ws->data[0].size == 96);
        REQUIRE(glb_ws->data[1].data_space == "Inputs");
        REQUIRE(glb_ws->data[1].size == 18);
        REQUIRE(glb_ws->data[2].data_space == "Outputs");
        REQUIRE(glb_ws->data[2].size == 512);

        // 验证 GLB 角色有调度模板和命令定义
        REQUIRE(glb_manager.role_has_schedule_template("ROLE_GLB") == true);
        REQUIRE(glb_manager.role_has_command_definitions("ROLE_GLB") == true);

        // 验证 GLB 调度任务生成
        REQUIRE(glb_manager.get_total_timesteps() == 256);

        // 验证 FILL 事件（timestep 0）
        DispatchTask fill_task = glb_manager.get_task_for_timestep(0);
        REQUIRE(fill_task.sub_tasks.find(DataType::WEIGHT) != fill_task.sub_tasks.end());
        REQUIRE(fill_task.sub_tasks.at(DataType::WEIGHT).size == 6);
        REQUIRE(fill_task.sub_tasks.at(DataType::INPUT).size == 3);
        REQUIRE(fill_task.sub_tasks.at(DataType::OUTPUT).size == 2);

        // 验证 DELTA 事件（timestep 1）
        DispatchTask delta_task = glb_manager.get_task_for_timestep(1);
        REQUIRE(delta_task.sub_tasks.find(DataType::WEIGHT) == delta_task.sub_tasks.end());
        REQUIRE(delta_task.sub_tasks.at(DataType::INPUT).size == 1);
        REQUIRE(delta_task.sub_tasks.at(DataType::OUTPUT).size == 2);

        // 验证命令定义
        const std::vector<CommandDefinition>* glb_commands = glb_manager.get_commands_for_role("ROLE_GLB");
        REQUIRE(glb_commands != nullptr);
        REQUIRE(glb_commands->size() == 1);
        REQUIRE(glb_commands->at(0).command_id == 0);
        REQUIRE(glb_commands->at(0).name == "EVICT_AFTER_INIT_LOAD");
        REQUIRE(glb_commands->at(0).evict_payload.weights == 96);
        REQUIRE(glb_commands->at(0).evict_payload.inputs == 18);
        REQUIRE(glb_commands->at(0).evict_payload.outputs == 512);
    }

    SECTION("COMPUTE role configuration") {
        WorkloadConfig config = loadWorkloadConfigFromString(multi_role_yaml_content);
        TaskManager compute_manager;

        // 配置 COMPUTE 角色
        REQUIRE_NOTHROW(compute_manager.Configure(config, "ROLE_COMPUTE"));

        // 验证 COMPUTE 工作集
        const RoleWorkingSet* compute_ws = compute_manager.get_working_set_for_role("ROLE_COMPUTE");
        REQUIRE(compute_ws != nullptr);
        REQUIRE(compute_ws->role == "ROLE_COMPUTE");
        REQUIRE(compute_ws->data.size() == 3);
        REQUIRE(compute_ws->data[0].size == 6);
        REQUIRE(compute_ws->data[1].size == 3);
        REQUIRE(compute_ws->data[2].size == 2);

        // 验证 COMPUTE 角色的属性
        int compute_latency = compute_manager.get_compute_latency_for_role("ROLE_COMPUTE");
        REQUIRE(compute_latency == 6);

        // 验证 COMPUTE 角色没有调度模板但有命令定义
        REQUIRE(compute_manager.role_has_schedule_template("ROLE_COMPUTE") == false);
        REQUIRE(compute_manager.role_has_command_definitions("ROLE_COMPUTE") == true);

        // 验证 COMPUTE 角色不生成调度任务
        REQUIRE(compute_manager.get_total_timesteps() == 0);

        // 验证命令定义
        const std::vector<CommandDefinition>* compute_commands = compute_manager.get_commands_for_role("ROLE_COMPUTE");
        REQUIRE(compute_commands != nullptr);
        REQUIRE(compute_commands->size() == 2);

        REQUIRE(compute_commands->at(0).command_id == 0);
        REQUIRE(compute_commands->at(0).name == "EVICT_DELTA");
        REQUIRE(compute_commands->at(0).evict_payload.weights == 0);
        REQUIRE(compute_commands->at(0).evict_payload.inputs == 1);
        REQUIRE(compute_commands->at(0).evict_payload.outputs == 2);

        REQUIRE(compute_commands->at(1).command_id == 1);
        REQUIRE(compute_commands->at(1).name == "EVICT_FULL_CONTEXT");
        REQUIRE(compute_commands->at(1).evict_payload.weights == 6);
        REQUIRE(compute_commands->at(1).evict_payload.inputs == 3);
        REQUIRE(compute_commands->at(1).evict_payload.outputs == 2);
    }

    SECTION("DRAM role configuration") {
        WorkloadConfig config = loadWorkloadConfigFromString(multi_role_yaml_content);
        TaskManager dram_manager;

        // 配置 DRAM 角色
        REQUIRE_NOTHROW(dram_manager.Configure(config, "ROLE_DRAM"));

        // 验证 DRAM 角色有调度模板但没有命令定义
        REQUIRE(dram_manager.role_has_schedule_template("ROLE_DRAM") == true);
        REQUIRE(dram_manager.role_has_command_definitions("ROLE_DRAM") == false);

        // 验证 DRAM 调度任务生成
        REQUIRE(dram_manager.get_total_timesteps() == 1);

        

        DispatchTask dram_task = dram_manager.get_task_for_timestep(0);
        REQUIRE(dram_task.sub_tasks.find(DataType::WEIGHT) != dram_task.sub_tasks.end());
        REQUIRE(dram_task.sub_tasks.at(DataType::WEIGHT).size == 96);
        REQUIRE(dram_task.sub_tasks.at(DataType::INPUT).size == 18);
        REQUIRE(dram_task.sub_tasks.at(DataType::OUTPUT).size == 512);
    }

    SECTION("Role utility functions") {
        WorkloadConfig config = loadWorkloadConfigFromString(multi_role_yaml_content);
        TaskManager manager;
        manager.Configure(config, "ROLE_GLB");

        // 测试角色列表获取
        auto roles = manager.get_all_configured_roles();
        // REQUIRE(roles.size() == 3);
        // REQUIRE(std::find(roles.begin(), roles.end(), "ROLE_GLB") != roles.end());
        // REQUIRE(std::find(roles.begin(), roles.end(), "ROLE_COMPUTE") != roles.end());
        // REQUIRE(std::find(roles.begin(), roles.end(), "ROLE_DRAM") != roles.end());

        // 测试工作数据大小计算
        size_t glb_size = manager.get_total_working_data_size_for_role("ROLE_GLB");
        size_t compute_size = manager.get_total_working_data_size_for_role("ROLE_COMPUTE");
        REQUIRE(glb_size == 96 + 18 + 512);  // 626
        REQUIRE(compute_size == 6 + 3 + 2);   // 11

        // 测试默认计算延迟
        int dram_latency = manager.get_compute_latency_for_role("ROLE_DRAM");
        REQUIRE(dram_latency == 0); // DRAM 没有定义计算延迟，应该返回默认值 0
    }
}

// 用于 Catch2 框架的 sc_main 函数（如果需要）
int sc_main(int argc, char* argv[]) {
    // 这个函数永远不会被调用，因为程序的入口是 Catch2 生生的 main()
    // 它存在的唯一目的就是为了让链接器满意
    return 0;
}