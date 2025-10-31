#include "systemc.h"
#include "Testbench.h" // 包含我们之前设计的测试平台框架

const std::string test_yaml_content = R"(
workload:
  working_set:
    - role: "ROLE_DRAM"
      data:
        - { data_space: "Weights", size: 96, reuse_strategy: "resident" }
        - { data_space: "Inputs", size: 18, reuse_strategy: "resident" }
        - { data_space: "Outputs", size: 512, reuse_strategy: "resident" }
    - role: "ROLE_GLB"
      data:
        - { data_space: "Weights", size: 96, reuse_strategy: "resident" }
        - { data_space: "Inputs", size: 18, reuse_strategy: "resident" }
        - { data_space: "Outputs", size: 512, reuse_strategy: "resident" }
    - role: "ROLE_BUFFER"
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
            target_group: "1,"
            delta:
              - { data_space: "Weights", size: 96 }
              - { data_space: "Inputs",  size: 18 }
              - { data_space: "Outputs", size: 512 }

    # -----------------------------------------------------------
    # 规格 B: 针对 ROLE_GLB
    # -----------------------------------------------------------
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 256
        delta_events:
          - trigger: { on_timestep_modulo: [16, 0] }
            name: "FILL_TO_PES"
            target_group: "2,"
            
            delta:
              - { data_space: "Weights", size: 6 }
              - { data_space: "Inputs",  size: 3 }
              - { data_space: "Outputs", size: 2 }

          - trigger: { on_timestep: "default" }
            name: "DELTA_TO_PES"
            target_group: "2,"
            
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
    - role: "ROLE_BUFFER"
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
// -----------------------------------------------------------------------------
// Testbench 的 run_test_scenario 实现
// -----------------------------------------------------------------------------
unsigned int drained_volume;

void Testbench::run_test_scenario() {
    
    // --- 1. 初始化阶段 ---
    std::cout << "@ " << sc_time_stamp() << " Testbench: Starting DRAM dispatch integration test..." << std::endl;
    reset.write(true);
    wait(5, SC_NS);
    
    reset.write(false);
    std::cout << "@ " << sc_time_stamp() << " Testbench: Reset released." << std::endl;

    // --- 2. 设置测试环境 ---
    std::cout << "@ " << sc_time_stamp() << " Testbench: Setting downstream GLB to ready." << std::endl;
    // 模拟 GLB 对两个通道都准备好接收大包
    // Main channel (level 3 for FULL_LOAD), Output channel (level 1 for OUTPUTS)
    // Encoded: (1 << 2) | 3 = 0b0111 = 7
    wait(2,SC_NS);
    sender_pe->buffer_manager_->OnDataReceived(DataType::WEIGHT, 96);
    sender_pe->buffer_manager_->OnDataReceived(DataType::INPUT, 18);
    sender_pe->output_buffer_manager_->OnDataReceived(DataType::OUTPUT, 512);
    // --- 3. 运行并等待 ---
    std::cout << "@ " << sc_time_stamp() << " Testbench: Running simulation to observe DRAM dispatch..." << std::endl;
    // 运行足够长的时间，以确保 DRAM 能完成这个巨大的包的发送
    wait(4000, SC_NS); 

    // --- 4. 结束仿真 ---
    std::cout << "@ " << sc_time_stamp() << " Testbench: Test finished. Stopping simulation." << std::endl;
    sc_stop();
}

  void Testbench::downstream_mock_process() {
        // 在每个时钟周期，为两个端口都检查
        for (int i = 0; i < 2; ++i) {
            // 1. 检查是否有新的请求
            //    (如果 DUT 的 req 与我们当前的 ack 电平相反)
            if (req_from_dut[i].read() == 1 - current_level_rx[i]) {
                
                // 这是一个新的 flit，我们已经"接收"了它
                // (真正的读取和记录由 monitor 进程完成)

                // 2. [核心] 翻转我们内部的 ack 电平，为下一次握手做准备
                current_level_rx[i] = 1 - current_level_rx[i];
            }
            
            // 3. 持续地将我们当前的 ack 电平写回端口
            ack_to_dut[i].write(current_level_rx[i]);
            ready_to_dut[0].write(7);
            ready_to_dut[1].write(7); // 始终表示准备好接收
        }
    }

  void Testbench::monitor_flit_output_process() {
        // 在每个时钟周期，为两个端口都检查
        for (int i = 0; i < 2; ++i) {
            // 关键的观察点：当 req 和 ack 电平不一致时，
            // 表明有一个正在传输的、尚未被确认的 flit 在信号线上。
            if (req_from_dut[i].read() != ack_to_dut[i].read()) {
                
                // 为了避免重复记录同一个 flit，我们可以增加一个检查
                // (更简单的做法是在 downstream_mock 中记录，但为了职责分离，我们这样做)
                // 这里的逻辑可以进一步简化，但核心是：在 req 变化且未被 ack 时记录。
                // 最简单的、虽然不完全精确但有效的监控方式是：
                if (req_from_dut[i].read() == 1 - current_level_rx[i]) {
                    Flit f = flit_from_dut[i].read();
                    received_flits[i].push(f);
                    
                    std::cout << "@" << sc_time_stamp() << " TB_MONITOR: Port[" << i << "] captured " 
                              << flit_type_to_str(f.flit_type) << " -> " << f.dst_id 
                              << " (seq: " << f.sequence_no << ")" << (f.is_multicast ? " [MULTICAST]" : "[UNICAST]") <<" flit command:"<<f.command<< std::endl;
                }
            }
        }
    } 
// -----------------------------------------------------------------------------
// sc_main: 仿真程序的入口点
// -----------------------------------------------------------------------------

int sc_main(int argc, char* argv[]) {
    // 实例化我们的测试平台
    drained_volume = 0;
    Testbench tb("tb");
    sc_clock clk("clk", 1, SC_NS); // 1ns 时钟周期
    tb.clock(clk);
    GlobalParams::workload = loadWorkloadConfigFromString(test_yaml_content);

    GlobalParams::CapabilityMap[ROLE_GLB].main_channel_caps = {0,18,96};
    GlobalParams::CapabilityMap[ROLE_GLB].output_channel_caps = {0,512};
    GlobalParams::CapabilityMap[ROLE_BUFFER].main_channel_caps = {0,1,3,6};
    GlobalParams::CapabilityMap[ROLE_BUFFER].output_channel_caps = {0,2};
    
    

    // 启动 SystemC 仿真，它会一直运行直到遇到 sc_stop()
    std::cout << "Starting simulation..." << std::endl;
    sc_start();
    std::cout << "Simulation finished at " << sc_time_stamp() << std::endl;
    return 0;

}