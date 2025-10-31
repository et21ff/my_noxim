#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "ConfigParser.h" // 假设的路径
#include "dbg.h"          // 用于调试输出
#include <set>
#include <cassert>

std::map<PE_Role, RoleChannelCapabilities> CapabilityMap;
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

// --- infer_capabilities_from_workload 函数实现 ---
void infer_capabilities_from_workload(const WorkloadConfig& workload) {
    // 准备阶段：创建临时 map 用于收集唯一的能力值
    std::map<PE_Role, std::set<int>> temp_main_caps;
    std::map<PE_Role, std::set<int>> temp_output_caps;

    // 主循环：遍历所有数据流规格
    for (const auto& spec : workload.data_flow_specs) {
        // 将字符串角色转换为枚举
        PE_Role source_role = stringToRole(spec.role);

        // 跳过无效角色
        if (source_role == ROLE_UNUSED) {
            continue;
        }

        // 确定目标角色与边界检查
        int next_role_int = static_cast<int>(source_role) + 1;

        // 严格的边界检查
        assert(next_role_int <= ROLE_BUFFER && "源角色是层级中的最后一级，不能有目标！");

        PE_Role target_role = static_cast<PE_Role>(next_role_int);

        // 内层循环：遍历所有 delta_events 和 actions
        if (spec.has_schedule()) {
            for (const auto& delta_event : spec.schedule_template->delta_events) {
                for (const auto& action : delta_event.actions) {
                    // 忽略 size 为 0 的 action
                    if (action.size == 0) {
                        continue;
                    }

                    // 将字符串 data_space 转换为 DataType
                    DataType data_type = stringToDataType(action.data_space);

                    // 根据数据类型分配到相应的缓冲区
                    if (data_type == DataType::INPUT || data_type == DataType::WEIGHT) {
                        // 数据进入 target_role 的主缓冲区
                        temp_main_caps[target_role].insert(static_cast<int>(action.size));
                    } else if (data_type == DataType::OUTPUT) {
                        // 数据与 target_role 的输出缓冲区相关
                        temp_output_caps[target_role].insert(static_cast<int>(action.size));
                    }
                    // 忽略 UNKNOWN 数据类型
                }
            }
        }
    }

    // 最终填充：清空并填充 GlobalParams::CapabilityMap
    CapabilityMap.clear();

    // 遍历所有已知的有效 PE_Role
    for (int role_int = ROLE_DRAM; role_int <= ROLE_BUFFER; ++role_int) {
        PE_Role role = static_cast<PE_Role>(role_int);

        // 创建 Capability 对象
        RoleChannelCapabilities capability;

        // 始终将 0 作为基础能力添加
        capability.main_channel_caps.push_back(0);
        capability.output_channel_caps.push_back(0);

        // 复制临时 map 中的能力值到 vector
        for (int cap : temp_main_caps[role]) {
            capability.main_channel_caps.push_back(cap);
        }

        for (int cap : temp_output_caps[role]) {
            capability.output_channel_caps.push_back(cap);
        }

        // 将 Capability 对象赋值给 GlobalParams::CapabilityMap
        CapabilityMap[role] = capability;
    }
}

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

TEST_CASE("infer_capabilities_from_workload function works correctly", "[ConfigParser][CapabilityInference]") {
    WorkloadConfig config;

    // 加载测试配置
    REQUIRE_NOTHROW(config = loadWorkloadConfigFromString(test_yaml_content));
    REQUIRE(validateWorkloadConfig(config) == true);

    SECTION("Capability inference matches expected results") {
        // 调用推断函数
        REQUIRE_NOTHROW(infer_capabilities_from_workload(config));

        // 验证 GlobalParams::CapabilityMap 已被填充
        REQUIRE(CapabilityMap.size() == 3); // ROLE_DRAM, ROLE_GLB, ROLE_BUFFER

        // 验证 ROLE_GLB 的能力（从 ROLE_DRAM 推断）
        const auto& glb_caps = CapabilityMap[ROLE_GLB];
        REQUIRE(glb_caps.main_channel_caps.size() >= 1); // 至少包含基础能力 0
        REQUIRE(glb_caps.output_channel_caps.size() >= 1); // 至少包含基础能力 0

        // 检查基础能力
        REQUIRE(glb_caps.main_channel_caps[0] == 0);
        REQUIRE(glb_caps.output_channel_caps[0] == 0);

        // 验证 ROLE_BUFFER 的能力（从 ROLE_GLB 推断）
        const auto& buffer_caps = CapabilityMap[ROLE_BUFFER];
        REQUIRE(buffer_caps.main_channel_caps.size() >= 1); // 至少包含基础能力 0
        REQUIRE(buffer_caps.output_channel_caps.size() >= 1); // 至少包含基础能力 0

        // 检查基础能力
        REQUIRE(buffer_caps.main_channel_caps[0] == 0);
        REQUIRE(buffer_caps.output_channel_caps[0] == 0);

        // 验证从测试配置推断出的具体能力值
        // 从 ROLE_GLB 的 FILL_TO_PES 事件推断：Weights=6, Inputs=3 -> main_channel_caps
        // 从 ROLE_GLB 的 DELTA_TO_PES 事件推断：Inputs=1, Outputs=2
        bool found_weights_6 = false, found_inputs_3 = false, found_inputs_1 = false, found_outputs_2 = false;

        for (int cap : buffer_caps.main_channel_caps) {
            if (cap == 6) found_weights_6 = true;
            if (cap == 3) found_inputs_3 = true;
            if (cap == 1) found_inputs_1 = true;
        }

        for (int cap : buffer_caps.output_channel_caps) {
            if (cap == 2) found_outputs_2 = true;
        }

        REQUIRE(found_weights_6);
        REQUIRE(found_inputs_3);
        REQUIRE(found_inputs_1);
        REQUIRE(found_outputs_2);
    }

    // SECTION("Function handles edge cases gracefully") {
    //     // 清空能力映射
    //     CapabilityMap.clear();

    //     // 使用简化的测试配置
    //     WorkloadConfig simple_config;
    //     REQUIRE_NOTHROW(simple_config = loadWorkloadConfigFromString(simple_test_yaml_content));

    //     // 调用推断函数
    //     REQUIRE_NOTHROW(infer_capabilities_from_workload(simple_config));

    //     // 验证映射仍然被正确初始化
    //     REQUIRE(CapabilityMap.size() == 3);
    // }
}

int sc_main(int argc, char* argv[]) {
    // 这个函数永远不会被调用，因为程序的入口是 Catch2 生成的 main()
    // 它存在的唯一目的就是为了让链接器满意
    return 0;
}