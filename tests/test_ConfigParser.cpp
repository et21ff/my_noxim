#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "ConfigParser.h" // 假设的路径

// --- 准备一个用于测试的 YAML 字符串 ---
const std::string test_yaml_content = R"(
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

// --- 开始测试 ---

TEST_CASE("YAML configuration is parsed correctly into WorkloadConfig struct", "[ConfigParser]") {
    
    WorkloadConfig config;
    
    // 使用 loadWorkloadConfigFromString 进行测试，避免文件 I/O
    std::cout << "--- CATCH2 TEST: Starting test_ConfigParser ---" << std::endl;
    REQUIRE_NOTHROW(config = loadWorkloadConfigFromString(test_yaml_content));

    // 使用 validate 函数进行初步检查
    REQUIRE(validateWorkloadConfig(config) == true);

    SECTION("Working Set is parsed correctly") {
        REQUIRE(config.working_set.size() == 1);
        const auto& glb_ws = config.working_set[0];
        REQUIRE(glb_ws.role == "ROLE_GLB");
        REQUIRE(glb_ws.data.size() == 2);
        REQUIRE(glb_ws.data[0].data_space == "Weights");
        REQUIRE(glb_ws.data[0].size == 96);
        REQUIRE(glb_ws.data[1].data_space == "Inputs");
        REQUIRE(glb_ws.data[1].reuse_strategy == "resident");
    }

    SECTION("Data Flow Specs are parsed correctly") {
        REQUIRE(config.data_flow_specs.size() == 1);
        const auto& glb_spec = config.data_flow_specs[0];
        REQUIRE(glb_spec.role == "ROLE_GLB");

        SECTION("Schedule Template is parsed correctly") {
            const auto& tmpl = glb_spec.schedule_template;
            REQUIRE(tmpl.total_timesteps == 256);
            REQUIRE(tmpl.delta_events.size() == 2);
        }

        SECTION("FILL Delta Event is parsed correctly") {
            const auto& fill_event = glb_spec.schedule_template.delta_events[0];
            REQUIRE(fill_event.name == "FILL");
            REQUIRE(fill_event.target_group == "ALL_COMPUTE_PES");
            
            // 验证 Trigger
            REQUIRE(fill_event.trigger.type == "on_timestep_modulo");
            REQUIRE(fill_event.trigger.params.size() == 2);
            REQUIRE(fill_event.trigger.params[0] == 16);
            REQUIRE(fill_event.trigger.params[1] == 0);

            // 验证 Delta
            REQUIRE(fill_event.delta.weights == 6);
            REQUIRE(fill_event.delta.inputs == 3);
            REQUIRE(fill_event.delta.outputs == 2);
        }

        SECTION("DELTA Delta Event is parsed correctly") {
            const auto& delta_event = glb_spec.schedule_template.delta_events[1];
            REQUIRE(delta_event.name == "DELTA");

            // 验证 Trigger
            REQUIRE(delta_event.trigger.type == "default");
            REQUIRE(delta_event.trigger.is_default() == true);
            
            // 验证 Delta
            REQUIRE(delta_event.delta.weights == 0);
            REQUIRE(delta_event.delta.inputs == 1);
            REQUIRE(delta_event.delta.outputs == 2);
        }
    }
    
    // 可以在这里调用 printWorkloadConfig(config) 来手动检查
    // std::cout << "\n--- Parsed Config For Visual Inspection ---" << std::endl;
    // printWorkloadConfig(config);
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