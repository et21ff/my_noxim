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

// 新增：动态配置函数实现
void ProcessingElement::configure(int id, int level_idx, const HierarchicalConfig& topology_config) {
    //========================================================================
    // I. 保存基础信息
    //========================================================================
    this->local_id = id;
    this->level_index = level_idx;

    //========================================================================
    // II. 根据层级确定角色和基础配置
    //========================================================================
    assert(level_idx < topology_config.levels.size());
    const LevelConfig& level_config = topology_config.get_level_config(level_idx);

    // 设置角色（假设每层只有一个主要角色）
    this->role = level_config.roles;

    // 设置缓冲区容量
    this->max_capacity = level_config.buffer_size;

    //========================================================================
    // III. 配置上游/下游连接
    //========================================================================
    // 清空现有连接
    this->upstream_node_ids.clear();
    this->downstream_node_ids.clear();

    // 配置上游
    int parent_id = GlobalParams::parent_map[this->local_id];
    if (parent_id != -1) {
        this->upstream_node_ids.push_back(parent_id);
    }

    // 配置下游
    int num_children = GlobalParams::fanouts_per_level[level_idx];
    int* children = GlobalParams::child_map[this->local_id];
    for (int i = 0; i < num_children; i++) {
        this->downstream_node_ids.push_back(children[i]);
    }

    //========================================================================
    // IV. 执行角色专属的初始化
    //========================================================================
    switch (this->role) {
        case ROLE_DRAM: {
            // 黄金参数定义
            const int GLB_CAPACITY = 262144;
            const int BUFFER_CAPACITY = 64;

            // 配置BufferManager
            EvictionSchedule dram_schedule = ScheduleFactory::createDRAMEvictionSchedule();
            buffer_manager_.reset(new BufferManager(max_capacity, dram_schedule));
            // buffer_manager_->OnDataReceived(DataType::WEIGHT, 96);
            // buffer_manager_->OnDataReceived(DataType::INPUT, 18);

            // 配置output buffer manager
            EvictionSchedule dram_output_schedule = ScheduleFactory::createDRAMEvictionSchedule();
            output_buffer_manager_.reset(new BufferManager(max_capacity, dram_output_schedule));
            // output_buffer_manager_->OnDataReceived(DataType::OUTPUT, 512);

            // 配置TaskManager
            task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
            task_manager_->Configure(GlobalParams::workload, "ROLE_DRAM");
            auto dataset = task_manager_->get_current_working_set();
            if(dataset.inputs >= 0 && dataset.weights >= 0)
            {
                buffer_manager_->OnDataReceived(DataType::INPUT, dataset.inputs);
                buffer_manager_->OnDataReceived(DataType::WEIGHT, dataset.weights);
            }

            if(dataset.outputs >= 0)
            {
                output_buffer_manager_->OnDataReceived(DataType::OUTPUT, dataset.outputs);
            }

            outputs_required_count_ = task_manager_->role_output_working_set_size_;
            outputs_received_count_ = 0;

            // 状态初始化
            dispatch_in_progress_ = false;
            logical_timestamp = 0;
            break;
        }

        case ROLE_GLB: {
            // 黄金参数定义
            const int K2_LOOPS = 16;
            const int P1_LOOPS = 16;
            const int FILL_DATA_SIZE = 9;  // W(6) + I(3)
            const int DELTA_DATA_SIZE = 1; // I(1)

            // 配置BufferManager
            EvictionSchedule glb_schedule = ScheduleFactory::createGLBEvictionSchedule();
            buffer_manager_.reset(new BufferManager(max_capacity, glb_schedule));
            // buffer_manager_->OnDataReceived(DataType::WEIGHT, 96);
            // buffer_manager_->OnDataReceived(DataType::INPUT, 18);
            current_data_size.write(buffer_manager_->GetCurrentSize());

            // 配置output buffer manager
            EvictionSchedule glb_output_schedule = ScheduleFactory::createGLBEvictionSchedule();
            output_buffer_manager_.reset(new BufferManager(max_capacity, glb_output_schedule));
            // output_buffer_manager_->OnDataReceived(DataType::OUTPUT, 512);

            // 配置TaskManager
            task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
            task_manager_->Configure(GlobalParams::workload, "ROLE_GLB");
            outputs_required_count_ = task_manager_->role_output_working_set_size_;
            outputs_received_count_ = 0;

            break;
        }

        case ROLE_BUFFER: {
            // 配置BufferManager
            EvictionSchedule buffer_schedule = ScheduleFactory::createBufferPESchedule();
            buffer_manager_.reset(new BufferManager(max_capacity, buffer_schedule));
            current_data_size.write(buffer_manager_->GetCurrentSize());

            // 配置output buffer manager
            EvictionSchedule buffer_output_schedule = ScheduleFactory::createOutputBufferSchedule();
            output_buffer_manager_.reset(new BufferManager(max_capacity, buffer_output_schedule));

            // 配置TaskManager
            task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
            task_manager_->Configure(GlobalParams::workload, "ROLE_BUFFER");
            outputs_required_count_ = 0;
            outputs_received_count_ = 0;
            compute_cycles = 0;

            break;
        }

        default:
            // 未处理的角色类型
            assert(false && "Unknown PE_Role in configure function");
            break;
    }

    //========================================================================
    // V. 通用状态初始化
    //========================================================================
    logical_timestamp = 0;

    // 调试日志
    cout << sc_time_stamp() << ": PE[" << local_id << "] configured as " << role_to_str(role)
         << " at level " << level_idx << endl;
}

void ProcessingElement::pe_init() {};
//     //========================================================================
//     // I. 黄金参数定义 (Golden Parameters)
//     //    - 源自Timeloop的逻辑分析和Accelergy的物理定义
//     //========================================================================
//      logical_timestamp = 0;
//     // ---- 逻辑循环结构 (from Timeloop) ----
//     const int K2_LOOPS = 16;
//     const int P1_LOOPS = 16;
    
//     // ---- 数据需求 (from Timeloop, 单位: Bytes) ----
//     const int FILL_DATA_SIZE = 9;  // W(6) + I(3)
//     const int DELTA_DATA_SIZE = 1; // I(1)
    
//     // ---- 物理容量 (from Accelergy, 单位: Bytes) ----
//     const int GLB_CAPACITY = 262144;
//     const int BUFFER_CAPACITY = 64;
    
//     //========================================================================
//     // II. 通用状态初始化
//     //     - 对所有PE都适用的默认值
//     //========================================================================
//     role = ROLE_UNUSED;
//     current_data_size = 0;
//     previous_ready_signal = -1; // 确保第一次ready信号变化时会打印日志
//     all_receive_tasks_finished = false;
//     all_transfer_tasks_finished = false;
//     current_receive_loop_level = -1; // -1 表示没有任务
//     current_transfer_loop_level = -1;
//     outputs_received_count_ = 0;
    
//     //========================================================================
//     // III. 基于角色的专属初始化
//     //========================================================================

//     if (local_id == 0) {
//         // --- ROLE_DRAM: 理想化的、一次性任务的生产者 ---
//         role = ROLE_DRAM;
//         max_capacity = 20000; // 假设DRAM容量为64KB
//         EvictionSchedule dram_schedule = ScheduleFactory::createDRAMEvictionSchedule();
//         buffer_manager_.reset(new BufferManager(max_capacity, dram_schedule));
//         buffer_manager_->OnDataReceived(DataType::WEIGHT, 96);
//         buffer_manager_->OnDataReceived(DataType::INPUT, 18);
        
        
//         // 初始化output buffer manager
//         EvictionSchedule dram_output_schedule = ScheduleFactory::createDRAMEvictionSchedule();
//         output_buffer_manager_.reset(new BufferManager(max_capacity, dram_output_schedule));
//         output_buffer_manager_->OnDataReceived(DataType::OUTPUT, 512);
//         max_capacity = -1;
//         downstream_node_ids.clear();
//         downstream_node_ids.push_back(1); // 下游是GLB
//         upstream_node_ids.clear(); // DRAM没有上游

//         task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
//         task_manager_->Configure(GlobalParams::workload, "ROLE_DRAM");
//         outputs_required_count_ = task_manager_->role_output_working_set_size_;

//         dispatch_in_progress_ = false;
//         logical_timestamp = 0; // 初始化TaskManager

        
//     } else if (local_id == 1) {
//         // --- ROLE_GLB: 智能的、任务驱动的中间商 ---
//         // current_data_size.write(0); // 初始为空
//         role = ROLE_GLB;
//         max_capacity = GLB_CAPACITY;
//         downstream_node_ids.clear();
//         downstream_node_ids.push_back(2); // 下游是Buffer
//         upstream_node_ids.clear();
//         upstream_node_ids.push_back(0); // 上游是DRAM

//         EvictionSchedule glb_schedule = ScheduleFactory::createGLBEvictionSchedule();
//         buffer_manager_.reset(new BufferManager(max_capacity, glb_schedule));
//         current_data_size.write(buffer_manager_->GetCurrentSize()); // 用manager的状态初始化信号
        
//         task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
//         task_manager_->Configure(GlobalParams::workload, "ROLE_GLB");
//         outputs_required_count_ = task_manager_->role_output_working_set_size_;
//         // 初始化output buffer manager
//         EvictionSchedule glb_output_schedule = ScheduleFactory::createGLBEvictionSchedule();
//         output_buffer_manager_.reset(new BufferManager(max_capacity, glb_output_schedule));
//         // GLB的接收任务：从DRAM接收1次大的数据块
//          transfer_task_queue.clear();
//         receive_task_queue.push_back({1});
//         receive_loop_counters.resize(1, 0);
//         current_receive_loop_level = 0;
        
//         // GLB的发送任务：向Buffer进行16x16的供应
//         transfer_task_queue.clear();
//         transfer_task_queue.push_back({K2_LOOPS});  // 16
//         transfer_task_queue.push_back({P1_LOOPS});  // 16
//         transfer_loop_counters.resize(2, 0);
//         current_transfer_loop_level = 1; // 从最内层(P1)循环开始

//         // GLB的发送块大小
//         transfer_fill_size = FILL_DATA_SIZE;
//         transfer_delta_size = DELTA_DATA_SIZE;

//         // GLB的阶段控制 (由其发送任务驱动)
//         current_stage = STAGE_FILL;

//         required_data_on_fill =  18; // FILL阶段需要的输入数据大小
//         required_data_on_delta = 18; // DELTA阶段需要的输入数据大小

//     } else if (local_id == 2) {
//         // --- ROLE_BUFFER: 带抽象消耗的最终目的地 ---
//         // current_data_size.write(0); // 初始为空

//         role = ROLE_BUFFER;
//         max_capacity = BUFFER_CAPACITY;
//         task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
//         task_manager_->Configure(GlobalParams::workload, "ROLE_BUFFER");
//         outputs_required_count_ = 0; 
//         EvictionSchedule buffer_schedule = ScheduleFactory::createBufferPESchedule();
//         buffer_manager_.reset(new BufferManager(max_capacity, buffer_schedule));
//         current_data_size.write(buffer_manager_->GetCurrentSize()); // 用manager的状态初始
        
//         // 初始化output buffer manager
//         EvictionSchedule buffer_output_schedule = ScheduleFactory::createOutputBufferSchedule();
//         output_buffer_manager_.reset(new BufferManager(max_capacity, buffer_output_schedule));
//         downstream_node_ids.clear(); // 是终点
//         upstream_node_ids.push_back(1); // 上游是GLB
        
//         receive_task_queue.clear();
//         receive_task_queue.push_back(K2_LOOPS);
//         receive_task_queue.push_back(P1_LOOPS);
//         receive_loop_counters.resize(2, 0);
//         current_receive_loop_level = 1;

//         // Buffer的消耗需求 (用于驱动抽象消耗和ready信号)
//         required_data_on_fill = FILL_DATA_SIZE;
//         required_data_on_delta = DELTA_DATA_SIZE;

//         required_output_data_on_delta = 2;
//         required_output_data_on_fill = 2;


        
//         // 抽象消耗状态
//         is_consuming = false;
//         consume_cycles_left = 0;
//         is_stalled_waiting_for_data = true; // 初始时等待数据

//         // Buffer的阶段控制 (由其消耗/接收任务驱动)
//         current_stage = STAGE_FILL;
//     }
//     // 可以在这里加一个调试日志来确认初始化结果
//     cout << sc_time_stamp() << ": PE[" << local_id << "] initialized as " << role_to_str(role);
//     transmittedAtPreviousCycle = false;
// }

int ProcessingElement::find_child_id(int id)
{
    for(size_t i = 0; i < downstream_node_ids.size(); i++)
    {
        if(downstream_node_ids[i] == id)
            return i;
    }
}

/**
 * @brief 更新并发送下游 ready 信号
 * 
 * 该函数在每个时钟周期被调用，负责：
 * 1. 检查 main 和 output 缓冲区的可用空间。
 * 2. 根据预先定义的“能力向量”，将可用空间转换为一个离散的“能力等级”。
 * 3. 将两个通道的能力等级用位编码方案合并成一个整数。
 * 4. 将编码后的值写入 `downstream_ready_out` 端口。
 */
void ProcessingElement::update_ready_signal() {
    // --- 步骤 0: 处理 Reset 和无效状态 ---
    if (reset.read()) {
        downstream_ready_out.write(0);
        previous_ready_signal = 0; // 同样重置内部状态
        return;
    }
    
    if (!buffer_manager_ || !output_buffer_manager_) {
        downstream_ready_out.write(0);
        previous_ready_signal = 0;
        return;
    }

    // --- 步骤 1: [核心] 计算主数据通道的能力等级 ---
    int main_level = 0;
    int available_space = buffer_manager_->GetCapacity() - buffer_manager_->GetCurrentSize();
    
    // 遍历本角色已加载的主通道能力向量 (e.g., {0, 1, 9} for COMPUTE)
    // 向量已按从小到大排序
    for (int i = 0; i < GlobalParams::CapabilityMap[role].main_channel_caps.size(); ++i) {
        if (available_space >= GlobalParams::CapabilityMap[role].main_channel_caps[i]) {
            // 我们能容纳大小为 my_main_channel_caps_[i] 的包
            // 将等级设置为当前索引 i (0, 1, 2...)
            main_level = i; 
        } else {
            // 如果连更小的都容纳不了，更大的肯定也不行，提前退出
            break;
        }
    }

    // --- 步骤 2: [核心] 计算输出数据通道的能力等级 ---
    // (逻辑与主通道完全相同，只是使用不同的缓冲区和能力向量)
    int output_level = 0;
    int available_output_space = output_buffer_manager_->GetCapacity() - output_buffer_manager_->GetCurrentSize();
    
    // 遍历本角色已加载的输出通道能力向量 (e.g., {0, 2} for COMPUTE)
    for (int i = 0; i < GlobalParams::CapabilityMap[role].output_channel_caps.size(); ++i) {
        if (available_output_space >= GlobalParams::CapabilityMap[role].output_channel_caps[i]) {
            output_level = i;
        } else {
            break;
        }
    }
    
    // --- 步骤 3: [核心] 使用位移和位或(OR)运算进行编码 ---
    // 将 output_level 的2个比特位放在高位 (bits 2-3)，
    // 将 main_level 的2个比特位放在低位 (bits 0-1)。
    // 这假设每个通道的能力等级不会超过 4 级 (0-3)。
    int ready_value = (output_level << 2) | main_level;
    
    // --- 步骤 4: 写入端口 ---
    downstream_ready_out.write(ready_value);

    // --- 步骤 5: 调试日志 ---
    if (ready_value != previous_ready_signal) {
        dbg(sc_time_stamp(), name(), "[READY_SIG] Updating ready_out from " 
             + std::to_string(previous_ready_signal) + " to " + std::to_string(ready_value),
             "Decoded -> MainLevel:" + std::to_string(main_level) +
             ", OutputLevel:" + std::to_string(output_level));
        
        previous_ready_signal = ready_value;
    }
}

void ProcessingElement::rxProcess() {
    // --- 0. 复位逻辑 ---
    if (reset.read()) {
        // 重置接收状态计数器
        main_receiving_size_ = 0;
        output_receiving_size_ = 0;

        for (int i = 0; i < 2; ++i) {
            ack_rx[i].write(0);
            current_level_rx[i] = 0;

            // 重置 buffer_full_status_rx 输出端口（表示所有VC都未满）
            TBufferFullStatus reset_status;
            for (int vc = 0; vc < MAX_VIRTUAL_CHANNELS; ++vc) {
                reset_status.mask[vc] = false; // false 表示未满
            }
            buffer_full_status_rx[i].write(reset_status);
        }

        // 清空所有缓冲区
        rx_buffer.fill(Buffer()); // 重新构造所有Buffer
        for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
        {
            rx_buffer[vc].SetMaxBufferSize(GlobalParams::buffer_depth);
            rx_buffer[vc].setLabel(string(name())+"->buffer[0]");
        }
        return;
    }

    // ==========================================================
    // 阶段一: [核心] 调用内部处理函数
    // ==========================================================
    // 在接收任何新 Flit 之前，首先尝试处理掉物理缓冲区中已有的 Flit
    internal_transfer_process();

    // ==========================================================
    // 阶段二: 接收新的入站 Flit
    // ==========================================================
    // 这部分是物理接收逻辑，负责将新来的 Flit Push 进 rx_buffer
    for (int i = 0; i < 2; ++i) {
        if (i == 0) {
            // --- Port 0: 新的支持VC的缓冲逻辑 ---

            // 物理握手：检查是否有新的请求req_rx[port_index].read() == 1 - current_level
            if (req_rx[0].read() == 1 - current_level_rx[0]) {

                // 读取 Flit
                Flit flit = flit_rx[0].read();

                // 获取 VC ID
                int vc_id = flit.vc_id;

                // 检查 VC ID 是否有效
                if (vc_id >= 0 && vc_id < MAX_VIRTUAL_CHANNELS) {

                    // 推入 BufferBank 中对应的 VC 缓冲区
                    // 使用断言确保缓冲区未满（通过流控机制保证）
                    assert(!rx_buffer[vc_id].IsFull() && "VC buffer should not be full due to backpressure");

                    rx_buffer[vc_id].Push(flit);

                    // 确认握手：翻转 current_level 并写入 ack_rx
                    current_level_rx[0] = 1 - current_level_rx[0];
                    ack_rx[0].write(current_level_rx[0]);

                    // 调试日志
                    std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                              << "[RX_PORT0] Received Flit on VC " << vc_id
                              << " src_id=" << flit.src_id
                              << " dst_id=" << flit.dst_id
                              << " flit_type=" << flit.flit_type
                              << " buffer_size=" << rx_buffer[vc_id].Size()
                              << " flit_data_type=" << DataType_to_str(flit.data_type)
                              << " flit_seq_no=" << flit.sequence_no
                              << " flit_command=" << flit.command
                              << std::endl;
                } else {
                    // 无效的 VC ID - 这是编程错误，应该使用断言
                    assert(false && "Invalid VC ID received");
                }
            }
        } else if (i == 1) {
            // --- Port 1: 暂时保留旧逻辑或占位符 ---
            // TODO: 为 port 1 实现新的缓冲逻辑

            // 暂时保留旧的调用（标记为待删除）
            // handle_rx_for_port(1);

            // 或者暂时留空
        }
    }

    // ==========================================================
    // 阶段三: 更新对上游的流控信号
    // ==========================================================
    // 这个逻辑报告的是物理缓冲区 rx_buffer 的状态
    for (int i = 0; i < 2; ++i) {
        TBufferFullStatus status;

        if (i == 0) {
            // Port 0: 检查每个 VC 的缓冲区状态
            for (int vc = 0; vc < MAX_VIRTUAL_CHANNELS; ++vc) {
                status.mask[vc] = rx_buffer[vc].IsFull(); // true 表示已满
            }
        } else {
            // Port 1: 暂时返回默认状态（所有VC都未满）
            for (int vc = 0; vc < MAX_VIRTUAL_CHANNELS; ++vc) {
                status.mask[vc] = false; // false 表示未满
            }
        }

        // 写入流控状态输出端口
        buffer_full_status_rx[i].write(status);
    }
}

// 新增：内部流式处理函数实现
void ProcessingElement::internal_transfer_process() {
    // 遍历所有虚拟通道
    for (int vc = 0; vc < GlobalParams::n_virtual_channels; ++vc) {
        Buffer& vc_buffer = rx_buffer[vc];

        // 流式处理循环：处理该VC中所有可以处理的Flit
        while (!vc_buffer.IsEmpty()) {
            // 窥视队首Flit
            const Flit& flit = vc_buffer.Front();

            if (flit.flit_type == FLIT_TYPE_HEAD) {
                // [特殊处理] 如果是回传包 (command == -1)，跳过流控检查
                if (flit.command == -1) {
                    // 回传包无条件接受（假设已预留空间）
                    vc_buffer.Pop();
                    
                    std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                              << "[INTERNAL_TRANSFER] Accepted OUTPUT_RETURN HEAD Flit on VC " << vc
                              << " src_id=" << flit.src_id
                              << " payload=" << flit.payload_data_size
                              << std::endl;
                    continue; // 继续处理下一个flit
                }

                
                // 正常数据包的流控检查
                BufferManager* target_manager = nullptr;
                size_t* receiving_size = nullptr;

                if (flit.data_type == DataType::OUTPUT) {
                    target_manager = output_buffer_manager_.get();
                    receiving_size = &output_receiving_size_;
                } else {
                    target_manager = buffer_manager_.get();
                    receiving_size = &main_receiving_size_;
                }

                // 执行关键的流控决策
                size_t required_capacity = target_manager->GetCurrentSize() +
                                          *receiving_size +
                                          flit.payload_data_size;

                if (required_capacity <= target_manager->GetCapacity()) {
                    // 检查通过：预留空间
                    *receiving_size += flit.payload_data_size;
                    vc_buffer.Pop();

                    // 处理command_id等元数据
                    if (flit.command != -1) {
                        pending_commands_[flit.logical_timestamp] = flit.command;
                    }

                    std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                              << "[INTERNAL_TRANSFER] Accepted HEAD Flit on VC " << vc
                              << " src_id=" << flit.src_id
                              << " payload=" << flit.payload_data_size
                              << " reserved_space=" << *receiving_size
                              << " command_id=" << flit.command
                              << std::endl;
                } else {
                    // 逻辑缓冲区空间不足，阻塞当前VC
                    std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                              << "[INTERNAL_TRANSFER] HEAD Flit BLOCKED on VC " << vc
                              << " required=" << required_capacity
                              << " capacity=" << target_manager->GetCapacity()
                              << std::endl;
                    break;
                }

            } else if (flit.flit_type == FLIT_TYPE_BODY) {
                // BODY Flit无条件丢弃
                vc_buffer.Pop();

            } else if (flit.flit_type == FLIT_TYPE_TAIL) {
                // [核心修改] 特殊处理回传包
                if (flit.command == -1) {
                    // 这是一个输出回传包，只更新计数器，不调用BufferManager
                    outputs_received_count_ += flit.payload_data_size;
                    
                    vc_buffer.Pop();
                    
                    std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                              << "[INTERNAL_TRANSFER] Processed OUTPUT_RETURN TAIL Flit on VC " << vc
                              << " src_id=" << flit.src_id
                              << " payload=" << flit.payload_data_size
                              << " total_outputs_received=" << outputs_received_count_
                              << "/" << outputs_required_count_
                              << std::endl;
                    
                    // 通知可能等待输出的逻辑
                    buffer_state_changed_event.notify(SC_ZERO_TIME);
                    continue;
                }

                // 正常数据包的TAIL处理
                BufferManager* target_manager = nullptr;
                size_t* receiving_size = nullptr;

                if (flit.data_type == DataType::OUTPUT) {
                    target_manager = output_buffer_manager_.get();
                    receiving_size = &output_receiving_size_;
                } else {
                    target_manager = buffer_manager_.get();
                    receiving_size = &main_receiving_size_;
                }

                // 调用OnDataReceived将数据正式"入库"
                if (flit.data_type == DataType::OUTPUT) {
                    target_manager->OnDataReceived(DataType::OUTPUT, flit.payload_data_size);
                } else {
                    target_manager->OnDataReceived(flit.data_type, flit.payload_data_size);
                }

                // 释放预留的空间
                assert(*receiving_size >= flit.payload_data_size && "Receiving size underflow");
                *receiving_size -= flit.payload_data_size;

                vc_buffer.Pop();

                // 通知计算逻辑
                buffer_state_changed_event.notify(SC_ZERO_TIME);

                std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                          << "[INTERNAL_TRANSFER] Processed TAIL Flit on VC " << vc
                          << " src_id=" << flit.src_id
                          << " type=" << DataType_to_str(flit.data_type)
                          << " payload=" << flit.payload_data_size
                          << " committed_to_logic_buffer"
                          << std::endl;
            }
        }
    }
}
// 新增：统一的VC发送处理函数实现
void ProcessingElement::handle_tx_for_all_vcs() {
    // [核心] 遍历所有 VC 发送队列
    for (int vc = 0; vc < GlobalParams::n_virtual_channels; ++vc) {
        // 如果这个 VC 的队列里没有包，跳过
        if (packet_queues_[vc].empty()) {
            continue;
        }

        // 检查当前端口是否正在等待ACK（简化模型：假设统一使用port 0）
        if (ack_tx[0].read() != current_level_tx[0]) {
            // 端口正忙，跳过这个VC
            continue;
        }

        // "窥视"队首的包
        Packet& packet_to_send = packet_queues_[vc].front();

        // 检查下游邻居的 VC 流控状态
        TBufferFullStatus downstream_status = buffer_full_status_tx[0].read();

        if (downstream_status.mask[vc] == true || packet_queues_[vc].empty()) {
            // 这个 VC 被阻塞了，跳过，去处理下一个 VC
            continue;
        }

        // --- 流控检查通过！可以发送 ---

        Flit flit_to_send = generate_next_flit_from_queue(packet_queues_[vc]);

        // 设置VC ID
        flit_to_send.vc_id = vc;

        // 物理发送 (统一到 Port 0)
        flit_tx[0].write(flit_to_send);
        current_level_tx[0] = 1 - current_level_tx[0];
        req_tx[0].write(current_level_tx[0]);

        // 打印发送日志
        std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                  << "[TX_VC" << vc << "] Sent Flit type=" << flit_to_send.flit_type
                  << " src=" << flit_to_send.src_id << " dst=" << flit_to_send.dst_id
                  << " command_id=" << flit_to_send.command << std::endl;

        // 如果发送的是 TAIL Flit，从这个 VC 的队列中 pop
        if (flit_to_send.flit_type == FLIT_TYPE_TAIL) {
            // packet_queues_[vc].pop();
            std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                      << "[TX_VC" << vc << "] Completed packet transmission"
                      << std::endl;
        }

        // 既然我们每周期只发送一个 Flit，就在成功发送后退出循环
        break;
    }
}

// 新增：辅助函数实现
bool ProcessingElement::packet_queues_are_empty() const {
    for (const auto& q : packet_queues_) {
        if (!q.empty()) {
            return false;
        }
    }
    return true;
}

int ProcessingElement::get_vc_id_for_packet(const Packet& pkt) const {
    // 简单的VC分配策略
    // 可以根据数据类型、目标节点或其他标准来分配VC
    if (pkt.is_multicast) {
        return 0; // 输出数据使用VC 2
    } else if (pkt.data_type == DataType::OUTPUT) {
        return 0; // 权重数据使用VC 0
    } else {
        return 1; // 输入数据使用VC 1
    }
}

int ProcessingElement::get_vc_id_for_packet_by_task(DataDispatchInfo task) const {
    // 根据数据类型分配VC ID的辅助函数
    if (task.type == DataType::OUTPUT) {
        return 2; // 输出数据使用VC 2
    } else if (task.target_ids.size() > 1) {
        return 1; // 多播数据使用VC 0
    } else {
        return 0; // 单播数据使用VC 1
    }
}

void ProcessingElement::handle_rx_for_port(int port_index) {
    // --- 1. 根据 port_index 选择正确的资源 ---
    bool& current_level = (port_index == 0) ? current_level_rx[0] : current_level_rx[1];
    int* payload_sizes_buffer = (port_index == 0) ? incoming_payload_sizes : incoming_output_payload_sizes;
    
    // dbg(sc_time_stamp(), name(), "[RX_PROCESS] Checking port " + std::to_string(port_index),
    //     "Current Level: " + std::to_string(current_level) + " req_rx: " + std::to_string(req_rx[port_index].read()));
    // --- 2. 检查是否有新的请求 (标准的 ABP 握手) ---
    if (req_rx[port_index].read() == 1 - current_level) {
        
        // --- 3. 读取 Flit ---
        Flit flit = flit_rx[port_index].read();

        // --- 4. 处理 HEAD Flit 事件 ---
        if (flit.flit_type == FLIT_TYPE_HEAD && flit.command != -1) {
            pending_commands_[flit.logical_timestamp] = flit.command;
            // dbg("" + sc_time_stamp().to_string(), name(), "[RX_PROCESS] Received HEAD Flit on port " + std::to_string(port_index),
            //     ", Command: " + std::to_string(flit.command) +
            //     ", Logical Timestamp: " + std::to_string(flit.logical_timestamp));
               std::cout << "@ " << sc_time_stamp() << " [" << name() << "]: "
              << "[RX_HEAD] Received HEAD Flit!"
              << " src_id=" << flit.src_id // 来源PE
              << " dst_id=" << flit.dst_id // 目的地PE (应该是自己)
              << " packet_type=" << DataType_to_str(flit.data_type) // 包类型
              << " timestamp=" << flit.logical_timestamp // 逻辑时间戳
              << " sequence_no=" << flit.sequence_no // 序列号
              << "payload_size=" << flit.payload_data_size // 负载大小
              << std::endl;
        }
        
        // --- 5. 处理 TAIL Flit 事件 ---
        if (flit.flit_type == FLIT_TYPE_TAIL) {
            if(flit.command == -1)
            {
                outputs_received_count_+=flit.payload_data_size;
                // dbg("" + sc_time_stamp().to_string(), name(), "[RX_PROCESS] Received TAIL Flit on port " + std::to_string(port_index),
                //     ", Payload Size: " + std::to_string(flit.payload_data_size) +
                //     ", Total Outputs Received: " + std::to_string(outputs_received_count_) +
                //     "/" + std::to_string(outputs_required_count_));
            }
            else
            {


                if(flit.is_output){
                    output_buffer_manager_->OnDataReceived(DataType::OUTPUT, 
                        flit.payload_data_size);
                    buffer_state_changed_event.notify(); // 立即在当前delta周期触发
                // dbg("" + sc_time_stamp().to_string(), name(), "[RX_PROCESS] Received TAIL Flit on port " + std::to_string(port_index),
                //     ", Payload Size: " + std::to_string(flit.payload_data_size) +
                //     ", New Buffer Size: " + std::to_string(output_buffer_manager_->GetCurrentSize()) +
                //     "/" + std::to_string(output_buffer_manager_->GetCapacity()));
                }
                else{
                    buffer_manager_->OnDataReceived(flit.data_type, 
                        flit.payload_data_size);
                    buffer_state_changed_event.notify(); // 立即在当前delta周期触发
                // dbg("" + sc_time_stamp().to_string(), name(), "[RX_PROCESS] Received TAIL Flit on port " + std::to_string(port_index),
                //     ", Payload Size: " + std::to_string(flit.payload_data_size) +
                //     ", New Buffer Size: " + std::to_string(buffer_manager_->GetCurrentSize()) +
                //     "/" + std::to_string(buffer_manager_->GetCapacity()));
                }

            }

    }

    current_level = 1 - current_level;
    ack_rx[port_index].write(current_level);
    // --- 7. 持续地将 ack 电平写回端口 ---
}
}

// in PE.cpp

void ProcessingElement::txProcess() {
    // 复位逻辑保持不变（更新以清空新的VC队列）
    if (reset.read()) {
        req_tx[0].write(0);
        req_tx[1].write(0);
        current_level_tx[0] = 0;
        current_level_tx[1] = 0;

        // 清空所有VC队列
        for (auto& q : packet_queues_) {
            while (!q.empty()) q.pop();
        }
        return;
    }

    // --- 步骤 A: 如果需要，智能地生成Packet ---
    // 只有在所有VC队列为空时，才尝试生成新的Packet
        // 根据角色调用生成逻辑。
        // run_storage_logic 内部已经包含了所有"意图"和"能力"的匹配检查。
        // 如果条件不满足，它什么也不会做，VC队列依然为空。
        if (role == ROLE_DRAM || role == ROLE_GLB) {
            run_storage_logic();
        }

        else if(role == ROLE_BUFFER){
            run_compute_logic();
        }

    // --- 步骤 B: [核心替换] 调用新的统一发送处理器 ---
    handle_tx_for_all_vcs();

    if(role!=ROLE_BUFFER && task_manager_->is_in_sync_points(logical_timestamp) && outputs_received_count_ < outputs_required_count_)
    {
        return;
    }


    if(role!=ROLE_BUFFER && current_dispatch_task_.sub_tasks.empty()&&packet_queues_are_empty() && dispatch_in_progress_)
        {
            logical_timestamp++;
            dispatch_in_progress_ = false;
            cout << sc_time_stamp() << ": PE[" << local_id << "] Completed dispatch for timestamp " << logical_timestamp - 1 << endl;
            if(logical_timestamp >= task_manager_->get_total_timesteps())
            {
                reset_logic();

            }

        }

    if(role==ROLE_BUFFER && is_compute_complete == true)
    {
        logical_timestamp++;
        is_compute_complete = false;
        cout<< sc_time_stamp() << ": PE[" << local_id << "] Completed compute for cycle " << compute_cycles << " compute latency is " << task_manager_->get_compute_latency() << endl;
        compute_cycles++;
        if(logical_timestamp >= task_manager_->get_total_timesteps())
        {
            reset_logic();

        }

    }
}

void ProcessingElement::reset_logic()
{
    if(role==ROLE_DRAM)
    {
        dbg("All tasks completed quiting simulation");
        // sc_stop();
        return;
    }



    if((role == ROLE_GLB || role == ROLE_BUFFER)&& !pending_commands_.empty())
    {

        auto it = pending_commands_.begin();
        int command = it->second;
        pending_commands_.erase(it);
        DataDelta cmd;
        if(command >= task_manager_-> get_command_count())
        {
            cmd = task_manager_->get_current_working_set();
        }
        else
        {
            cmd = task_manager_->get_command_definition(command);
        }
        
        buffer_manager_->RemoveData(DataType::WEIGHT, cmd.weights);
        buffer_manager_->RemoveData(DataType::INPUT, cmd.inputs);
        if(cmd.outputs >0)
        {
            Packet pkt;
            pkt.src_id = local_id;
            pkt.dst_id = upstream_node_ids[0];
            pkt.payload_data_size = cmd.outputs;
            pkt.data_type = DataType::OUTPUT;
            pkt.size = pkt.flit_left = cmd.outputs+2;
            pkt.command = -1; //表示这是一个回送包
            pkt.is_multicast = false;
            pkt.vc_id = 2; //回送包使用vc 0
            // dbg(sc_time_stamp(), name(), "[RESET_LOGIC] Generating output return Packet to PE " + std::to_string(pkt.dst_id) +
            //     " for " + std::to_string(cmd.outputs) + " bytes.");
            std::cout << "@ " << sc_time_stamp() 
          << " [" << name() << "]: "
          << "[RESET_LOGIC] Generating output return Packet to PE " << pkt.dst_id
          << " for " << cmd.outputs << " bytes." 
          << std::endl;
                
            packet_queues_[pkt.vc_id].push(pkt);




        }
        output_buffer_manager_->RemoveData(DataType::OUTPUT, cmd.outputs);

        cout<< sc_time_stamp() << ": PE[" << local_id << "]" << buffer_manager_->GetCurrentSize() << "/" << buffer_manager_->GetCapacity() << " bytes remaining after resetting for timestamp " << logical_timestamp << endl;
        output_buffer_state_changed_event.notify();
    }

    logical_timestamp = 0;
    outputs_received_count_ = 0;


}



// void ProcessingElement::handle_tx_for_port(int port_index) {
//     // --- 1. 根据 port_index 选择正确的资源 ---
//     std::queue<Packet>& current_queue = (port_index == 0) ? packet_queue : packet_queue_2;
//     bool& current_level = (port_index == 0) ? current_level_tx[0] : current_level_tx[1];

//     // 如果对应队列为空，则无事可做
//     if (current_queue.empty()) {
//         return;
//     }

//     // --- 2. "窥探"队首的Packet，以确定其发送要求 ---
//     const Packet& pkt_to_send = current_queue.front();
//     std::string packet_type_str = get_packet_type_str(pkt_to_send, port_index);

//     // --- 3. 获取发送要求 (Required Capability) ---
//     // (假设存在一个辅助函数来获取，以保持代码整洁)
//     if(pkt_to_send.command!=-1) //不对回传包进行capability检查
//     {
//         int required_capability = get_required_capability(pkt_to_send.payload_data_size, port_index);


//         if(pkt_to_send.is_multicast)
//         {
//             for(auto dst_id : pkt_to_send.multicast_dst_ids)
//             {
//                 if(decode_ready_signal(downstream_ready_in[find_child_id(dst_id)]->read(), port_index) < required_capability)
//                 return;
//             }
//         }
//         else
//         {
//             // --- 4. 解码下游的 ready 信号 ---
//             int downstream_capability = decode_ready_signal(downstream_ready_in[find_child_id(pkt_to_send.dst_id)]->read(), port_index);
//             // 如果下游能力不足，则无法发送
//             if (downstream_capability < required_capability) {
//                 return;
//             }
//         }
//     }

// // dbg(sc_time_stamp(), name(), "[TX_PORT_" + std::to_string(port_index) + "] ack_tx:",
// //     std::to_string(ack_tx[port_index].read()),
//     // "current_level:", std::to_string(current_level));
//     // --- 5. 终极守门员检查：检查 ack 和精确的能力匹配 ---
//     if (ack_tx[port_index].read() == current_level) {
//         // --- 6. [关键] 在 Packet 对象被销毁前，保存其完整信息 ---
//         // 因为 generate_next_flit_from_queue 在发送TAIL后会 pop 队列
//         Packet sent_packet_info = pkt_to_send; 

//         // --- 7. 生成下一个 Flit ---
//         bool is_output = port_index == 0 ? false : true;
//         Flit flit = generate_next_flit_from_queue(current_queue, is_output);

//         // --- 8. 处理 HEAD/TAIL 事件 ---
//         if (flit.flit_type == FLIT_TYPE_HEAD) {
//             // dbg(sc_time_stamp(), name(), "[TX_PORT_" + std::to_string(port_index) + "] Sending HEAD:",
//             //     "Type=" + packet_type_str,
//             //     "Target=" + std::to_string(flit.dst_id),
//             //     "Timestamp=" + std::to_string(flit.logical_timestamp));
//             std::cout << "@ " << sc_time_stamp() << " [" << name() << "]: "
//           << "[TX_HEAD] Port " << port_index << ":"
//           << " Type=" << packet_type_str
//           << " dst_id=" << flit.dst_id
//           << " Timestamp=" << flit.logical_timestamp
//           << " PktSize=" << pkt_to_send.payload_data_size << " bytes" // 含义1: 包的总大小
//           << " Buffersize=" << buffer_manager_->GetCurrentSize()  // 含义2: 我的队列积压
//           << std::endl;
//         }

//         if (flit.flit_type == FLIT_TYPE_BODY) {
//             // dbg(sc_time_stamp(), name(), "[TX_PORT_" + std::to_string(port_index) + "] Sending BODY:",
//             //     "Type=" + packet_type_str,
//             //     "Target=" + std::to_string(flit.dst_id),
//             //     "SeqNo=" + std::to_string(flit.sequence_no));
//         }
        
//         if (flit.flit_type == FLIT_TYPE_TAIL) {
//             // [核心修改] 不再处理任何高层逻辑，
//             // 而是调用事件处理函数，将 packet 的完整信息传递过去。
//             // dbg(sc_time_stamp(), name(), "[TX_PORT_" + std::to_string(port_index) + "] Sending TAIL:",
//             //     "Type=" + packet_type_str,
//             //     "Target=" + std::to_string(flit.dst_id),
//             //     "SeqNo=" + std::to_string(flit.sequence_no));
//             // process_tail_sent_event(port_index, sent_packet_info);
//         }

//         // --- 9. 物理发送 Flit 并更新握手状态 ---
//         flit_tx[port_index].write(flit);
//         current_level = 1 - current_level;
//         req_tx[port_index].write(current_level);
//     }
// }

 //暂时保留逻辑 以后实现
size_t ProcessingElement::get_required_capability(size_t size, int port_index) const {
     PE_Role nextRole = static_cast<PE_Role>(static_cast<int>(role) + 1);
     if(port_index == 0 )
     {
        for(size_t i=0;i<GlobalParams::CapabilityMap.at(nextRole).main_channel_caps.size();i++)
        {
            if(size == GlobalParams::CapabilityMap.at(nextRole).main_channel_caps[i])
            {
                return i;
            }

        }
    }
    else
    {
        for(size_t i=0;i<GlobalParams::CapabilityMap.at(nextRole).output_channel_caps.size();i++)
        {
            if(size == GlobalParams::CapabilityMap.at(nextRole).output_channel_caps[i])
            {
                return i;
            }

        }
    }

}

std::string ProcessingElement::get_packet_type_str(const Packet& packet, int port_index) const {
    if (port_index == 0) {
        return (packet.payload_data_size == transfer_fill_size) ? "FILL" : "DELTA";
    } else {
        return "OUTPUT";
    }
}

int ProcessingElement::decode_ready_signal(int combined_ready_value, int port_index) const {
    if (port_index == 0) {
        // 主数据通道在低位
        return combined_ready_value & 0b11; 
    } else { // port_index == 1
        // 输出通道在高位
        return (combined_ready_value >> 2) & 0b11;
    }
}

void ProcessingElement::process_tail_sent_event(int port_index, Packet sent_status) {
    return;
}

void ProcessingElement::run_compute_logic() {

    if (role != ROLE_BUFFER) {
        return;
    }

        if (!compute_in_progress_) {
        // 4. Dependency Check: Verify that all required data for the *current* timestep's computation is present.
        // A compute task's dependencies are its required Inputs and Weights.
            const auto& required_data = task_manager_->get_working_set_for_role(role_to_str(role))->get_data_map();
            consume_cycles_left = task_manager_->get_compute_latency();
            
            for (const auto& entry : required_data) {
                DataType type = entry.first;
                size_t size = entry.second;
                
                // For a compute PE, the working set should only define dependencies (Inputs, Weights), not Outputs.
                if (type == DataType::INPUT || type == DataType::WEIGHT) {
                    if (!buffer_manager_->AreDataTypeReady(type, size)) {
                        // If any dependency is not met, we cannot start the computation yet.
                        return; 
                    }
                }

                if(type == DataType::OUTPUT)
                {
                    if(!output_buffer_manager_->AreDataTypeReady(type, size))
                    {
                        return;
                    }
                }
            }

            compute_in_progress_ = true;
        }

        if (compute_in_progress_) {
            consume_cycles_left--;
            if (consume_cycles_left == 0) {
                compute_in_progress_ = false;
                is_compute_complete = true;
            }
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
        case ROLE_DRAM: return "ROLE_DRAM";
        case ROLE_GLB: return "ROLE_GLB";
        case ROLE_BUFFER: return "ROLE_BUFFER";
        default: return "UNKNOWN";
    }
}
// in PE.cpp
void  ProcessingElement::run_storage_logic() {
    if (role != ROLE_GLB && role != ROLE_DRAM) {
        return;
    }

    if (logical_timestamp >= task_manager_->get_total_timesteps()) {
        all_transfer_tasks_finished = true;
        return;
    }

    if(!dispatch_in_progress_)
    {
        std::map<DataType,size_t> data_map = task_manager_->get_working_set_for_role(role_to_str(role))->get_data_map();
        for(const auto& entry : data_map)
        {
            if(entry.first != DataType::OUTPUT)
            {
                if(!buffer_manager_->AreDataTypeReady(entry.first,entry.second)) return;
            }
            else
            {
                if(!output_buffer_manager_->AreDataTypeReady(entry.first,entry.second)) return;
            }
        }

         current_dispatch_task_ = task_manager_->get_task_for_timestep(logical_timestamp);
         std::cout << sc_time_stamp() << ": PE[" << local_id << "] Starting dispatch for timestep " << logical_timestamp << " with " 
                   << current_dispatch_task_.sub_tasks.size() << " subtasks." << std::endl;
         command_to_send = get_command_to_send();


         dispatch_in_progress_ = true;
    }

    if (current_dispatch_task_.sub_tasks.empty()) {
        return; 
    }

    // --- 1. [核心] 从待办列表中随机选择一个子任务 ---
    int num_pending_subtasks = current_dispatch_task_.sub_tasks.size();
    auto it = current_dispatch_task_.sub_tasks.begin();

    while (it != current_dispatch_task_.sub_tasks.end())
    {
        DataDispatchInfo& selected_task = *it;
        // 计算此数据类型对应的VC ID
        int vc_id = get_vc_id_for_packet_by_task(selected_task);

        // 检查对应的VC队列是否为空（实现"size只为1"的规则）
        if (!packet_queues_[vc_id].empty()) {
            ++it;
            continue; // 该VC通道忙，跳过
        }


        std::cout << sc_time_stamp() << ": PE[" << local_id << "] Generating packet for task: "
                << "Type=" << DataType_to_str(selected_task.type)
                << ", Size=" << selected_task.size;
        for(auto target_id : selected_task.target_ids)
        {
            std::cout << ", Target=" << target_id;
        }
        std::cout << std::endl;

        Packet pkt;
        pkt.src_id = local_id;
        pkt.is_multicast = (selected_task.target_ids.size() >1);
        if(pkt.is_multicast)
        {
            for(auto target_id : selected_task.target_ids)
            {
                pkt.multicast_dst_ids.push_back(target_id);
            }
        }
        else
        {
            pkt.dst_id = *selected_task.target_ids.begin();
        }
        pkt.vc_id = vc_id;  // 使用预先计算的VC ID
        pkt.payload_data_size = selected_task.size;
        pkt.logical_timestamp = logical_timestamp;
        pkt.data_type = selected_task.type;
        // pkt.is_output = (selected_task.type == DataType::OUTPUT);
        pkt.size = pkt.flit_left = selected_task.size+2;
        pkt.command = command_to_send;

        // 将Packet推入对应的VC队列
        packet_queues_[vc_id].push(pkt);
        // for(auto target_id : selected_task.target_ids)
        //     {
        //             current_dispatch_task_.record_completion(selected_task.type, target_id,selected_task.size);
        //     }
        it = current_dispatch_task_.sub_tasks.erase(it);   
    } 
    
}

int ProcessingElement::get_command_to_send() // tofix 当完整模拟时需要通过获取子
{
    PE_Role nextRole = static_cast<PE_Role>(static_cast<int>(role) + 1);
    auto commands = task_manager_->get_commands_for_role(role_to_str(nextRole));
    if (commands == nullptr || commands->empty()) {
        dbg(sc_time_stamp(), name(), "[CMD_ERROR] No command definitions found for downstream role: " + role_to_str(nextRole));
        return -2; // 返回无效命令
    }

    if(logical_timestamp+1 >= task_manager_->get_total_timesteps())
    {
        return commands->size()+1;
    }
    DataDelta next_delta;
    DispatchTask next_task = task_manager_->get_task_for_timestep(logical_timestamp+1);

    for (const auto& sub_task : next_task.sub_tasks) {
        int multicast_factor = sub_task.target_ids.size();
        if (sub_task.type == DataType::WEIGHT) next_delta.weights += sub_task.size*multicast_factor;
        if (sub_task.type == DataType::INPUT)  next_delta.inputs  += sub_task.size*multicast_factor;
        if (sub_task.type == DataType::OUTPUT) next_delta.outputs += sub_task.size*multicast_factor;
    }

    for (const auto& cmd : *commands) {
        // 比较推算出的 payload 和命令中定义的 payload
        if (cmd.evict_payload.weights == next_delta.weights/downstream_node_ids.size() &&
            cmd.evict_payload.inputs  == next_delta.inputs/downstream_node_ids.size() &&
            cmd.evict_payload.outputs == next_delta.outputs/downstream_node_ids.size())
        {
            // 找到了完全匹配的命令！
            return cmd.command_id;
        }
    }

    return -2;

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

Flit ProcessingElement::generate_next_flit_from_queue(std::queue<Packet>& queue)
{
    Flit flit;
    Packet packet = queue.front();

    // 填充公共字段
    flit.src_id = packet.src_id;
    if(packet.is_multicast)
    {
        flit.multicast_dst_ids = packet.multicast_dst_ids;
        flit.is_multicast = true;
    }
    else
    {
        flit.dst_id = packet.dst_id;
        flit.is_multicast = false;
    }
    flit.vc_id = packet.vc_id;
    flit.logical_timestamp = packet.logical_timestamp;
    flit.sequence_no = packet.size - packet.flit_left;
    flit.sequence_length = packet.size;
    flit.hop_no = 0;
    flit.payload_data_size = packet.payload_data_size;
    flit.hub_relay_node = NOT_VALID;
    flit.data_type = packet.data_type;
    flit.command = packet.command;

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

// Flit ProcessingElement::nextFlit()
// {
//     return generate_next_flit_from_queue(packet_queue, false);
// }

// Flit ProcessingElement::nextOutputFlit()
// {
//     return generate_next_flit_from_queue(packet_queue_2, true);
// }
unsigned int ProcessingElement::getQueueSize() const
{
    return packet_queues_.size();
}