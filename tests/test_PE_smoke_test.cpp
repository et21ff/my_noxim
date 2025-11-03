#include "systemc.h"
#include "Testbench.h" // 包含我们之前设计的测试平台框架
#include "ConfigurationManager.h"
#include "NoC.h"
const std::string test_yaml_content = R"(
topology: "HIERARCHICAL"

# 层次化详细配置
hierarchical_config:
  num_levels: 3
  connection_mode: "tree"
  
  # 每层的详细配置
  level_configs:
    - level: 0
      node_type: "DRAM"
      buffer_size: 512
      roles: "ROLE_DRAM"
      fanouts: 1  # 该层每个节点连接到下一层的节点数
      
    - level: 1  
      node_type: "GLB" 
      buffer_size: 512
      roles: "ROLE_GLB"
      fanouts: 1  # 该层每个节点连接到下一层的节点数
      
    - level: 2
      node_type: "COMPUTE"
      buffer_size: 64  
      roles: "ROLE_BUFFER"
      fanouts: 0
      

buffer_depth: 8 # <--- [建议修改] 稍微调大一点，避免初始调试时因缓冲区太小而产生干扰
# size of flits, in bits
flit_size: 32
# lenght in mm of router to hub connection
r2h_link_length: 2.0
# lenght in mm of router to router connection
r2r_link_length: 1.0
n_virtual_channels: 1
# Routing algorithms:
routing_algorithm: XY # <--- 在1x1的网络里，路由算法不重要，XY即可
routing_table_filename: ""
dyad_threshold: 0.6
# ... (selection_strategy 等保持不变) ...
selection_strategy: RANDOM
# ------------------- [重要] 禁用所有不相关的特性 -------------------

# WIRELESS CONFIGURATION (禁用)
use_winoc: false
use_wirxsleep: false
# ------------------- SIMULATION PARAMETERS -------------------
#
clock_period_ps: 1000
reset_time: 10 # <--- [建议修改] 缩短reset时间，让我们的逻辑更快开始
simulation_time: 10000 # <--- [关键修改] 增加模拟时间，确保我们能看到完整的交互链
stats_warm_up_time: 0 # <--- [建议修改] 禁用warm up，我们想从一开始就看log
# power breakdown, nodes communication details
detailed: false
# stop after a given amount of load has been processed
max_volume_to_be_drained: 0
show_buffer_stats: false
# ... (detailed, max_volume_to_beROLE_DRAM_drained 等保持不变) ...

# Verbosity level:
verbose_mode: VERBOSE_OFF # <--- [关键修改] 设置为LOW，可以看到一些基本的网络活动日志

# Trace (可选，但推荐)
trace_mode: false # 如果需要详细的flit追踪，可以设为true，但日志会非常多
trace_filename: "debug_trace.log"

# ------------------- [重要] 禁用Noxim自带的流量注入 -------------------
# 我们现在用自己的逻辑生成流量，所以要把Noxim自带的流量注入率设为0

min_packet_size: 8
max_packet_size: 8
packet_injection_rate: 0.1 # <--- [关键修改] 设为0！
probability_of_retransmission: 0.1 # <--- [关键修改] 设为0！

# Traffic distribution:
# 将其设置为TABLE_BASED，但提供一个空文件，确保它不会产生任何随机流量。
traffic_distribution: TRAFFIC_TABLE_BASED # <--- [关键修改]
traffic_table_filename: "empty_traffic.txt" # <--- 创建一个空的txt文件

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

          - trigger: { on_timestep: "fallback" }
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
    wait(2000, SC_NS); 

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


int sc_main(int arg_num, char *arg_vet[]) {
    // 实例化我们的测试平台
    drained_volume = 0;

    std::cout<<"start configuration..."<<std::endl;
    configure(arg_num, arg_vet);

        // ========================================================================
    // [核心] 手动初始化全局拓扑映射表 (替代临时的 NoC 对象)
    // ========================================================================
    std::cout << "--- Manually Initializing Global Topology Maps for a 1x1x1 system ---" << std::endl;

    // --- 1. 定义拓扑常量 ---
    const int total_nodes = 3;
    const int num_levels = 3;

    // --- 2. 分配内存 ---
    int* node_level_map = new int[total_nodes];
    int* parent_map = new int[total_nodes];
    int** child_map = new int*[total_nodes];
    for (int i = 0; i < total_nodes; ++i) {
        // 即使一个节点没有孩子，也为它分配一个小的数组，以避免空指针
        child_map[i] = new int[1]; 
    }

    // --- 3. 填充映射表 ---

    // --- Node 0 (DRAM @ Level 0) ---
    node_level_map[0] = 0;
    parent_map[0] = -1;       // 根节点，没有父节点
    child_map[0][0] = 1;      // 唯一的子节点是 Node 1 (GLB)

    // --- Node 1 (GLB @ Level 1) ---
    node_level_map[1] = 1;
    parent_map[1] = 0;        // 父节点是 Node 0 (DRAM)
    child_map[1][0] = 2;      // 唯一的子节点是 Node 2 (BUFFER)

    // --- Node 2 (BUFFER @ Level 2) ---
    node_level_map[2] = 2;
    parent_map[2] = 1;        // 父节点是 Node 1 (GLB)
    // Node 2 是叶子节点，没有子节点。child_map[2] 指向一个空列表，这没问题。
    // (fanout 为 0)

    // --- 4. 将手动创建的数组赋值给 GlobalParams ---
    GlobalParams::node_level_map = node_level_map;
    GlobalParams::parent_map = parent_map;
    GlobalParams::child_map = child_map;
    GlobalParams::num_nodes = total_nodes;
    GlobalParams::num_levels = num_levels;

    // 我们还需要 fanouts_per_level，它直接来自 YAML
    // 假设你的 loadConfiguration 已经解析了它。
    // 如果没有，我们也手动创建它：
    if (GlobalParams::fanouts_per_level == nullptr) {
        GlobalParams::fanouts_per_level = new int[num_levels];
        GlobalParams::fanouts_per_level[0] = 1; // Level 0 (DRAM) 的 fanout 是 1
        GlobalParams::fanouts_per_level[1] = 1; // Level 1 (GLB) 的 fanout 是 1
        GlobalParams::fanouts_per_level[2] = 0; // Level 2 (BUFFER) 的 fanout 是 0
    }
    
    std::cout << "--- Global Maps Initialized Manually ---" << std::endl;



    std::cout << "Step 2: Performing topology calculation using a temporary NoC instance..." << std::endl;
    Testbench tb("tb");
    sc_clock clk("clk", 1, SC_NS); // 1ns 时钟周期
    tb.clock(clk);


    





    

    // 启动 SystemC 仿真，它会一直运行直到遇到 sc_stop()
    std::cout << "Starting simulation..." << std::endl;
    sc_start();
    std::cout << "Simulation finished at " << sc_time_stamp() << std::endl;
    return 0;

}