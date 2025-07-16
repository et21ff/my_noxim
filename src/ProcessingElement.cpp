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
int ProcessingElement::randInt(int min, int max)
{
    return min +
	(int) ((double) (max - min + 1) * rand() / (RAND_MAX + 1.0));
}

void ProcessingElement::pe_init() {
    //========================================================================
    // I. 黄金参数定义 (Golden Parameters)
    //    - 源自Timeloop的逻辑分析和Accelergy的物理定义
    //========================================================================
    
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
        current_data_size.write(10000);
        max_capacity = -1;
        downstream_node_ids.push_back(1); // 下游是GLB
        
        // DRAM的发送任务: 响应GLB的一次性总加载请求
        // 假设GLB一次性请求所有数据 (W:96 + I:18 = 114 Bytes)
        transfer_task_queue.clear();
        transfer_task_queue.push_back({1});
        transfer_loop_counters.resize(1, 0);
        current_transfer_loop_level = 0;
        
        // DRAM的发送块大小
        transfer_fill_size = 96 + 18*16; // 一次性发送所有需要的数据
        
    } else if (local_id == 1) {
        // --- ROLE_GLB: 智能的、任务驱动的中间商 ---
        current_data_size.write(0); // 初始为空
        role = ROLE_GLB;
        max_capacity = GLB_CAPACITY;
        downstream_node_ids.push_back(2); // 下游是Buffer
        
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

    } else if (local_id == 2) {
        // --- ROLE_BUFFER: 带抽象消耗的最终目的地 ---
        current_data_size.write(0); // 初始为空
        role = ROLE_BUFFER;
        max_capacity = BUFFER_CAPACITY;
        downstream_node_ids.clear(); // 是终点
        
        receive_task_queue.clear();
        receive_task_queue.push_back(K2_LOOPS);
        receive_task_queue.push_back(P1_LOOPS);
        receive_loop_counters.resize(2, 0);
        current_receive_loop_level = 1;

        // Buffer的消耗需求 (用于驱动抽象消耗和ready信号)
        required_data_on_fill = FILL_DATA_SIZE;
        required_data_on_delta = DELTA_DATA_SIZE;
        
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
        // 如果需要调试 reset 分支
            dbg(sc_time_stamp(), name(), "RESET active. Writing FALSE");
        return;
    }
    int ready_value = 0;
    int available_space = max_capacity - current_data_size.read();

    // ** 2. 根据剩余空间，分级判断接收能力 **
    if (available_space >= required_data_on_fill) {
             // 空间巨大，可以接收一个完整的FILL块
            ready_value = 2;
        } else if (available_space >= required_data_on_delta) {
             // 空间不够接收FILL，但足够接收一个DELTA块
            ready_value = 1;
            }
     downstream_ready_out.write(ready_value);

    // ** 4. [调试日志] 只在信号值发生变化时打印，避免日志风暴 **
    if (ready_value != previous_ready_signal) {
        if (role == ROLE_GLB || role == ROLE_BUFFER) { // 只打印我们关心的PE
             cout << sc_time_stamp() << ": " << name() << " [READY_SIG] Updating ready_out from " 
                  << previous_ready_signal << " to " << ready_value 
                  << " (AvailableSpace: " << (max_capacity - current_data_size.read()) << ")" << endl;
        }
        previous_ready_signal = ready_value;
    }


    
}

void ProcessingElement::rxProcess() {
    // 步骤 0: 处理Reset信号 (保持不变)
    if (reset.read()) {
        ack_rx.write(0);
        current_level_rx = 0;
        // 在reset时，也应该清理其他与接收相关的状态
        return;
    }

    // 步骤 1: 遵循Noxim的握手协议 (核心结构保持不变)
    if (req_rx.read() == 1 - current_level_rx) {
        // 读取flit，这是我们处理的输入
        Flit flit = flit_rx.read();
        // ------------------- 我们注入的新逻辑 [开始] -------------------
        if (flit.flit_type == FLIT_TYPE_HEAD) {
            incoming_packet_size = flit.payload_data_size; // 记录接收包的大小
            cout << sc_time_stamp() << ": " << name() << " RX <<< HEAD. Real data size: " 
                 << incoming_packet_size << " bytes. Network flits: " << flit.sequence_length << endl;
            
            is_receiving_packet = true; // 包开始了，进入“接收中”状态
            update_ready_signal();

        }
        // 步骤 2: 只在接收到包尾(TAIL)时，才触发我们的上层逻辑
        if (flit.flit_type == FLIT_TYPE_TAIL) {
            int receive_size = incoming_packet_size;
            is_receiving_packet = false; // 包结束了，退出“接收中”状态
            update_ready_signal();
            cout << sc_time_stamp() << ": " << name() << " RX <<< TAIL from " 
                 << flit.src_id << ", packet size: " << flit.sequence_length << endl;

            // 步骤 3: 根据角色执行不同的操作
            switch (role) {
                case ROLE_GLB: {
                    // SPAD接收到来自上游(GLB)的数据
                    current_data_size.write(receive_size+current_data_size.read()); // 假设flit.size就是我们约定的块大小

                    assert(current_data_size <= max_capacity); 
                    cout << sc_time_stamp() << ": SPAD[" << local_id << "] RX data. New size: " 
                         << current_data_size << "/" << max_capacity << endl;
                    break;
                }

                case ROLE_BUFFER: {
                    // ComputePE接收到来自SPAD的数据
                    current_data_size.write(receive_size+current_data_size.read());
                    total_bytes_received+=receive_size;
                    assert(current_data_size <= max_capacity); 

                    cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] RX data. New size: " 
                         << current_data_size << "/" << max_capacity<<endl;

                    // ** 事件响应: 检查是否可以解除停机状态 **
                    int required_data_per_compute = (current_stage == STAGE_FILL) ? required_data_on_fill : required_data_on_delta;

                    if (is_stalled_waiting_for_data && current_data_size >= required_data_per_compute) {
                        is_stalled_waiting_for_data = false;
                        cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] Data arrived, UNSTALLING." << endl;
                    }
                    break;
                }
                
                case ROLE_DRAM: {
                    // GLB 在我们的模型中是顶级生产者，它只发送不接收。
                    // 但为了完整性，可以加个日志。
                    cout << sc_time_stamp() << ": WARNING - GLB[" << local_id << "] received a packet unexpectedly." << endl;
                    break;
                }
            }
        }
        // ------------------- 我们注入的新逻辑 [结束] -------------------


        // 步骤 4: 翻转握手信号 (核心结构保持不变)
        current_level_rx = 1 - current_level_rx;
    }

    // 步骤 5: 将新的ack level写回端口 (核心结构保持不变)
    ack_rx.write(current_level_rx);
}

// in PE.cpp

void ProcessingElement::txProcess() {
    // 复位逻辑保持不变
    if (reset.read()) {
        req_tx.write(0);
        current_level_tx = 0;
        while (!packet_queue.empty()) packet_queue.pop();
        return;
    }

    // --- 步骤 A: 如果需要，智能地生成Packet ---
    // 只有在队列为空时，才尝试生成新的Packet
    if (packet_queue.empty()) {
        // 根据角色调用生成逻辑。
        // run_storage_logic 内部已经包含了所有“意图”和“能力”的匹配检查。
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
    if (!packet_queue.empty()) {
        
        // 1. "窥探"队首的Packet，以确定其发送要求
        const Packet& pkt_to_send = packet_queue.front();

        // 2. 根据Packet的真实大小，推断其发送要求
        int required_capability;
        int payload_data_size = pkt_to_send.payload_data_size;
        string packet_type_str; // 用于日志

        if (payload_data_size == transfer_fill_size) {
            required_capability = 2;
            packet_type_str = "FILL";
        } else { // 假设不是FILL就是DELTA
            required_capability = 1;
            packet_type_str = "DELTA";
        }

        // 3. 读取下游 *当前* 的接收能力
        int downstream_capability = downstream_ready_in.read();

        // 4. 终极守门员检查：检查ack，并进行精确的能力匹配
        if (ack_tx.read() == current_level_tx && downstream_capability >= required_capability) {
            // 所有检查通过！可以安全地发送Flit
            Flit flit = nextFlit(); // nextFlit() 会在发送TAIL后从队列中pop

            if (flit.flit_type == FLIT_TYPE_HEAD) {
                cout << sc_time_stamp() << ": " << name() << " txProcess() - GO: Sending HEAD of " 
                     << packet_type_str << " packet to " << flit.dst_id << endl;
            }
            else if(flit.flit_type == FLIT_TYPE_TAIL)
            {
                current_data_size.write(current_data_size.read() - payload_data_size);
                total_bytes_sent += payload_data_size;

            }
            // 真正的硬件行为：通过端口发送flit
            flit_tx.write(flit);

            // 更新Noxim的握手协议状态
            current_level_tx = 1 - current_level_tx;
            req_tx.write(current_level_tx);

        } 
    }
}
// in PE.cpp

// 为了在计算完成后正确消耗数据，我们需要一个新的成员变量来“记住”要消耗多少
// 请在 PE.h 中添加: int data_to_consume_on_finish;

void ProcessingElement::run_compute_logic() {
    // 如果所有接收/消耗任务都已完成，则直接返回
    if (all_receive_tasks_finished) {
        return;
    }

// --- 阶段 1: 处理正在进行的计算 ---
if (is_consuming) {
    consume_cycles_left--;
    if (consume_cycles_left == 0) {
        is_consuming = false;
        
        // ==========================================================
        //         ** 这是唯一的、正确的消耗逻辑 **
        // ==========================================================
        
        // 1. 从缓冲区消耗“之前已暂存”的数据量。
        //    data_to_consume_on_finish 是在上一次启动计算时，
        //    根据当时的循环状态，被精确设置好的唯一真相。
        current_data_size.write(current_data_size.read() - data_to_consume_on_finish);
        
        // 2. 将同样精确的数值，累加到总消耗量中。
        total_bytes_consumed += data_to_consume_on_finish;

        // 3. 打印清晰的日志
        //    为了知道这次消耗的是FILL还是DELTA，我们可以比较暂存值。
        string task_type = (data_to_consume_on_finish == required_data_on_fill) ? "FILL" : "DELTA";
        cout << sc_time_stamp() << ": COMPUTE[" << local_id 
             << "] Finished consuming a '" << task_type << "' task (" << data_to_consume_on_finish << " bytes). "
             << "Buffer now has " << current_data_size.read() << " bytes." << endl;
        
        // 4. 更新嵌套循环计数器，为下一次消耗做准备。
        update_receive_loop_counters();
    }
    return; // 正在计算中，本周期无需做其他决策
}

    // --- 阶段 2: 如果当前空闲，则决策是否开始下一次计算 ---
    
    // 1. 根据嵌套循环状态，确定下一次期望消耗的数据类型和大小
    // 关键：我们只关心最内层循环的迭代次数
    int innermost_loop_level = receive_task_queue.size() - 1;
    bool is_first_iteration_of_innermost_loop = (receive_loop_counters[innermost_loop_level] == 0);

    int required_data_for_next_compute;
    string required_type_str;

    if (is_first_iteration_of_innermost_loop) {
        // 最内层循环的第一次迭代，需要一个FILL块
        required_data_for_next_compute = required_data_on_fill;
        required_type_str = "FILL";
    } else {
        // 后续迭代，需要一个DELTA块
        required_data_for_next_compute = required_data_on_delta;
        required_type_str = "DELTA";
    }

    // 2. 检查缓冲区中是否有足够的数据来执行这次计算
    if (current_data_size.read() >= required_data_for_next_compute) {
        // 数据充足，可以开始计算！
        cout << sc_time_stamp() << ": COMPUTE[" << local_id 
             << "] Starting a '" << required_type_str << "' task. "
             << "Requires " << required_data_for_next_compute << " bytes, has " << current_data_size.read() << "." << endl;

        is_consuming = true;
        consume_cycles_left = 6; // 假设有一个计算周期的变量
        
        // **重要**: 暂存下本次将要消耗的数据量
        data_to_consume_on_finish = required_data_for_next_compute;
    }
    else {
        // 数据不足，等待。
        // 可以添加日志来观察“数据饥饿”状态
        // cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] Data starved. Needs " 
        //      << required_data_for_next_compute << " bytes, but only has " 
        //      << current_data_size.read() << ". Waiting." << endl;
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

    // 2. 处理“进位”：从内向外检查每个循环是否完成
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
    int payload_size_to_send;
    string intent_str; // 用于日志

    if (is_first_iteration) {
        // 意图：发送一个FILL包
        intent_str = "FILL";
        required_capability = 2; // 发送FILL，要求下游能力必须为2
        payload_size_to_send = transfer_fill_size;
    } else {
        // 意图：发送一个DELTA包
        intent_str = "DELTA";
        required_capability = 1; // 发送DELTA，要求下游能力至少为1
        payload_size_to_send = transfer_delta_size;
    }

    // --- 步骤 2: 读取下游的接收能力 ---
    int downstream_capability = downstream_ready_in.read();

    // --- 步骤 3: 核心决策：当且仅当“意图”与“能力”匹配时，才生成Packet ---
    if (downstream_capability >= required_capability&& current_data_size.read() >= payload_size_to_send) {
        // 匹配成功！可以生成并发送Packet
        cout << sc_time_stamp() << ": " << name() 
             << " run_storage_logic() - MATCH: Intent to send " << intent_str 
             << ", Downstream capability is " << downstream_capability 
             << ". Generating packet." << endl;

        // 创建Packet
        Packet pkt;
        pkt.src_id = local_id;
        // 注意: 您需要根据您的任务逻辑来确定目标ID (dst_id)
        // 这里只是一个示例，假设发送给相邻的PE
        pkt.dst_id = downstream_node_ids[0]; // 请确保 'target_id' 是一个有效的成员变量或已定义
        pkt.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
        int bytes_per_flit = GlobalParams::flit_size / 8; // 每个Flit的字节数
        // 1. 先计算承载payload需要多少flit
    int payload_flits = (payload_size_to_send + bytes_per_flit - 1) / bytes_per_flit;
    if (payload_size_to_send == 0) payload_flits = 0; // 处理0字节payload的特殊情况

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
        pkt.payload_data_size = payload_size_to_send; // 携带真实的字节大小！
        pkt.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
        // 将Packet放入发送队列
        packet_queue.push(pkt);

        // **重要**: 生成Packet后，必须更新循环计数器状态，为下一次决策做准备
        // 您需要调用您实现的循环更新函数，例如:
        update_transfer_loop_counters(); 
    }
}

// void ProcessingElement::run_storage_logic()
// {
//     // 检查是否有足够的数据来生产一个包
//     if (current_data_size.read() >= transfer_chunk_size&& !ended_compute_loop) {
//         // ---- 这是两个函数完全相同的核心逻辑 ----
//         int bytes_per_flit = GlobalParams::flit_size / 8;
//         const int num_flits = (transfer_chunk_size + bytes_per_flit - 1) / bytes_per_flit;

//         Packet pkt;
//         pkt.src_id = local_id;
//         pkt.dst_id = downstream_node_ids[0]; // 假设只有一个下游
//         pkt.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
//         pkt.size = pkt.flit_left = num_flits;
//         pkt.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
//         pkt.payload_data_size = transfer_chunk_size; //单位是字节
//         packet_queue.push(pkt);

//         // 使用传入的参数来打印定制化的日志
//         PE_Role next_role = static_cast<PE_Role>(role + 1);
//         cout << sc_time_stamp() << ": " << role_to_str(role) << "[" << local_id 
//              << "] INTENDS to push a packet to queue for " << role_to_str(next_role)<< "[" 
//              << pkt.dst_id << "]." << endl;
//     }

// }
// in PE.cpp

void ProcessingElement::update_transfer_loop_counters() {
    // 如果所有任务已完成，直接返回
    if (all_transfer_tasks_finished) {
        return;
    }

    // 嵌套循环的更新逻辑必须从最内层开始
    int level_to_update = transfer_task_queue.size() - 1;

    // 1. 增加最内层循环的计数器
    transfer_loop_counters[level_to_update]++;

    // 2. 处理“进位”：从内向外检查每个循环是否完成
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

Flit ProcessingElement::nextFlit()
{
    Flit flit;
    Packet packet = packet_queue.front();

    flit.src_id = packet.src_id;
    flit.dst_id = packet.dst_id;
    flit.vc_id = packet.vc_id;
    flit.timestamp = packet.timestamp;
    flit.sequence_no = packet.size - packet.flit_left;
    flit.sequence_length = packet.size;
    flit.hop_no = 0;
    flit.payload_data_size = packet.payload_data_size;
    //  flit.payload     = DEFAULT_PAYLOAD;

    flit.hub_relay_node = NOT_VALID;

    if (packet.size == packet.flit_left)
	flit.flit_type = FLIT_TYPE_HEAD;
    else if (packet.flit_left == 1)
	flit.flit_type = FLIT_TYPE_TAIL;
    else
	flit.flit_type = FLIT_TYPE_BODY;

    packet_queue.front().flit_left--;
    if (packet_queue.front().flit_left == 0)
	packet_queue.pop();

    return flit;
}

bool ProcessingElement::canShot(Packet & packet)
{
   // assert(false);
    if(never_transmit) return false;
   
    //if(local_id!=16) return false;
    /* DEADLOCK TEST 
	double current_time = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;

	if (current_time >= 4100) 
	{
	    //if (current_time==3500)
	         //cout << name() << " IN CODA " << packet_queue.size() << endl;
	    return false;
	}
	//*/

#ifdef DEADLOCK_AVOIDANCE
    if (local_id%2==0)
	return false;
#endif
    bool shot;
    double threshold;

    double now = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;

    if (GlobalParams::traffic_distribution != TRAFFIC_TABLE_BASED) {
	if (!transmittedAtPreviousCycle)
	    threshold = GlobalParams::packet_injection_rate;
	else
	    threshold = GlobalParams::probability_of_retransmission;

	shot = (((double) rand()) / RAND_MAX < threshold);
	if (shot) {
	    if (GlobalParams::traffic_distribution == TRAFFIC_RANDOM)
		    packet = trafficRandom();
        else if (GlobalParams::traffic_distribution == TRAFFIC_TRANSPOSE1)
		    packet = trafficTranspose1();
        else if (GlobalParams::traffic_distribution == TRAFFIC_TRANSPOSE2)
    		packet = trafficTranspose2();
        else if (GlobalParams::traffic_distribution == TRAFFIC_BIT_REVERSAL)
		    packet = trafficBitReversal();
        else if (GlobalParams::traffic_distribution == TRAFFIC_SHUFFLE)
		    packet = trafficShuffle();
        else if (GlobalParams::traffic_distribution == TRAFFIC_BUTTERFLY)
		    packet = trafficButterfly();
        else if (GlobalParams::traffic_distribution == TRAFFIC_LOCAL)
		    packet = trafficLocal();
        else if (GlobalParams::traffic_distribution == TRAFFIC_ULOCAL)
		    packet = trafficULocal();
        else {
            cout << "Invalid traffic distribution: " << GlobalParams::traffic_distribution << endl;
            exit(-1);
        }
	}
    } else {			// Table based communication traffic
	if (never_transmit)
	    return false;

	bool use_pir = (transmittedAtPreviousCycle == false);
	vector < pair < int, double > > dst_prob;
	double threshold =
	    traffic_table->getCumulativePirPor(local_id, (int) now, use_pir, dst_prob);

	double prob = (double) rand() / RAND_MAX;
	shot = (prob < threshold);
	if (shot) {
	    for (unsigned int i = 0; i < dst_prob.size(); i++) {
		if (prob < dst_prob[i].second) {
                    int vc = randInt(0,GlobalParams::n_virtual_channels-1);
		    packet.make(local_id, dst_prob[i].first, vc, now, getRandomSize());
		    break;
		}
	    }
	}
    }

    return shot;
}


Packet ProcessingElement::trafficLocal()
{
    Packet p;
    p.src_id = local_id;
    double rnd = rand() / (double) RAND_MAX;

    vector<int> dst_set;

    int max_id = (GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y);

    for (int i=0;i<max_id;i++)
    {
	if (rnd<=GlobalParams::locality)
	{
	    if (local_id!=i && sameRadioHub(local_id,i))
		dst_set.push_back(i);
	}
	else
	    if (!sameRadioHub(local_id,i))
		dst_set.push_back(i);
    }


    int i_rnd = rand()%dst_set.size();

    p.dst_id = dst_set[i_rnd];
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();
    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    
    return p;
}


int ProcessingElement::findRandomDestination(int id, int hops)
{
    assert(GlobalParams::topology == TOPOLOGY_MESH);

    int inc_y = rand()%2?-1:1;
    int inc_x = rand()%2?-1:1;
    
    Coord current =  id2Coord(id);
    


    for (int h = 0; h<hops; h++)
    {

	if (current.x==0)
	    if (inc_x<0) inc_x=0;

	if (current.x== GlobalParams::mesh_dim_x-1)
	    if (inc_x>0) inc_x=0;

	if (current.y==0)
	    if (inc_y<0) inc_y=0;

	if (current.y==GlobalParams::mesh_dim_y-1)
	    if (inc_y>0) inc_y=0;

	if (rand()%2)
	    current.x +=inc_x;
	else
	    current.y +=inc_y;
    }
    return coord2Id(current);
}


int roulette()
{
    int slices = GlobalParams::mesh_dim_x + GlobalParams::mesh_dim_y -2;


    double r = rand()/(double)RAND_MAX;


    for (int i=1;i<=slices;i++)
    {
	if (r< (1-1/double(2<<i)))
	{
	    return i;
	}
    }
    assert(false);
    return 1;
}


Packet ProcessingElement::trafficULocal()
{
    Packet p;
    p.src_id = local_id;

    int target_hops = roulette();

    p.dst_id = findRandomDestination(local_id,target_hops);

    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();
    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);

    return p;
}

Packet ProcessingElement::trafficRandom()
{
    Packet p;
    p.src_id = local_id;
    double rnd = rand() / (double) RAND_MAX;
    double range_start = 0.0;
    int max_id;

    if (GlobalParams::topology == TOPOLOGY_MESH)
	max_id = (GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y) - 1; //Mesh 
    else    // other delta topologies
	max_id = GlobalParams::n_delta_tiles-1; 

    // Random destination distribution
    do {
	p.dst_id = randInt(0, max_id);

	// check for hotspot destination
	for (size_t i = 0; i < GlobalParams::hotspots.size(); i++) {

	    if (rnd >= range_start && rnd < range_start + GlobalParams::hotspots[i].second) {
		if (local_id != GlobalParams::hotspots[i].first ) {
		    p.dst_id = GlobalParams::hotspots[i].first;
		}
		break;
	    } else
		range_start += GlobalParams::hotspots[i].second;	// try next
	}
#ifdef DEADLOCK_AVOIDANCE
	assert((GlobalParams::topology == TOPOLOGY_MESH));
	if (p.dst_id%2!=0)
	{
	    p.dst_id = (p.dst_id+1)%256;
	}
#endif

    } while (p.dst_id == p.src_id);

    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();
    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);

    return p;
}
// TODO: for testing only
Packet ProcessingElement::trafficTest()
{
    Packet p;
    p.src_id = local_id;
    p.dst_id = 10;

    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();
    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);

    return p;
}

Packet ProcessingElement::trafficTranspose1()
{
    assert(GlobalParams::topology == TOPOLOGY_MESH);
    Packet p;
    p.src_id = local_id;
    Coord src, dst;

    // Transpose 1 destination distribution
    src.x = id2Coord(p.src_id).x;
    src.y = id2Coord(p.src_id).y;
    dst.x = GlobalParams::mesh_dim_x - 1 - src.y;
    dst.y = GlobalParams::mesh_dim_y - 1 - src.x;
    fixRanges(src, dst);
    p.dst_id = coord2Id(dst);

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

Packet ProcessingElement::trafficTranspose2()
{
    assert(GlobalParams::topology == TOPOLOGY_MESH);
    Packet p;
    p.src_id = local_id;
    Coord src, dst;

    // Transpose 2 destination distribution
    src.x = id2Coord(p.src_id).x;
    src.y = id2Coord(p.src_id).y;
    dst.x = src.y;
    dst.y = src.x;
    fixRanges(src, dst);
    p.dst_id = coord2Id(dst);

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

void ProcessingElement::setBit(int &x, int w, int v)
{
    int mask = 1 << w;

    if (v == 1)
	x = x | mask;
    else if (v == 0)
	x = x & ~mask;
    else
	assert(false);
}

int ProcessingElement::getBit(int x, int w)
{
    return (x >> w) & 1;
}

inline double ProcessingElement::log2ceil(double x)
{
    return ceil(log(x) / log(2.0));
}

Packet ProcessingElement::trafficBitReversal()
{

    int nbits =
	(int)
	log2ceil((double)
		 (GlobalParams::mesh_dim_x *
		  GlobalParams::mesh_dim_y));
    int dnode = 0;
    for (int i = 0; i < nbits; i++)
	setBit(dnode, i, getBit(local_id, nbits - i - 1));

    Packet p;
    p.src_id = local_id;
    p.dst_id = dnode;

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

Packet ProcessingElement::trafficShuffle()
{

    int nbits =
	(int)
	log2ceil((double)
		 (GlobalParams::mesh_dim_x *
		  GlobalParams::mesh_dim_y));
    int dnode = 0;
    for (int i = 0; i < nbits - 1; i++)
	setBit(dnode, i + 1, getBit(local_id, i));
    setBit(dnode, 0, getBit(local_id, nbits - 1));

    Packet p;
    p.src_id = local_id;
    p.dst_id = dnode;

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

Packet ProcessingElement::trafficButterfly()
{

    int nbits = (int) log2ceil((double)
		 (GlobalParams::mesh_dim_x *
		  GlobalParams::mesh_dim_y));
    int dnode = 0;
    for (int i = 1; i < nbits - 1; i++)
	setBit(dnode, i, getBit(local_id, i));
    setBit(dnode, 0, getBit(local_id, nbits - 1));
    setBit(dnode, nbits - 1, getBit(local_id, 0));

    Packet p;
    p.src_id = local_id;
    p.dst_id = dnode;

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

void ProcessingElement::fixRanges(const Coord src,
				       Coord & dst)
{
    // Fix ranges
    if (dst.x < 0)
	dst.x = 0;
    if (dst.y < 0)
	dst.y = 0;
    if (dst.x >= GlobalParams::mesh_dim_x)
	dst.x = GlobalParams::mesh_dim_x - 1;
    if (dst.y >= GlobalParams::mesh_dim_y)
	dst.y = GlobalParams::mesh_dim_y - 1;
}

int ProcessingElement::getRandomSize()
{
    return randInt(GlobalParams::min_packet_size,
		   GlobalParams::max_packet_size);
}

unsigned int ProcessingElement::getQueueSize() const
{
    return packet_queue.size();
}

