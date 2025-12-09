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
#include <cmath>
#include <numeric>
int ProcessingElement::randInt(int min, int max) {
  return min + (int)((double)(max - min + 1) * rand() / (RAND_MAX + 1.0));
}

ProcessingElement::~ProcessingElement() {
  if (unified_buffer_manager_) {
    delete unified_buffer_manager_;
    unified_buffer_manager_ = nullptr;
  }
}

// 新增：动态配置函数实现
void ProcessingElement::configure(int id, int level_idx,
                                  const HierarchicalConfig &topology_config) {
  //========================================================================
  // I. 保存基础信息
  //========================================================================
  this->local_id = id;
  this->level_index = level_idx;

  //========================================================================
  // II. 根据层级确定角色和基础配置
  //========================================================================
  assert(level_idx < topology_config.levels.size());
  const LevelConfig &level_config = topology_config.get_level_config(level_idx);

  // 设置角色（假设每层只有一个主要角色）
  this->role = level_config.roles;

  // 设置缓冲区容量
  this->max_capacity = level_config.buffer_size[0];

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
  int *children = GlobalParams::child_map[this->local_id];
  for (int i = 0; i < num_children; i++) {
    this->downstream_node_ids.push_back(children[i]);
  }

  //========================================================================
  // IV. 执行角色专属的初始化
  //========================================================================
  switch (this->role) {
  case ROLE_DRAM: {
    // 共享模式
    unified_buffer_manager_ = new BufferManager(max_capacity);

    // 配置TaskManager
    task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
    task_manager_->Configure(GlobalParams::workload, "ROLE_DRAM");
    auto dataset = task_manager_->get_current_working_set();
    if (dataset.inputs >= 0 && dataset.weights >= 0) {
      unified_buffer_manager_->OnDataReceived(DataType::INPUT, dataset.inputs);
      unified_buffer_manager_->OnDataReceived(DataType::WEIGHT,
                                              dataset.weights);
    }

    if (dataset.outputs >= 0) {
      unified_buffer_manager_->OnDataReceived(DataType::OUTPUT,
                                              dataset.outputs);
    }

    outputs_required_count_ = task_manager_->get_current_working_set().outputs;
    outputs_received_count_ = 0;

    // 状态初始化
    dispatch_in_progress_ = false;
    logical_timestamp = 0;
    break;
  }

  case ROLE_GLB: {
    // 共享模式
    unified_buffer_manager_ = new BufferManager(max_capacity);
    current_data_size.write(unified_buffer_manager_->GetCurrentSize());

    // 配置TaskManager
    task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
    task_manager_->Configure(GlobalParams::workload, "ROLE_GLB");
    outputs_required_count_ =
        task_manager_->get_current_working_set().outputs / 2;
    outputs_received_count_ = 0;

    this->downstream_node_ids.clear();
    for (auto compute_node :
         GlobalParams::storage_to_compute_map[this->local_id]) {
      this->downstream_node_ids.push_back(compute_node);
    }

    break;
  }

  case ROLE_BUFFER: {
    // 独立模式
    std::map<DataType, size_t> type_caps;
    type_caps[DataType::INPUT] = level_config.buffer_size[0];
    type_caps[DataType::WEIGHT] = level_config.buffer_size[1];
    type_caps[DataType::OUTPUT] = level_config.buffer_size[2];
    unified_buffer_manager_ = new BufferManager(type_caps);

    current_data_size.write(unified_buffer_manager_->GetCurrentSize());

    // 配置TaskManager
    task_manager_ = std::unique_ptr<TaskManager>(new TaskManager());
    task_manager_->Configure(GlobalParams::workload, "ROLE_BUFFER");

    // 从配置中读取自驱逐参数
    const RoleProperties *props =
        task_manager_->get_properties_for_role("ROLE_BUFFER");
    if (props) {
      eviction_interval_cycles_ = props->eviction_interval_cycles;
      weight_eviction_amount_ = props->weight_eviction_amount;
    } else {
      eviction_interval_cycles_ = 0; // 默认值：不启用自驱逐
      weight_eviction_amount_ = 0;   // 默认值：不驱逐权重
    }

    outputs_required_count_ = 0;
    outputs_received_count_ = 0;
    compute_cycles = 0;

    this->upstream_node_ids.clear();
    this->upstream_node_ids.push_back(
        GlobalParams::compute_to_storage_map[this->local_id]);

    break;
  }

  case ROLE_DISTRIBUTOR: {
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
  cout << sc_time_stamp() << ": PE[" << local_id << "] configured as "
       << role_to_str(role) << " at level " << level_idx << endl;
}

int ProcessingElement::find_child_id(int id) {
  for (size_t i = 0; i < downstream_node_ids.size(); i++) {
    if (downstream_node_ids[i] == id)
      return i;
  }
}

void ProcessingElement::rxProcess() {
  // --- 0. 复位逻辑 ---
  if (reset.read()) {
    // 重置接收状态计数器
    main_receiving_size_ = 0;
    output_receiving_size_ = 0;

    for (int i = 0; i < NUM_LOCAL_PORTS; ++i) {
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
    for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++) {
      rx_buffer[vc].SetMaxBufferSize(GlobalParams::buffer_depth);
      rx_buffer[vc].setLabel(string(name()) + "->buffer[0]");
    }
    return;
  }

  if (role == ROLE_DISTRIBUTOR)
    return;

  // ==========================================================
  // 阶段一: [核心] 调用内部处理函数
  // ==========================================================
  // 在接收任何新 Flit 之前，首先尝试处理掉物理缓冲区中已有的 Flit
  internal_transfer_process();

  // ==========================================================
  // 阶段二: 接收新的入站 Flit
  // ==========================================================
  // 这部分是物理接收逻辑，负责将新来的 Flit Push 进 rx_buffer
  for (int i = 0; i < NUM_LOCAL_PORTS; ++i) {
    if (i == 0) {
      // --- Port 0: 新的支持VC的缓冲逻辑 ---

      // 物理握手：检查是否有新的请求req_rx[port_index].read() == 1 -
      // current_level
      if (req_rx[0].read() == 1 - current_level_rx[0]) {

        // 读取 Flit
        Flit flit = flit_rx[0].read();

        // 获取 VC ID
        int vc_id = flit.vc_id;

        // 检查 VC ID 是否有效
        if (vc_id >= 0 && vc_id < MAX_VIRTUAL_CHANNELS) {

          // 推入 BufferBank 中对应的 VC 缓冲区
          // 使用断言确保缓冲区未满（通过流控机制保证）
          assert(!rx_buffer[vc_id].IsFull() &&
                 "VC buffer should not be full due to backpressure");

          rx_buffer[vc_id].Push(flit);

          // 确认握手：翻转 current_level 并写入 ack_rx
          current_level_rx[0] = 1 - current_level_rx[0];
          ack_rx[0].write(current_level_rx[0]);

          // 调试日志
          std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                    << "[RX_PORT0] Received Flit on VC " << vc_id
                    << " src_id=" << flit.src_id << " dst_id=" << flit.dst_id
                    << " flit_type=" << flit.flit_type
                    << " buffer_size=" << rx_buffer[vc_id].Size()
                    << " flit_data_type=" << DataType_to_str(flit.data_type)
                    << " flit_seq_no=" << flit.sequence_no
                    << " flit_command=" << flit.command << std::endl;
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
  for (int i = 0; i < NUM_LOCAL_PORTS; ++i) {
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
    Buffer &vc_buffer = rx_buffer[vc];

    // 流式处理循环：处理该VC中所有可以处理的Flit
    while (!vc_buffer.IsEmpty()) {
      // 窥视队首Flit
      const Flit &flit = vc_buffer.Front();

      if (flit.flit_type == FLIT_TYPE_HEAD) {
        // [特殊处理] 如果是回传包 (command == -1)，跳过流控检查
        if (flit.command == -1) {
          // 回传包无条件接受（假设已预留空间）
          vc_buffer.Pop();

          std::cout
              << "@" << sc_time_stamp() << " [" << name() << "]: "
              << "[INTERNAL_TRANSFER] Accepted OUTPUT_RETURN HEAD Flit on VC "
              << vc << " src_id=" << flit.src_id
              << " payload=" << flit.payload_data_size
              << " command_id=" << flit.command << std::endl;
          continue; // 继续处理下一个flit
        }

        // 正常数据包的流控检查
        size_t *receiving_size = nullptr;

        if (flit.data_type == DataType::OUTPUT) {
          receiving_size = &output_receiving_size_;
        } else {
          receiving_size = &main_receiving_size_;
        }

        // 执行关键的流控决策
        size_t required_capacity =
            unified_buffer_manager_->GetCurrentSize(flit.data_type) +
            *receiving_size + flit.payload_data_size;

        if (required_capacity <=
            unified_buffer_manager_->GetCapacity(flit.data_type)) {
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
                    << " command_id=" << flit.command << std::endl;
        } else {
          // 逻辑缓冲区空间不足，阻塞当前VC
          std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                    << "[INTERNAL_TRANSFER] HEAD Flit BLOCKED on VC " << vc
                    << " required=" << required_capacity << " capacity="
                    << unified_buffer_manager_->GetCapacity(flit.data_type)
                    << std::endl;
          break;
        }
      } else if (flit.flit_type == FLIT_TYPE_BODY) {
        // BODY Flit无条件丢弃
        vc_buffer.Pop();
        std::cout
            << "@" << sc_time_stamp() << " [" << name() << "]: "
            << "[INTERNAL_TRANSFER] Processed OUTPUT_RETURN body Flit on VC "
            << vc << " src_id=" << flit.src_id
            << " payload=" << flit.payload_data_size
            << "seq_no=" << flit.sequence_no << std::endl;
      } else if (flit.flit_type == FLIT_TYPE_TAIL) {
        // [核心修改] 特殊处理回传包
        if (flit.command == -1) {
          // 这是一个输出回传包，只更新计数器，不调用BufferManager
          outputs_received_count_ += flit.payload_data_size;
          // assert(outputs_received_count_ <= outputs_required_count_ &&
          //        "Received more outputs than required");

          vc_buffer.Pop();

          std::cout
              << "@" << sc_time_stamp() << " [" << name() << "]: "
              << "[INTERNAL_TRANSFER] Processed OUTPUT_RETURN TAIL Flit on VC "
              << vc << " src_id=" << flit.src_id
              << " payload=" << flit.payload_data_size
              << " total_outputs_received=" << outputs_received_count_ << "/"
              << outputs_required_count_ << std::endl;

          // 通知可能等待输出的逻辑
          buffer_state_changed_event.notify(SC_ZERO_TIME);
          continue;
        }

        // 正常数据包的TAIL处理
        size_t *receiving_size = nullptr;

        if (flit.data_type == DataType::OUTPUT) {
          receiving_size = &output_receiving_size_;
        } else {
          receiving_size = &main_receiving_size_;
        }

        // 调用OnDataReceived将数据正式"入库"
        unified_buffer_manager_->OnDataReceived(flit.data_type,
                                                flit.payload_data_size);
        // 释放预留的空间
        assert(*receiving_size >= flit.payload_data_size &&
               "Receiving size underflow");
        *receiving_size -= flit.payload_data_size;

        vc_buffer.Pop();

        // 通知计算逻辑
        buffer_state_changed_event.notify(SC_ZERO_TIME);

        std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                  << "[RECEIVE_EVENT] Processed TAIL Flit on VC " << vc
                  << " src_id=" << flit.src_id
                  << " type=" << DataType_to_str(flit.data_type)
                  << " payload=" << flit.payload_data_size
                  << " committed_to_logic_buffer"
                  << "buffer_size="
                  << unified_buffer_manager_->GetCurrentSize(flit.data_type)
                  << "/" << unified_buffer_manager_->GetCapacity(flit.data_type)
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
    Packet &packet_to_send = packet_queues_[vc].front();

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
              << " src=" << flit_to_send.src_id
              << " dst=" << flit_to_send.dst_id
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
  for (const auto &q : packet_queues_) {
    if (!q.empty()) {
      return false;
    }
  }
  return true;
}

int ProcessingElement::get_vc_id_for_packet(const Packet &pkt) const {
  if (pkt.data_type == DataType::WEIGHT) {
    return 0; // 权重数据使用VC 0
  } else if (pkt.data_type == DataType::INPUT) {
    return 1; // 输入数据使用VC 1
  } else if (pkt.data_type == DataType::OUTPUT) {
    return 2; // 输出数据使用VC 2
  } else {
    return 0; // 默认VC 0
  }
}

int ProcessingElement::get_vc_id_for_packet_by_task(
    DataDispatchInfo task) const {
  // 根据数据类型分配VC ID的辅助函数
  if (task.type == DataType::WEIGHT) {
    return 0; // 权重数据使用VC 0
  } else if (task.type == DataType::INPUT) {
    return 1; // 输入数据使用VC 1
  } else if (task.type == DataType::OUTPUT) {
    return 2; // 输出数据使用VC 2
  } else {
    return 0; // 默认VC 0
  }
}

void ProcessingElement::txProcess() {
  // 复位逻辑保持不变（更新以清空新的VC队列）
  if (reset.read()) {
    req_tx[0].write(0);
    current_level_tx[0] = 0;
    compute_in_progress_ = false;
    is_compute_complete = false;
    compute_cycles = 0;

    // 清空所有VC队列
    for (auto &q : packet_queues_) {
      while (!q.empty())
        q.pop();
    }
    return;
  }

  if (role == ROLE_DISTRIBUTOR)
    return;

  // --- 步骤 A: 如果需要，智能地生成Packet ---
  // 只有在所有VC队列为空时，才尝试生成新的Packet
  // 根据角色调用生成逻辑。
  // run_storage_logic 内部已经包含了所有"意图"和"能力"的匹配检查。
  // 如果条件不满足，它什么也不会做，VC队列依然为空。
  if (role == ROLE_DRAM || role == ROLE_GLB) {
    run_storage_logic();
  }

  else if (role == ROLE_BUFFER) {
    run_compute_logic();
  }

  // --- 步骤 B: [核心替换] 调用新的统一发送处理器 ---
  handle_tx_for_all_vcs();

  if (role != ROLE_BUFFER &&
      task_manager_->is_in_sync_points(logical_timestamp) &&
      outputs_received_count_ < outputs_required_count_) {
    return;
  }

  if (role != ROLE_DRAM &&
      logical_timestamp == task_manager_->get_total_timesteps() - 1 &&
      outputs_received_count_ < outputs_required_count_ * 2) {
    return;
  }

  if (role != ROLE_BUFFER && current_dispatch_task_.sub_tasks.empty() &&
      packet_queues_are_empty() && dispatch_in_progress_) {
    if (task_manager_->is_in_sync_points(logical_timestamp)) {
      outputs_received_count_ -= outputs_required_count_;
    }
    logical_timestamp++;
    dispatch_in_progress_ = false;
    cout << sc_time_stamp() << ": PE[" << local_id
         << "] Completed dispatch for timestamp " << logical_timestamp - 1
         << endl;
    if (logical_timestamp >= task_manager_->get_total_timesteps()) {
      reset_logic();
    }
  }

  if (role == ROLE_BUFFER && is_compute_complete == true) {
    logical_timestamp++;
    is_compute_complete = false;
    cout << sc_time_stamp() << ": PE[" << local_id
         << "] Completed compute for cycle " << compute_cycles
         << " compute latency is " << task_manager_->get_compute_latency()
         << endl;
    compute_cycles++;

    // 基于计算周期数判断是否需要自驱逐
    if (eviction_interval_cycles_ > 0 && compute_cycles > 0 &&
        compute_cycles % eviction_interval_cycles_ == 0) {
      evict_weights_self();
    }
    if (logical_timestamp >= task_manager_->get_total_timesteps()) {
      reset_logic();
    }
  }
}

void ProcessingElement::reset_logic() {
  if (role == ROLE_DRAM) {
    dbg(sc_time_stamp(), "All tasks completed quiting simulation");
    // sc_stop();
    return;
  }

  if ((role == ROLE_GLB || role == ROLE_BUFFER) && !pending_commands_.empty()) {

    auto it = pending_commands_.begin();
    int command = it->second;
    pending_commands_.erase(it);
    DataDelta cmd;
    if (command >= task_manager_->get_command_count()) {
      cmd = task_manager_->get_current_working_set();
    } else {
      cmd = task_manager_->get_command_definition(command);
    }
    unified_buffer_manager_->RemoveData(DataType::WEIGHT, cmd.weights);
    unified_buffer_manager_->RemoveData(DataType::INPUT, cmd.inputs);
    if (cmd.outputs > 0) {
      Packet pkt;
      pkt.src_id = local_id;
      pkt.payload_data_size = cmd.outputs;
      pkt.data_type = DataType::OUTPUT;
      int bandwidth_scale =
          GlobalParams::hierarchical_config.get_level_config(level_index - 1)
              .bandwidth /
          GlobalParams::word_bits;
      pkt.size = pkt.flit_left =
          (cmd.outputs + bandwidth_scale - 1) / bandwidth_scale + 2;
      pkt.command = -1; // 表示这是一个回送包
      pkt.vc_id = 2;    // 回送包使用vc 0

      pkt.target_role = static_cast<PE_Role>(static_cast<int>(role) - 1);
      // dbg(sc_time_stamp(), name(), "[RESET_LOGIC] Generating output return
      // Packet to PE " + std::to_string(pkt.dst_id) +
      //     " for " + std::to_string(cmd.outputs) + " bytes.");
      std::cout << "@ " << sc_time_stamp() << " [" << name() << "]: "
                << "[RESET_LOGIC] Generating output return Packet to PE "
                << pkt.dst_id << " for " << cmd.outputs << " bytes."
                << std::endl;

      packet_queues_[pkt.vc_id].push(pkt);
    }
    unified_buffer_manager_->RemoveData(DataType::OUTPUT, cmd.outputs);

    LOG << sc_time_stamp() << ": PE[" << local_id << "]"
        << unified_buffer_manager_->GetCurrentSize() << "/"
        << unified_buffer_manager_->GetCapacity()
        << " bytes remaining after resetting for timestamp "
        << logical_timestamp << endl;
    output_buffer_state_changed_event.notify();
  }

  logical_timestamp = 0;
  outputs_received_count_ = 0;
}

void ProcessingElement::run_compute_logic() {

  if (role != ROLE_BUFFER) {
    return;
  }

  if (!compute_in_progress_) {
    // 4. Dependency Check: Verify that all required data for the *current*
    // timestep's computation is present. A compute task's dependencies are
    // its required Inputs and Weights.
    const auto &required_data =
        task_manager_->get_working_set_for_role(role_to_str(role))
            ->get_data_map();

    for (const auto &entry : required_data) {
      DataType type = entry.first;
      size_t size = entry.second;

      // For a compute PE, the working set should only define dependencies
      // (Inputs, Weights), not Outputs.
      if (type == DataType::INPUT || type == DataType::WEIGHT) {
        if (!unified_buffer_manager_->AreDataTypeReady(type, size)) {
          // cout<< sc_time_stamp() << ": PE[" << local_id << "] Waiting for "
          //     << size << " bytes of " << DataType_to_str(type)
          //     << " to start computation at cycle " << compute_cycles <<
          //     endl;
          // If any dependency is not met, we cannot start the computation
          // yet.
          return;
        }
      }

      if (type == DataType::OUTPUT) {
        if (!unified_buffer_manager_->AreDataTypeReady(type, size)) {
          return;
        }
      }
    }
    consume_cycles_left = task_manager_->get_compute_latency();
    compute_in_progress_ = true;
  }

  if (compute_in_progress_) {
    consume_cycles_left--;
    assert(consume_cycles_left >= 0 && "Consume cycles underflow");

    if (consume_cycles_left == 0) {
      compute_in_progress_ = false;
      is_compute_complete = true;
    }
  }
}

std::string ProcessingElement::role_to_str(const PE_Role &role) {
  switch (role) {
  case ROLE_DRAM:
    return "ROLE_DRAM";
  case ROLE_GLB:
    return "ROLE_GLB";
  case ROLE_BUFFER:
    return "ROLE_BUFFER";
  default:
    return "UNKNOWN";
  }
}

// 计算从当前层到目标层之间的实际目标数量
int ProcessingElement::calculate_target_count(int current_level,
                                              DataType data_type,
                                              PE_Role target_role) {
  int target_count = 1;

  // 从当前层开始，遍历到目标层之前
  for (int level = current_level;; level++) {
    const LevelConfig &level_config =
        GlobalParams::hierarchical_config.get_level_config(level);

    // 检查是否到达目标层
    if (level_config.roles == target_role) {
      break; // 到达目标层，停止累乘
    }

    // 检查该层是否有路由模式配置
    if (level_config.has_routing_patterns) {
      auto it = level_config.routing_patterns.find(data_type);
      if (it != level_config.routing_patterns.end()) {
        // 累乘该数据类型的port_groups数量
        target_count *= it->second.port_groups.size();
      }
    }
  }

  return target_count;
}

// in PE.cpp
void ProcessingElement::run_storage_logic() {
  if (role != ROLE_GLB && role != ROLE_DRAM) {
    return;
  }

  if (logical_timestamp >= task_manager_->get_total_timesteps()) {
    all_transfer_tasks_finished = true;
    return;
  }

  if (!dispatch_in_progress_) {
    std::map<DataType, size_t> data_map =
        task_manager_->get_working_set_for_role(role_to_str(role))
            ->get_data_map();
    for (const auto &entry : data_map) {
      if (entry.first != DataType::OUTPUT) {
        if (!unified_buffer_manager_->AreDataTypeReady(entry.first,
                                                       entry.second))
          return;
      } else {
        if (!unified_buffer_manager_->AreDataTypeReady(entry.first,
                                                       entry.second))
          return;
      }
    }

    current_dispatch_task_ =
        task_manager_->get_task_for_timestep(logical_timestamp);
    std::cout << sc_time_stamp() << ": PE[" << local_id
              << "] Starting dispatch for timestep " << logical_timestamp
              << " with " << current_dispatch_task_.sub_tasks.size()
              << " subtasks." << std::endl;
    command_to_send = get_command_to_send();

    dispatch_in_progress_ = true;
  }

  if (current_dispatch_task_.sub_tasks.empty()) {
    return;
  }

  // --- 1. [核心] 从待办列表中随机选择一个子任务 ---
  int num_pending_subtasks = current_dispatch_task_.sub_tasks.size();
  auto it = current_dispatch_task_.sub_tasks.begin();

  while (it != current_dispatch_task_.sub_tasks.end()) {
    DataDispatchInfo &selected_task = *it;
    // 计算此数据类型对应的VC ID
    int vc_id = get_vc_id_for_packet_by_task(selected_task);

    // 检查对应的VC队列是否为空（实现"size只为1"的规则）
    if (!packet_queues_[vc_id].empty()) {
      ++it;
      continue; // 该VC通道忙，跳过
    }

    std::cout << sc_time_stamp() << ": PE[" << local_id
              << "] Generating packet for task: "
              << "Type=" << DataType_to_str(selected_task.type)
              << ", Size=" << selected_task.size << std::endl;

    Packet pkt;
    pkt.src_id = local_id;
    pkt.target_role = selected_task.target_role;

    // 计算实际的目标数量
    int target_count = calculate_target_count(level_index, selected_task.type,
                                              selected_task.target_role);

    int bandwidth_scale =
        GlobalParams::hierarchical_config.get_level_config(level_index)
            .bandwidth /
        GlobalParams::word_bits;
    pkt.vc_id = vc_id; // 使用预先计算的VC ID
    pkt.payload_data_size = selected_task.size;
    pkt.logical_timestamp = logical_timestamp;
    pkt.data_type = selected_task.type;

    if (target_count > 1) {
      // 多目标场景：使用当前层实际带宽
      int current_bandwidth =
          GlobalParams::hierarchical_config.get_level_config(level_index)
              .bandwidth;

      // 步骤A：计算单层切片是否溢出
      double R = (double)(target_count * 8) / current_bandwidth;

      // 步骤B：确定单层Flit数（向上取整）
      int Nslice;
      if (R <= 1.0) {
        Nslice = 1;
      } else {
        Nslice = (int)ceil(R);
      }

      // 步骤C：计算总长度
      pkt.size = pkt.flit_left = Nslice * selected_task.size + 2;
    } else {
      // 单目标场景：保持原有计算
      pkt.size = pkt.flit_left =
          (selected_task.size + bandwidth_scale - 1) / bandwidth_scale + 2;
    }
    pkt.command = command_to_send;

    // 将Packet推入对应的VC队列
    packet_queues_[vc_id].push(pkt);
    it = current_dispatch_task_.sub_tasks.erase(it);
  }
}

int ProcessingElement::get_command_to_send() // tofix
                                             // 当完整模拟时需要通过获取子
{
  PE_Role nextRole = static_cast<PE_Role>(static_cast<int>(role) + 1);
  auto commands = task_manager_->get_commands_for_role(role_to_str(nextRole));
  if (commands == nullptr || commands->empty()) {
    dbg(sc_time_stamp(), name(),
        "[CMD_ERROR] No command definitions found for downstream role: " +
            role_to_str(nextRole));
    assert(false && "cannot find available command");
    return -2; // 返回无效命令
  }

  // if (role == ROLE_GLB &&
  //     logical_timestamp + 1 >= task_manager_->get_total_timesteps() &&
  //     task_manager_->get_command_definition(pending_commands_.begin()->second)
  //             .weights > task_manager_->get_current_working_set().weights) {
  //   return commands->size() + 1;
  // }
  DataDelta next_delta;
  DispatchTask next_task = task_manager_->get_task_for_timestep(
      (logical_timestamp + 1) % task_manager_->get_total_timesteps());
  // 这条命令需要在负载下被测试

  for (const DataDispatchInfo &sub_task : next_task.sub_tasks) {
    // 不再使用 target_ids，multicast_factor 设为 1
    int multicast_factor = 1;
    if (sub_task.type == DataType::WEIGHT)
      next_delta.weights += sub_task.size * multicast_factor;
    if (sub_task.type == DataType::INPUT)
      next_delta.inputs += sub_task.size * multicast_factor;
    if (sub_task.type == DataType::OUTPUT)
      next_delta.outputs += sub_task.size * multicast_factor;
  }

  for (const auto &cmd : *commands) {
    // 比较推算出的 payload 和命令中定义的 payload
    if (cmd.evict_payload.inputs == next_delta.inputs &&
        cmd.evict_payload.outputs == next_delta.outputs) {
      // 找到了完全匹配的命令！
      return cmd.command_id;
    }
  }
  assert(false && "cannot find available command");
  return -2;
}
Flit ProcessingElement::generate_next_flit_from_queue(
    std::queue<Packet> &queue) {
  Flit flit;
  Packet packet = queue.front();

  // 填充公共字段
  flit.src_id = packet.src_id;
  flit.vc_id = packet.vc_id;
  flit.logical_timestamp = packet.logical_timestamp;
  flit.sequence_no = packet.size - packet.flit_left;
  flit.sequence_length = packet.size;
  flit.hop_no = 0;
  flit.payload_data_size = packet.payload_data_size;
  flit.hub_relay_node = NOT_VALID;
  flit.data_type = packet.data_type;
  flit.command = packet.command;

  flit.target_role = packet.target_role;

  // 确定flit类型
  if (packet.size == packet.flit_left) {
    flit.flit_type = FLIT_TYPE_HEAD;
    std::copy(std::begin(packet.payload_sizes), std::end(packet.payload_sizes),
              flit.payload_sizes);
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

unsigned int ProcessingElement::getQueueSize() const {
  return packet_queues_.size();
}
void ProcessingElement::evict_weights_self() {
  size_t current_weights =
      unified_buffer_manager_->GetCurrentSize(DataType::WEIGHT);

  if (current_weights > 0) {
    size_t evict_amount = std::min(weight_eviction_amount_, current_weights);
    unified_buffer_manager_->RemoveData(DataType::WEIGHT, evict_amount);

    std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
              << "[SELF_EVICT] Evicted " << evict_amount
              << " weights at compute cycle " << compute_cycles
              << ", remaining: "
              << unified_buffer_manager_->GetCurrentSize(DataType::WEIGHT)
              << std::endl;

    buffer_state_changed_event.notify(SC_ZERO_TIME);
  }
}
