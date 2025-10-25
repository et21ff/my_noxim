#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "ConfigParser.h" // 假设的路径
#include "dbg.h"          // 用于调试输出

// --- 准备一个用于测试的 YAML 字符串（完整的多角色配置）---
const std::string test_yaml_content = R"(
workload:
  working_set:
    - role: "ROLE_GLB"
      data:
        - { data_space: "Weights", size: 96, reuse_strategy: "resident" }
        - { data_space: "Inputs", size: 18, reuse_strategy: "resident" }
        - { data_space: "Outputs", size: 512, reuse_strategy: "resident" }
    - role: "ROLE_COMPUTE"
      data:
        - { data_space: "Weights", size: 6, reuse_strategy: "temporal" }
        - { data_space: "Inputs",  size: 3, reuse_strategy: "temporal" }
        - { data_space: "Outputs", size: 2, reuse_strategy: "temporal" }

  data_flow_specs:

    - role: "ROLE_DRAM"
      schedule_template:
        total_timesteps: 1
        delta_events:
          - trigger: { on_timestep: "default" }
            name: "INITIAL_LOAD_TO_GLB"
            target_group: "ALL_GLBS"
            delta:
              - { data_space: "Weights", size: 96 }
              - { data_space: "Inputs",  size: 18 }
              - { data_space: "Outputs", size: 512 }
              - { data_space: "Weight",  size: 1 , target_group: "1,2,3" }

    # -----------------------------------------------------------
    # 规格 B: 针对 ROLE_GLB
    # -----------------------------------------------------------
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 256
        delta_events:
          - trigger: { on_timestep_modulo: [16, 0] }
            name: "FILL_TO_PES"
            target_group: "ALL_COMPUTE_PES" 
            
            delta:
              - { data_space: "Weights", size: 6 }
              - { data_space: "Inputs",  size: 3 }
              - { data_space: "Outputs", size: 2 }

          - trigger: { on_timestep: "default" }
            name: "DELTA_TO_PES"
            target_group: "ALL_COMPUTE_PES"
            
            delta:
              - { data_space: "Inputs",  size: 1 }
              - { data_space: "Outputs", size: 2 }

      command_definitions:
        - command_id: 0
          name: "EVICT_AFTER_INIT_LOAD"
          evict_payload: { Weights: 96, Inputs: 18, Outputs: 512 }

    # -----------------------------------------------------------
    # 规格 C: 针对 ROLE_COMPUTE
    # -----------------------------------------------------------
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

// --- 简化的测试配置（仅用于基础测试）---
const std::string simple_test_yaml_content = R"(
workload:
  working_set:
    - role: "ROLE_GLB"
      data:
        - { data_space: "Weights", size: 96, reuse_strategy: "resident" }
        - { data_space: "Inputs",  size: 18, reuse_strategy: "resident" }

  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 256
        delta_events:
          - name: "FILL"
            target_group: "ALL_COMPUTE_PES"
            trigger:
              on_timestep_modulo: [16, 0]
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

// --- 开始测试 ---

TEST_CASE("Complete multi-role YAML configuration is parsed correctly", "[ConfigParser][MultiRole]") {

    WorkloadConfig config;

    // 使用 loadWorkloadConfigFromString 进行测试，避免文件 I/O
    std::cout << "--- CATCH2 TEST: Starting multi-role ConfigParser test ---" << std::endl;
    REQUIRE_NOTHROW(config = loadWorkloadConfigFromString(test_yaml_content));

    // 使用 validate 函数进行初步检查
    REQUIRE(validateWorkloadConfig(config) == true);

    SECTION("All roles are present in working set") {
        REQUIRE(config.working_set.size() == 2);

        // 验证 GLB 工作集
        const auto& glb_ws = config.working_set[0];
        REQUIRE(glb_ws.role == "ROLE_GLB");
        REQUIRE(glb_ws.data.size() == 3);
        REQUIRE(glb_ws.data[0].data_space == "Weights");
        REQUIRE(glb_ws.data[0].size == 96);
        REQUIRE(glb_ws.data[1].data_space == "Inputs");
        REQUIRE(glb_ws.data[1].size == 18);
        REQUIRE(glb_ws.data[2].data_space == "Outputs");
        REQUIRE(glb_ws.data[2].size == 512);

        // 验证 COMPUTE 工作集
        const auto& compute_ws = config.working_set[1];
        REQUIRE(compute_ws.role == "ROLE_COMPUTE");
        REQUIRE(compute_ws.data.size() == 3);
        REQUIRE(compute_ws.data[0].size == 6);
        REQUIRE(compute_ws.data[1].size == 3);
        REQUIRE(compute_ws.data[2].size == 2);
    }

    SECTION("All three role specs are parsed correctly") {
        REQUIRE(config.data_flow_specs.size() == 3);

        SECTION("DRAM spec is parsed correctly") {
            const auto* dram_spec = config.find_spec_for_role("ROLE_DRAM");
            REQUIRE(dram_spec != nullptr);
            REQUIRE(dram_spec->role == "ROLE_DRAM");
            REQUIRE(dram_spec->has_schedule());
            REQUIRE_FALSE(dram_spec->has_commands());

            const auto& tmpl = *dram_spec->schedule_template;
            REQUIRE(tmpl.total_timesteps == 1);
            REQUIRE(tmpl.delta_events.size() == 1);

            const auto& event = tmpl.delta_events[0];
            REQUIRE(event.name == "INITIAL_LOAD_TO_GLB");
            REQUIRE(event.actions.size() == 4);
            REQUIRE(event.actions[0].data_space == "Weights");
            REQUIRE(event.actions[0].size == 96);
            REQUIRE(event.actions[1].data_space == "Inputs");
            REQUIRE(event.actions[1].size == 18);
            REQUIRE(event.actions[2].data_space == "Outputs");
            REQUIRE(event.actions[2].size == 512);
            REQUIRE(event.actions[0].target_group == "ALL_GLBS");
            REQUIRE(event.actions[1].target_group == "ALL_GLBS");
            REQUIRE(event.actions[2].target_group == "ALL_GLBS");
            REQUIRE(event.actions[3].data_space == "Weight");
            REQUIRE(event.actions[3].size == 1);
            REQUIRE(event.actions[3].target_group == "1,2,3");

        }

        SECTION("GLB spec is parsed correctly") {
            const auto* glb_spec = config.find_spec_for_role("ROLE_GLB");
            REQUIRE(glb_spec != nullptr);
            REQUIRE(glb_spec->role == "ROLE_GLB");
            REQUIRE(glb_spec->has_schedule());
            REQUIRE(glb_spec->has_commands());

            // 验证调度模板
            const auto& tmpl = *glb_spec->schedule_template;
            REQUIRE(tmpl.total_timesteps == 256);
            REQUIRE(tmpl.delta_events.size() == 2);

            // 验证命令定义
            REQUIRE(glb_spec->command_definitions.size() == 1);
            const auto& cmd = glb_spec->command_definitions[0];
            REQUIRE(cmd.command_id == 0);
            REQUIRE(cmd.name == "EVICT_AFTER_INIT_LOAD");
            dbg(cmd.evict_payload.weights);
            dbg(cmd.evict_payload.inputs);
            dbg(cmd.evict_payload.outputs);
            REQUIRE(cmd.evict_payload.weights == 96);
            REQUIRE(cmd.evict_payload.inputs == 18);
            REQUIRE(cmd.evict_payload.outputs == 512);
        }

        SECTION("COMPUTE spec is parsed correctly") {
            const auto* compute_spec = config.find_spec_for_role("ROLE_COMPUTE");
            REQUIRE(compute_spec != nullptr);
            REQUIRE(compute_spec->role == "ROLE_COMPUTE");
            REQUIRE_FALSE(compute_spec->has_schedule());
            REQUIRE(compute_spec->has_commands());

            // 验证属性
            REQUIRE(compute_spec->properties.compute_latency == 6);

            // 验证命令定义
            REQUIRE(compute_spec->command_definitions.size() == 2);

            const auto& cmd1 = compute_spec->command_definitions[0];
            REQUIRE(cmd1.command_id == 0);
            REQUIRE(cmd1.name == "EVICT_DELTA");
            REQUIRE(cmd1.evict_payload.weights == 0);
            REQUIRE(cmd1.evict_payload.inputs == 1);
            REQUIRE(cmd1.evict_payload.outputs == 2);

            const auto& cmd2 = compute_spec->command_definitions[1];
            REQUIRE(cmd2.command_id == 1);
            REQUIRE(cmd2.name == "EVICT_FULL_CONTEXT");
            REQUIRE(cmd2.evict_payload.weights == 6);
            REQUIRE(cmd2.evict_payload.inputs == 3);
            REQUIRE(cmd2.evict_payload.outputs == 2);
        }
    }
}

TEST_CASE("YAML parsing handles errors gracefully", "[ConfigParser]") {
    SECTION("Throws on missing workload node") {
        const std::string bad_yaml = "some_other_key: value";
        REQUIRE_THROWS_AS(loadWorkloadConfigFromString(bad_yaml), std::runtime_error);
    }
    
    SECTION("Throws on malformed file") {
        const std::string malformed_yaml = "workload: [key: value"; // 括号不匹配
        REQUIRE_THROWS_AS(loadWorkloadConfigFromString(malformed_yaml), std::runtime_error);
    }

    SECTION("Validation fails on incomplete config") {
        const std::string incomplete_yaml = R"(
workload:
  data_flow_specs:
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 0 # 无效值
        delta_events: []
)";
        WorkloadConfig config = loadWorkloadConfigFromString(incomplete_yaml);
        REQUIRE(validateWorkloadConfig(config) == false);
    }
}

int sc_main(int argc, char* argv[]) {
    // 这个函数永远不会被调用，因为程序的入口是 Catch2 生成的 main()
    // 它存在的唯一目的就是为了让链接器满意
    return 0;
}