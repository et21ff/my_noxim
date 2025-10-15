/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the processing element
 */

#include "ProcessingElement.h"
#include "dbg.h"
#include <numeric>
int ProcessingElement::randInt(int min, int max)
{
    return min +
	(int) ((double) (max - min + 1) * rand() / (RAND_MAX + 1.0));
}

// 引入ScheduleFactory来创建驱逐计划
#include "smartbuffer/ScheduleFactory.h"

void ProcessingElement::pe_init() {
    //========================================================================
    // I. 黄金参数定义 (Golden Parameters)
    //    - 源自Timeloop的逻辑分析和Accelergy的物理定义
    //========================================================================
     logical_timestamp = 0;
    // ---- 逻辑循环结构 (from Timeloop) ----
    const int K2_LOOPS = 16;
    const int P1_LOOPS = 16;
    
    // ---- 数据需求 (from Timeloop, 单位: Bytes) ----
    const int FILL_DATA_SIZE = 9;  // W(6) + I(3)
    const int DELTA_DATA_SIZE = 1; // I(1)
    
    // ---- 物理容量 (from Accelergy, 单位: Bytes) ----
    const int GLB_CAPACITY = 262144;
    const int BUFFER_CAPACITY = 64;
    
    //========================================================================
    // II. 通用状态初始化
    //     - 对所有PE都适用的默认值
    //========================================================================
    role = ROLE_UNUSED;
    current_data_size = 0;
    previous_ready_signal = -1; // 确保第一次ready信号变化时会打印日志
    all_receive_tasks_finished = false;
    all_transfer_tasks_finished = false;
    current_receive_loop_level = -1; // -1 表示没有任务
    current_transfer_loop_level = -1;
    
    //========================================================================
    // III. 基于角色的专属初始化
    //========================================================================

    if (local_id == 0) {
        // --- ROLE_DRAM: 理想化的、一次性任务的生产者 ---
        role = ROLE_DRAM;
        max_capacity = 20000; // 假设DRAM容量为64KB
        EvictionSchedule dram_schedule = ScheduleFactory::createDRAMEvictionSchedule();
        buffer_manager_.reset(new BufferManager(max_capacity, dram_schedule));
        buffer_manager_->OnDataReceived(DataType::WEIGHT, 10000);
        buffer_manager_->OnDataReceived(DataType::INPUT, 10000);
        
        // 初始化output buffer manager
        EvictionSchedule dram_output_schedule = ScheduleFactory::createDRAMEvictionSchedule();
        output_buffer_manager_.reset(new BufferManager(max_capacity, dram_output_schedule));
        max_capacity = -1;
        downstream_node_ids.push_back(1); // 下游是GLB
        upstream_node_ids.clear(); // DRAM没有上游
        
        // DRAM的发送任务: 响应GLB的一次性总加载请求
        // 假设GLB一次性请求所有数据 (W:96 + I:18 = 114 Bytes)
        transfer_task_queue.clear();
        transfer_task_queue.push_back({1});
        transfer_loop_counters.resize(1, 0);
        current_transfer_loop_level = 0;
        
        // DRAM的发送块大小
        transfer_fill_size = 96 + 18; // 一次性发送所有需要的数据
        
    } else if (local_id == 1) {
        // --- ROLE_GLB: 智能的、任务驱动的中间商 ---
        // current_data_size.write(0); // 初始为空
        role = ROLE_GLB;
        max_capacity = GLB_CAPACITY;
        downstream_node_ids.push_back(2); // 下游是Buffer

        EvictionSchedule glb_schedule = ScheduleFactory::createGLBEvictionSchedule();
        buffer_manager_.reset(new BufferManager(max_capacity, glb_schedule));
        current_data_size.write(buffer_manager_->GetCurrentSize()); // 用manager的状态初始化信号
        
        // 初始化output buffer manager
        EvictionSchedule glb_output_schedule = ScheduleFactory::createGLBEvictionSchedule();
        output_buffer_manager_.reset(new BufferManager(max_capacity, glb_output_schedule));
        // GLB的接收任务：从DRAM接收1次大的数据块
         transfer_task_queue.clear();
        receive_task_queue.push_back({1});
        receive_loop_counters.resize(1, 0);
        current_receive_loop_level = 0;
        
        // GLB的发送任务：向Buffer进行16x16的供应
        transfer_task_queue.clear();
        transfer_task_queue.push_back({K2_LOOPS});  // 16
        transfer_task_queue.push_back({P1_LOOPS});  // 16
        transfer_loop_counters.resize(2, 0);
        current_transfer_loop_level = 1; // 从最内层(P1)循环开始

        // GLB的发送块大小
        transfer_fill_size = FILL_DATA_SIZE;
        transfer_delta_size = DELTA_DATA_SIZE;

        // GLB的阶段控制 (由其发送任务驱动)
        current_stage = STAGE_FILL;

        required_data_on_fill =  18; // FILL阶段需要的输入数据大小
        required_data_on_delta = 18; // DELTA阶段需要的输入数据大小

    } else if (local_id == 2) {
        // --- ROLE_BUFFER: 带抽象消耗的最终目的地 ---
        // current_data_size.write(0); // 初始为空

        role = ROLE_BUFFER;
        max_capacity = BUFFER_CAPACITY;
        EvictionSchedule buffer_schedule = ScheduleFactory::createBufferPESchedule();
        buffer_manager_.reset(new BufferManager(max_capacity, buffer_schedule));
        current_data_size.write(buffer_manager_->GetCurrentSize()); // 用manager的状态初始
        
        // 初始化output buffer manager
        EvictionSchedule buffer_output_schedule = ScheduleFactory::createOutputBufferSchedule();
        output_buffer_manager_.reset(new BufferManager(max_capacity, buffer_output_schedule));
        downstream_node_ids.clear(); // 是终点
        upstream_node_ids.push_back(1); // 上游是GLB
        
        receive_task_queue.clear();
        receive_task_queue.push_back(K2_LOOPS);
        receive_task_queue.push_back(P1_LOOPS);
        receive_loop_counters.resize(2, 0);
        current_receive_loop_level = 1;

        // Buffer的消耗需求 (用于驱动抽象消耗和ready信号)
        required_data_on_fill = FILL_DATA_SIZE;
        required_data_on_delta = DELTA_DATA_SIZE;

        required_output_data_on_delta = 2;
        required_output_data_on_fill = 2;


        
        // 抽象消耗状态
        is_consuming = false;
        consume_cycles_left = 0;
        is_stalled_waiting_for_data = true; // 初始时等待数据

        // Buffer的阶段控制 (由其消耗/接收任务驱动)
        current_stage = STAGE_FILL;
    }
    // 可以在这里加一个调试日志来确认初始化结果
    cout << sc_time_stamp() << ": PE[" << local_id << "] initialized as " << role_to_str(role) 
         << " | TransferTasks: " << transfer_task_queue.size() 
         << " | ReceiveTasks: " << receive_task_queue.size() << endl;
    transmittedAtPreviousCycle = false;
}

void ProcessingElement::update_ready_signal() {
    if (reset.read()) {
        downstream_ready_out.write(0);
        previous_ready_signal = 0;
        return;
    }
    
    // 如果没有缓冲区，则不发送任何 ready 信号
    if (!buffer_manager_ || !output_buffer_manager_) {
        downstream_ready_out.write(0);
        return;
    }

    // --- 步骤 1: 计算每个缓冲区的独立 ready 状态 (0, 1, or 2) ---

    // 主数据缓冲区
    int main_ready = 0;
    int available_space = buffer_manager_->GetCapacity() - buffer_manager_->GetCurrentSize();
    if (available_space >= required_data_on_fill) {
        main_ready = 2; // 可以接收 FILL
    } else if (available_space >= required_data_on_delta) {
        main_ready = 1; // 只能接收 DELTA
    }

    // 输出数据缓冲区
    int output_ready = 0;
    int available_output_space = output_buffer_manager_->GetCapacity() - output_buffer_manager_->GetCurrentSize();
    // 假设输出通道的数据需求大小定义在 required_output_... 变量中
    if (available_output_space >= required_output_data_on_fill) {
        output_ready = 2; // 可以接收大的 Output 块
    } else if (available_output_space >= required_output_data_on_delta) {
        output_ready = 1; // 只能接收小的 Output 块
    }
    
    // --- 步骤 2: [核心] 使用位移和位或(OR)运算进行编码 ---
    // 将 output_ready 的2个比特位放在高位，main_ready 的2个比特位放在低位
    int ready_value = (output_ready << 2) | main_ready;
    
    // --- 步骤 3: 写入端口 ---
    downstream_ready_out.write(ready_value);

    // --- 步骤 4: 调试日志 (保持不变) ---
    if (ready_value != previous_ready_signal) {
        // (为了调试，可以打印出解码后的值)
        dbg(sc_time_stamp(), name(), "[READY_SIG] Updating ready_out from " 
             + std::to_string(previous_ready_signal) + " to " + std::to_string(ready_value),
             "Decoded -> MainReady:" + std::to_string(main_ready) +
             ", OutputReady:" + std::to_string(output_ready));
        
        previous_ready_signal = ready_value;
    }
}

void ProcessingElement::rxProcess() {
    // 步骤 0: 处理Reset信号 (保持不变)
    if (reset.read()) {
        ack_rx[0].write(0);
        current_level_rx = 0;
        // 在reset时，也应该清理其他与接收相关的状态
        return;
    }

    // 步骤 1: 遵循Noxim的握手协议 (核心结构保持不变)
    if (req_rx[0].read() == 1 - current_level_rx) {
        // 读取flit，这是我们处理的输入
        Flit flit = flit_rx[0].read();
        // ------------------- 我们注入的新逻辑 [开始] -------------------
        if (flit.flit_type == FLIT_TYPE_HEAD) {
            std::copy(std::begin(flit.payload_sizes),std::end(flit.payload_sizes), incoming_payload_sizes);
            // 读取包大小
            int total_size = std::accumulate(incoming_payload_sizes, incoming_payload_sizes + 3, 0);
             cout << sc_time_stamp() << ": " << name() << " RX <<< HEAD. Payload Mix (I/W/O): " 
                 << incoming_payload_sizes[0] << "/" << incoming_payload_sizes[1] << "/" << incoming_payload_sizes[2]
                 << ". Total Size: " << total_size << " bytes." << endl;

        }
        // 步骤 2: 只在接收到包尾(TAIL)时，才触发我们的上层逻辑
        if (flit.flit_type == FLIT_TYPE_TAIL) {
            cout << sc_time_stamp() << ": " << name() << " RX <<< TAIL from " << flit.src_id << endl;

            if (buffer_manager_) {
                // +++ 核心逻辑: 遍历元数据数组，逐一添加数据 +++
                bool success = true;
                if (incoming_payload_sizes[INPUT_IDX] > 0) {
                    success &= buffer_manager_->OnDataReceived(DataType::INPUT, incoming_payload_sizes[INPUT_IDX]);
                }
                if (incoming_payload_sizes[WEIGHT_IDX] > 0) {
                    success &= buffer_manager_->OnDataReceived(DataType::WEIGHT, incoming_payload_sizes[WEIGHT_IDX]);
                }
                if (incoming_payload_sizes[OUTPUT_IDX] > 0) {
                    success &= buffer_manager_->OnDataReceived(DataType::OUTPUT, incoming_payload_sizes[OUTPUT_IDX]);
                }
                assert(success && "FATAL: Buffer overflow. Ready/Ack logic is flawed.");
                buffer_state_changed_event.notify(); // 立即在当前delta周期触发
                
                int total_received_this_packet = std::accumulate(incoming_payload_sizes, incoming_payload_sizes + 3, 0);
                total_bytes_received += total_received_this_packet;
                update_ready_signal(); // 更新ready信号
                cout << sc_time_stamp() << ": " << name() << " RX data. New size: " 
                     << buffer_manager_->GetCurrentSize() << "/" << buffer_manager_->GetCapacity() << endl;
                                        }
                                }
     // 步骤 4: 翻转握手信号 (核心结构保持不变)
    current_level_rx = 1 - current_level_rx;
    }
    // 步骤 5: 将新的ack level写回端口 (核心结构保持不变)
    ack_rx[0].write(current_level_rx);
}

// in PE.cpp

void ProcessingElement::txProcess() {
    // 复位逻辑保持不变
    if (reset.read()) {
        req_tx[0].write(0);
        req_tx[1].write(0);
        current_level_tx[0] = 0;
        current_level_tx[1] = 0;
        while (!packet_queue.empty()) packet_queue.pop();
        while (!packet_queue_2.empty()) packet_queue_2.pop();
        return;
    }

    // --- 步骤 A: 如果需要，智能地生成Packet ---
    // 只有在队列为空时，才尝试生成新的Packet
    if (packet_queue.empty() && packet_queue_2.empty()) {
        // 根据角色调用生成逻辑。
        // run_storage_logic 内部已经包含了所有"意图"和"能力"的匹配检查。
        // 如果条件不满足，它什么也不会做，packet_queue 依然为空。
        if (role == ROLE_DRAM || role == ROLE_GLB) {
            run_storage_logic();
        }
        else if(role == ROLE_BUFFER){
            run_compute_logic();
        }
    }

    // --- 步骤 B: 统一的、智能的发送逻辑 ---
    // 如果队列中现在有Packet（无论是之前遗留的还是刚刚生成的），则尝试发送
    if(!packet_queue.empty()) {
        handle_tx_for_port(0); // 处理第0个端口
    }
    // 注意：output_txprocess 仍然独立处理 packet_queue_2，
    // 所以这里我们暂时只为 port 0 调用
    if(!packet_queue_2.empty()) {
        handle_tx_for_port(1); // 处理第1个端口
    }

    // --- +++ GLB 专用：同步检查与时间戳推进 +++ ---
    if (role == ROLE_GLB && dispatch_in_progress_) {
        // 检查当前发送批次是否完成
        if (is_dispatch_complete()) {
            cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_SYNC] *** SYNCHRONIZATION POINT ***" << endl;

            // 执行同步点操作
            dispatch_in_progress_ = false;  // 结束当前发送阶段

            // 批处理式的 BufferManager 更新（示例）
            if (buffer_manager_) {
                // 这里可以添加批处理更新逻辑，例如：
                // buffer_manager_->OnBatchTransferred(glb_timestep_);
                buffer_manager_->OnComputeFinished(glb_timestep_);
                buffer_state_changed_event.notify();
                
                cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_SYNC] BufferManager batch update for timestep "
                     << glb_timestep_ << endl;
            }

            // 推进主时间戳
            int old_timestep = glb_timestep_;
            glb_timestep_++;

            cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_SYNC] *** TIMESTEP ADVANCED: "
                 << old_timestep << " -> " << glb_timestep_ << " ***" << endl;

            // 可选：立即开始新的发送阶段
            // 注意：这里可能需要根据具体逻辑来决定是否立即开始下一阶段
            // 如果还有未完成的发送任务，可以立即开始下一阶段
            if (!all_transfer_tasks_finished) {
                start_new_dispatch_phase();
            } else {
                cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_SYNC] All transfer tasks completed. No more dispatches." << endl;
            }
        }
    }
}

void ProcessingElement::handle_tx_for_port(int port_index) {
    // 根据 port_index 选择正确的资源
    queue<Packet>& current_queue = (port_index == 0) ? packet_queue : packet_queue_2;
    bool& current_level = current_level_tx[port_index];
    
    // 确保队列非空
    if (current_queue.empty()) {
        return;
    }

    // 1. "窥探"队首的Packet，以确定其发送要求
    const Packet& pkt_to_send = current_queue.front();

    // 2. 根据Packet的真实大小，推断其发送要求 (这部分逻辑暂时只对port 0有效)
    int required_capability = 1; // 默认为1
    string packet_type_str = "DEFAULT";
    if (port_index == 0) {
        int payload_data_size = pkt_to_send.payload_data_size;
        if (payload_data_size == transfer_fill_size) {
            required_capability = 2;
            packet_type_str = "FILL";
        } else {
            required_capability = 1;
            packet_type_str = "DELTA";
        }
    }

    // 3. 读取下游 *当前* 的接收能力
    // 注意：目前downstream_ready_in只有一个输入，所以我们硬编码为[0]
    // 在未来的步骤中，这可能需要扩展
    int downstream_capability = downstream_ready_in[0]->read();

    // 4. 终极守门员检查：检查ack，并进行精确的能力匹配
    if (ack_tx[port_index].read() == current_level && downstream_capability >= required_capability) {
        // 所有检查通过！可以安全地发送Flit
        Flit flit = generate_next_flit_from_queue(current_queue, port_index == 1);

        if (flit.flit_type == FLIT_TYPE_HEAD) {
            cout << sc_time_stamp() << ": " << name() << " [TX_PORT_" << port_index << "] Sending HEAD of "
                 << packet_type_str << " packet to " << flit.dst_id << endl;
        }
        if (flit.flit_type == FLIT_TYPE_TAIL) {
            // 特定于 port 0 的逻辑
            if (port_index == 0 && buffer_manager_ && (role == ROLE_GLB || role == ROLE_DRAM)) {
                logical_timestamp++;
                buffer_manager_->OnComputeFinished(logical_timestamp);
                buffer_state_changed_event.notify();
                cout << sc_time_stamp() << ": " << name()
                     << " [LIFECYCLE] Data service finished at logical step " << logical_timestamp
                     << ". Buffer state is now: " << buffer_manager_->GetCurrentSize() << endl;
            }

            // +++ GLB 专用：更新发送跟踪状态 +++
            if (role == ROLE_GLB && dispatch_in_progress_) {
                int dst_id = flit.dst_id;

                // 检查是否在跟踪器中存在该目标节点
                auto it = dispatch_tracker_.find(dst_id);
                if (it != dispatch_tracker_.end()) {
                    // 获取当前发送的包的类型信息
                    std::string packet_type_sent = "UNKNOWN";
                    bool updated = false;

                    // 根据 flit 的 payload_sizes 确定包类型并更新对应状态
                    if (flit.payload_sizes[WEIGHT_IDX] > 0) {
                        it->second.weights_sent = true;
                        packet_type_sent = "WEIGHTS";
                        updated = true;
                    }
                    if (flit.payload_sizes[INPUT_IDX] > 0) {
                        it->second.inputs_sent = true;
                        packet_type_sent = (updated ? "MULTI" : "INPUTS");
                        updated = true;
                    }
                    if (flit.payload_sizes[OUTPUT_IDX] > 0) {
                        it->second.outputs_sent = true;
                        packet_type_sent = (updated ? "MULTI" : "OUTPUTS");
                        updated = true;
                    }

                    if (updated) {
                        cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_DISPATCH] Updated status for node "
                             << dst_id << ": " << packet_type_sent << " sent. Current status - Weights:"
                             << (it->second.weights_sent ? "✓" : "✗") << ", Inputs:"
                             << (it->second.inputs_sent ? "✓" : "✗") << ", Outputs:"
                             << (it->second.outputs_sent ? "✓" : "✗") << endl;
                    }
                } else {
                    cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_DISPATCH] WARNING: Received TAIL for untracked node "
                         << dst_id << endl;
                }
            }

             cout << sc_time_stamp() << ": " << name() << " [TX_PORT_" << port_index << "] Sending TAIL to " << flit.dst_id << endl;
        }

        flit_tx[port_index].write(flit);
        // 更新Noxim的握手协议状态
        current_level = 1 - current_level;
        req_tx[port_index].write(current_level);
    }
}

void ProcessingElement::start_new_dispatch_phase() {
    // 清空当前的发送跟踪器
    dispatch_tracker_.clear();

    // 获取当前 glb_timestep_ 的所有目标节点 ID
    // 注意：这里假设 downstream_node_ids 包含了当前时间步需要发送的所有目标
    // 在更复杂的实现中，可能需要根据 glb_timestep_ 来计算具体的目标列表

    cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_DISPATCH] Starting new dispatch phase for timestep "
         << glb_timestep_ << ". Targets: ";

    // 遍历所有下游节点，初始化跟踪状态
    for (int target_id : downstream_node_ids) {
        dispatch_tracker_[target_id] = PacketSentStatus();  // 使用默认构造函数（所有false）
        cout << target_id << " ";
    }
    cout << endl;

    // 标记发送阶段开始
    dispatch_in_progress_ = true;

    cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_DISPATCH] Dispatch phase initialized. Tracker size: "
         << dispatch_tracker_.size() << ", in_progress: " << (dispatch_in_progress_ ? "true" : "false") << endl;
}

bool ProcessingElement::is_dispatch_complete() {
    // 如果没有正在进行的发送，认为已完成
    if (!dispatch_in_progress_) {
        return true;
    }

    // 如果跟踪器为空，认为已完成（边界情况）
    if (dispatch_tracker_.empty()) {
        return true;
    }

    // --- 根据 glb_timestep_ 确定当前批次需要发送的数据类型 ---
    bool needs_weights = false;
    bool needs_inputs = false;
    bool needs_outputs = false;

    // 这里需要根据具体的应用逻辑来确定当前时间步需要发送哪些类型
    // 示例逻辑：可以根据 glb_timestep_ 的奇偶性或者其他模式来判断
    if (glb_timestep_ == 0) {
        // 第0步：发送所有类型（初始化阶段）
        needs_weights = true;
        needs_inputs = true;
        needs_outputs = true;
    } else {
        // 后续步骤：根据具体需求确定
        // 例如：偶数时间步发送 weights+inputs，奇数时间步只发送 inputs
        if (glb_timestep_ % 2 == 0) {
            needs_weights = true;
            needs_inputs = true;
        } else {
            needs_inputs = true;
        }

        // 假设 outputs 在特定条件下才发送
        if (glb_timestep_ % 4 == 3) {
            needs_outputs = true;
        }
    }

    cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_SYNC] Checking dispatch completion for timestep "
         << glb_timestep_ << ". Requirements - Weights:" << (needs_weights ? "✓" : "✗")
         << ", Inputs:" << (needs_inputs ? "✓" : "✗") << ", Outputs:" << (needs_outputs ? "✓" : "✗") << endl;

    // --- 检查每个目标的发送状态 ---
    for (const auto& [node_id, status] : dispatch_tracker_) {
        bool node_complete = true;
        std::vector<std::string> missing_types;

        // 检查每种需要的数据类型
        if (needs_weights && !status.weights_sent) {
            node_complete = false;
            missing_types.push_back("WEIGHTS");
        }
        if (needs_inputs && !status.inputs_sent) {
            node_complete = false;
            missing_types.push_back("INPUTS");
        }
        if (needs_outputs && !status.outputs_sent) {
            node_complete = false;
            missing_types.push_back("OUTPUTS");
        }

        // 如果这个节点有任何未完成的任务，立即返回 false
        if (!node_complete) {
            cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_SYNC] Node " << node_id
                 << " still missing: ";
            for (const auto& type : missing_types) {
                cout << type << " ";
            }
            cout << endl;
            return false;
        }
    }

    // 所有节点的所有必需任务都已完成
    cout << sc_time_stamp() << ": PE[" << local_id << "] [GLB_SYNC] All required dispatches completed for timestep "
         << glb_timestep_ << endl;
    return true;
}

// void ProcessingElement::output_txprocess() {
//     // 复位逻辑保持不变
//     if (reset.read()) {
//         req_tx[1].write(0);
//         current_level_tx_2 = 0;
//         while (!packet_queue_2.empty()) packet_queue_2.pop();
//         return;
//     }

//     // +++ 调试：为DRAM添加特殊处理 +++
//     if (role == ROLE_DRAM) {
//         // DRAM作为最终接收端，不需要再转发output数据
//         // 但可以记录接收到的output数据总量
//         static int dram_total_output_received = 0;
//         if (output_buffer_manager_ && output_buffer_manager_->GetCurrentSize() > dram_total_output_received) {
//             int new_data = output_buffer_manager_->GetCurrentSize() - dram_total_output_received;
//             dram_total_output_received = output_buffer_manager_->GetCurrentSize();
//             cout << sc_time_stamp() << ": " << name() 
//                  << " [DRAM_OUTPUT_FINAL] Accumulated " << new_data 
//                  << " bytes of output. Total: " << dram_total_output_received << endl;
//         }
//         return;
//     }

//     // 步骤 A: 生成Output包的逻辑保持不变...
//     if (packet_queue_2.empty()) {
//         if (output_buffer_manager_ && output_buffer_manager_->GetCurrentSize() >= 4) {
//             // +++ 增强调试：记录发送意图 +++
//             cout << sc_time_stamp() << ": " << name() 
//                  << " [OUTPUT_TX_INTENT] Planning to send 4-byte OUTPUT packet. Buffer: " 
//                  << output_buffer_manager_->GetCurrentSize() 
//                  << " → Target: " << (role == ROLE_BUFFER ? "GLB(1)" : "DRAM(0)") << endl;

//             Packet pkt;
//             pkt.src_id = local_id;
            
//             if (role == ROLE_BUFFER) {
//                 pkt.dst_id = 1; // Buffer PE发送回GLB
//             } else if (role == ROLE_GLB) {
//                 pkt.dst_id = 0; // GLB发送回DRAM
//             }
            
//             int payload_sizes[3] = {0, 0, 4};
//             std::copy(std::begin(payload_sizes), std::end(payload_sizes), pkt.payload_sizes);
            
//             pkt.payload_data_size = 4;
//             pkt.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
            
//             // 计算flit数量的逻辑...
//             int bytes_per_flit = GlobalParams::flit_size / 8;
//             int payload_flits = (4 + bytes_per_flit - 1) / bytes_per_flit;
//             int num_flits = (payload_flits <= 1) ? 3 : (1 + payload_flits + 1);
            
//             pkt.flit_left = pkt.size = num_flits;
//             pkt.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
            
//             packet_queue_2.push(pkt);
            
//             // +++ 调试：确认包已生成 +++
//             cout << sc_time_stamp() << ": " << name() 
//                  << " [OUTPUT_TX_GENERATED] Created output packet: "
//                  << local_id << "→" << pkt.dst_id 
//                  << ", " << num_flits << " flits, vc=" << pkt.vc_id << endl;
//         }
//     }

//     // 步骤 B: 发送逻辑
//     if (!packet_queue_2.empty()) {
//         if (ack_tx[1].read() == current_level_tx_2) {
//             Flit flit = nextOutputFlit();
            
//             if (flit.flit_type == FLIT_TYPE_HEAD) {
//                 cout << sc_time_stamp() << ": " << name() 
//                      << " [OUTPUT_TX_HEAD] Sending OUTPUT HEAD: " 
//                      << flit.src_id << "→" << flit.dst_id 
//                      << ", seq=" << flit.sequence_no << "/" << flit.sequence_length << endl;
//             }
            
//             if (flit.flit_type == FLIT_TYPE_TAIL) {
//                 if (output_buffer_manager_) {
//                     size_t bytes_to_remove = 4;
//                     size_t old_size = output_buffer_manager_->GetCurrentSize();
                    
//                     bool success = output_buffer_manager_->RemoveData(DataType::OUTPUT, bytes_to_remove);
                    
//                     if (success) {
//                         size_t new_size = output_buffer_manager_->GetCurrentSize();
//                         cout << sc_time_stamp() << ": " << name() 
//                              << " [OUTPUT_TX_COMPLETE] Sent OUTPUT packet: " 
//                              << flit.src_id << "→" << flit.dst_id 
//                              << ". Buffer: " << old_size << "→" << new_size 
//                              << " (-" << bytes_to_remove << ")" << endl;
//                     } else {
//                         cout << sc_time_stamp() << ": " << name() 
//                              << " [OUTPUT_TX_ERROR] Failed to remove data from output buffer!" << endl;
//                     }
//                 }
//             }
            
//             flit_tx[1].write(flit);
//             current_level_tx_2 = 1 - current_level_tx_2;
//             req_tx[1].write(current_level_tx_2);
//         }
//     }
// }

// void ProcessingElement::output_rxProcess() {
//     if (reset.read()) {
//         ack_rx[1].write(0);
//         current_level_rx_2 = 0;
//         return;
//     }

//     if (req_rx[1].read() == 1 - current_level_rx_2) {
//         Flit flit = flit_rx[1].read();
        
//         if (flit.flit_type == FLIT_TYPE_HEAD) {
//             std::copy(std::begin(flit.payload_sizes), std::end(flit.payload_sizes), incoming_output_payload_sizes);
//             int total_size = std::accumulate(incoming_output_payload_sizes, incoming_output_payload_sizes + 3, 0);
            
//             // +++ 增强调试：完整的HEAD信息 +++
//             cout << sc_time_stamp() << ": " << name() 
//                  << " [OUTPUT_RX_HEAD] Receiving OUTPUT HEAD: " 
//                  << flit.src_id << "→" << flit.dst_id 
//                  << ", seq=" << flit.sequence_no << "/" << flit.sequence_length
//                  << ", payload=" << total_size << "B" << endl;
//         }
        
//         if (flit.flit_type == FLIT_TYPE_TAIL) {
//             // +++ 增强调试：完整的TAIL处理信息 +++
//             cout << sc_time_stamp() << ": " << name() 
//                  << " [OUTPUT_RX_TAIL] Received OUTPUT TAIL: " 
//                  << flit.src_id << "→" << flit.dst_id << endl;

//             if (output_buffer_manager_) {
//                 bool success = true;
//                 size_t old_size = output_buffer_manager_->GetCurrentSize();
                
//                 if (incoming_output_payload_sizes[OUTPUT_IDX] > 0) {
//                     success = output_buffer_manager_->OnDataReceived(DataType::OUTPUT, incoming_output_payload_sizes[OUTPUT_IDX]);
//                 }
                
//                 if (success) {
//                     size_t new_size = output_buffer_manager_->GetCurrentSize();
//                     int received_bytes = incoming_output_payload_sizes[OUTPUT_IDX];
                    
//                     cout << sc_time_stamp() << ": " << name() 
//                          << " [OUTPUT_RX_COMPLETE] Processed OUTPUT packet: " 
//                          << flit.src_id << "→" << flit.dst_id 
//                          << ". Buffer: " << old_size << "→" << new_size 
//                          << " (+" << received_bytes << ")" << endl;
//                 } else {
//                     cout << sc_time_stamp() << ": " << name() 
//                          << " [OUTPUT_RX_ERROR] Failed to receive output data - buffer full!" << endl;
//                 }
//             }
//         }
        
//         current_level_rx_2 = 1 - current_level_rx_2;
//     }
    
//     ack_rx[1].write(current_level_rx_2);
// }

#include <vector>
#include <string>
#include <iostream>

// 假设这些头文件已包含，并且 PE 类定义了所有必需的成员变量
// #include "ProcessingElement.h"
// #include "BufferManager.h"
// #include "DataStructs.h"

void ProcessingElement::run_compute_logic() {
    // 防御性检查：如果所有接收/消耗任务都已完成，则直接返回
    if (all_receive_tasks_finished) {
        return;
    }

    // ==========================================================
    // 阶段 1: 处理正在进行的计算
    // ==========================================================
    if (is_consuming) {
        consume_cycles_left--;
        if (consume_cycles_left == 0) {
            is_consuming = false;
            
            // 推进逻辑时间戳，这是与驱逐计划同步的关键
            logical_timestamp++;
            
            // 调用 BufferManager 的核心方法来执行驱逐，
            // 并直接获取本次操作实际消耗/驱逐的字节数。
            // 这是唯一的、权威的真相来源。
            size_t bytes_consumed_this_task = buffer_manager_->OnComputeFinished(logical_timestamp);
            output_buffer_manager_->OnDataReceived(DataType::OUTPUT, 2);
            
            // +++ 通知：缓冲区因为驱逐了数据而状态改变！ +++
            buffer_state_changed_event.notify();
            // 将这个权威的数值，累加到总消耗量中
            total_bytes_consumed += bytes_consumed_this_task;

            // 打印包含丰富上下文的、准确的日志
            cout << sc_time_stamp() << ": " << name() 
                 << " [LIFECYCLE] Computation finished at logical step " << logical_timestamp 
                 << ". Evicted " << bytes_consumed_this_task << " bytes. "
                 << "Buffer size is now: " << buffer_manager_->GetCurrentSize() 
                 << ", Total consumed: " << total_bytes_consumed << endl;
            
            // 更新嵌套循环计数器，为下一次决策做准备
            update_receive_loop_counters();
        }
        // 如果计算仍在进行中 (consume_cycles_left > 0)，则直接返回，等待下一个周期
        return; 
    }

    // ==========================================================
    // 阶段 2: 如果当前空闲，则决策是否开始下一次计算
    // ==========================================================
    
    // 1. 根据嵌套循环状态，定义下一次计算所需的数据类型集合
    int innermost_loop_level = receive_task_queue.size() - 1;
    bool is_first_iteration = (receive_loop_counters[innermost_loop_level] == 0);

    std::vector<DataType> required_data_types;
    string required_type_str;

    if (is_first_iteration) {
        required_type_str = "FILL";
        required_data_types.push_back(DataType::WEIGHT);
        required_data_types.push_back(DataType::INPUT);
    } else {
        required_type_str = "DELTA";
        required_data_types.push_back(DataType::INPUT);
    }

    // 2. 询问 BufferManager，检查所需的所有数据类型是否都已就绪
    if (buffer_manager_->AreDataTypesReady(required_data_types)) {
        // 数据充足，可以开始新的计算任务
        cout << sc_time_stamp() << ": " << name() 
             << " Starting a '" << required_type_str << "' task. "
             << "Required data is ready in buffer." << endl;

        is_consuming = true;
        consume_cycles_left = 6; // 假设有一个代表计算延迟的常量或变量
        
        // 注意：我们不再需要 data_to_consume_on_finish 这个成员变量。
        // 启动逻辑和完成逻辑已经通过 BufferManager 完全解耦。
    }
    else {
        // 数据未就绪，保持空闲状态，等待数据到达。
        // 可以在这里添加调试日志来观察"数据饥饿"状态。
        // cout << sc_time_stamp() << ": " << name() << " Data starved for '" 
        //      << required_type_str << "' task. Waiting." << endl;
    }
}

void ProcessingElement::update_receive_loop_counters() {
    if (all_receive_tasks_finished) {
        return;
    }
    // 逻辑与 update_transfer_loop_counters 完全一样，但操作的是receive任务队列
    int level_to_update = receive_task_queue.size() - 1;
    // 1. 增加最内层循环的计数器
    receive_loop_counters[level_to_update]++;

    // 2. 处理"进位"：从内向外检查每个循环是否完成
    while (receive_loop_counters[level_to_update] >= receive_task_queue[level_to_update]) {
        
        // 当前循环已完成，重置计数器
        receive_loop_counters[level_to_update] = 0;

        // 移动到上一层循环
        level_to_update--;

        // 如果处理完了最外层循环，说明所有任务都已结束
        if (level_to_update < 0) {
            all_receive_tasks_finished = true;
            cout << sc_time_stamp() << ": " << name() 
                 << " All receive/consume tasks finished." << endl;
            cout << sc_time_stamp() << ": " << name() << " FINAL: Total bytes consumed: " << total_bytes_consumed << endl;
            cout << sc_time_stamp() << ": " << name() << " FINAL: Total bytes received: " << total_bytes_received << endl;
            return;
        }
        
        // 否则，将上一层的计数器加 1
        receive_loop_counters[level_to_update]++;
    }
}

std::string ProcessingElement::role_to_str(const PE_Role& role) {
    switch (role) {
        case ROLE_DRAM: return "DRAM";
        case ROLE_GLB: return "GLB";
        case ROLE_BUFFER: return "BUFFER";
        default: return "UNKNOWN";
    }
}
// in PE.cpp

void ProcessingElement::run_storage_logic() {
    // --- 防御性检查 ---
    // 如果所有传输任务都已完成，则无需再生成Packet
    if (all_transfer_tasks_finished) {
        return;
    }

    // --- 步骤 1: 确定当前发送意图 ---
    // 根据当前循环的迭代次数判断是应该发送FILL还是DELTA
    // 假设：每个循环的第一次迭代 (counter == 0) 发送FILL
    bool is_first_iteration = (transfer_loop_counters[current_transfer_loop_level] == 0);
    
    int required_capability;
    std::vector<DataType> required_data_types;
    int payload_sizes[3] = {0, 0, 0}; // 用于填充Packet的元数据
    string intent_str; // 用于日志
    if(role==ROLE_DRAM)
    {
        intent_str = "FILL";
        required_data_types.push_back(DataType::WEIGHT);
        required_data_types.push_back(DataType::INPUT);
        // 这是DRAM唯一一次生成数据包
        payload_sizes[WEIGHT_IDX] = 96;
        payload_sizes[INPUT_IDX] = 18;

    }
    else if(role == ROLE_GLB) {
        if (is_first_iteration) {
            // 意图：发送一个FILL包
            intent_str = "FILL";
            // 定义FILL的需求：需要WEIGHT和INPUT
            required_data_types.push_back(DataType::WEIGHT);
            required_data_types.push_back(DataType::INPUT);
            // 定义FILL的元数据
            payload_sizes[WEIGHT_IDX] = 6; // 来自pe_init的定义
            payload_sizes[INPUT_IDX] = 3;  // 来自pe_init的定义
        } else {
            // 意图：发送一个DELTA包
            intent_str = "DELTA";
            required_data_types.push_back(DataType::INPUT);
            payload_sizes[INPUT_IDX] = 1; // 来自pe_init的定义
            
        }
    }
    // ==========================================================
    // +++ 步骤 2: 核心决策：检查数据是否就绪 +++
    // ==========================================================
    // 检查这个PE是否有缓冲区，并且缓冲区中的数据是否满足我们的需求
    bool data_ready = false;
    if (role == ROLE_BUFFER && intent_str == "OUTPUT") {
        // 对于OUTPUT数据，检查output_buffer_manager_
        data_ready = output_buffer_manager_ && output_buffer_manager_->AreDataTypesReady(required_data_types);
    } else {
        // 对于其他数据，检查buffer_manager_
        data_ready = buffer_manager_ && buffer_manager_->AreDataTypesReady(required_data_types);
    }
    
    if (data_ready) {
        
        // 数据就绪！现在才需要检查下游能力，因为没数据时检查下游是无意义的
        int required_capability = (intent_str == "FILL") ? 2 : 1;
        int downstream_capability = downstream_ready_in[0]->read();

        if (downstream_capability >= required_capability) {
            // 所有条件都满足！可以生成并发送Packet
            cout << sc_time_stamp() << ": " << name() 
                 << " run_storage_logic() - MATCH: Data ready for " << intent_str 
                 << " and downstream capability is sufficient (" << downstream_capability 
                 << "). Generating packet." << endl;

            Packet pkt;
            pkt.src_id = local_id;
            pkt.dst_id = downstream_node_ids[0];
            // ... (其他pkt字段的填充逻辑，如timestamp, vc_id等保持不变) ...

            // +++ 为Packet填充精确的元数据 +++
            std::copy(std::begin(payload_sizes), std::end(payload_sizes), pkt.payload_sizes);
            // pkt的总大小现在可以从元数据中计算
            int total_payload_size = pkt.total_size(); // 假设Packet有total_size()方法
            pkt.payload_data_size = total_payload_size; // 兼容旧字段
            
        // 注意: 您需要根据您的任务逻辑来确定目标ID (dst_id)
        // 这里只是一个示例，假设发送给相邻的PE
        pkt.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
        int bytes_per_flit = GlobalParams::flit_size / 8; // 每个Flit的字节数
        // 1. 先计算承载payload需要多少flit
        int payload_flits = (total_payload_size + bytes_per_flit - 1) / bytes_per_flit;
        if (total_payload_size == 0) payload_flits = 0; // 处理0字节payload的特殊情况

        // 2. 根据payload_flits决定总的flit数
        int num_flits;
        if (payload_flits == 0) {
        // 纯信令包，没有数据。强制为 HEAD + TAIL
        num_flits = 2; 
        } else if (payload_flits == 1) {
        // 数据可以放在一个flit里。强制为 HEAD + BODY (含数据) + TAIL
        num_flits = 3; 
        } else {
            // 数据需要多个flit。
            // HEAD + (payload_flits个BODY) + TAIL
            num_flits = 1 + payload_flits + 1;
        }
        pkt.flit_left = pkt.size= num_flits; // 设置包的总Flit数
        pkt.payload_data_size = total_payload_size; // 携带真实的字节大小！
        pkt.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
        // 将Packet放入发送队列
        packet_queue.push(pkt);
        update_transfer_loop_counters();
        }
    }
}

void ProcessingElement::update_transfer_loop_counters() {
    // 如果所有任务已完成，直接返回
    if (all_transfer_tasks_finished) {
        return;
    }

    // 嵌套循环的更新逻辑必须从最内层开始
    int level_to_update = transfer_task_queue.size() - 1;

    // 1. 增加最内层循环的计数器
    transfer_loop_counters[level_to_update]++;

    // 2. 处理"进位"：从内向外检查每个循环是否完成
    // 使用 while 循环可以处理多个循环同时完成的情况 (例如，内外循环都只有1次迭代)
    while (transfer_loop_counters[level_to_update] >= transfer_task_queue[level_to_update].iterations) {
        
        // 当前循环已完成，将其计数器重置为 0
        transfer_loop_counters[level_to_update] = 0;

        // 移动到上一层（更外层的）循环
        level_to_update--;

        // 如果我们已经处理完了最外层循环 (level < 0)，说明所有任务都已结束
        if (level_to_update < 0) {
            all_transfer_tasks_finished = true;
            cout << sc_time_stamp() << ": " << name() 
                 << " All transfer tasks finished. No more packets will be generated." << endl;
             cout << sc_time_stamp() << ": " << name() << " FINAL: Total bytes sent: " << total_bytes_sent << endl;
            return; // 任务完成，退出函数
        }
        
        // 否则，将上一层（现在是当前层）的计数器加 1
        transfer_loop_counters[level_to_update]++;
    }
}

Flit ProcessingElement::generate_next_flit_from_queue(std::queue<Packet>& queue, bool is_output)
{
    Flit flit;
    Packet packet = queue.front();

    // 填充公共字段
    flit.src_id = packet.src_id;
    flit.dst_id = packet.dst_id;
    flit.vc_id = packet.vc_id;
    flit.timestamp = packet.timestamp;
    flit.sequence_no = packet.size - packet.flit_left;
    flit.sequence_length = packet.size;
    flit.hop_no = 0;
    flit.payload_data_size = packet.payload_data_size;
    flit.is_output = is_output;
    flit.hub_relay_node = NOT_VALID;

    // 确定flit类型
    if (packet.size == packet.flit_left) {
        flit.flit_type = FLIT_TYPE_HEAD;
        std::copy(std::begin(packet.payload_sizes), std::end(packet.payload_sizes), flit.payload_sizes);
    } else if (packet.flit_left == 1) {
        flit.flit_type = FLIT_TYPE_TAIL;
    } else {
        flit.flit_type = FLIT_TYPE_BODY;
    }

    // 处理flit_left和队列pop
    queue.front().flit_left--;
    if (queue.front().flit_left == 0) {
        queue.pop();
    }

    return flit;
}

Flit ProcessingElement::nextFlit()
{
    return generate_next_flit_from_queue(packet_queue, false);
}

Flit ProcessingElement::nextOutputFlit()
{
    return generate_next_flit_from_queue(packet_queue_2, true);
}
unsigned int ProcessingElement::getQueueSize() const
{
    return packet_queue.size();
}