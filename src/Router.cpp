/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the router
 */

#include "Router.h"
#include <dbg.h>
#include <systemc.h>

inline int toggleKthBit(int n, int k) { return (n ^ (1 << (k - 1))); }

void Router::process()
{
  txProcess();
  rxProcess();
}

void Router::rxProcess()
{
  if (reset.read())
  {
    TBufferFullStatus bfs;
    // Clear outputs and indexes of receiving protocol
    for (size_t i = 0; i < all_flit_rx.size(); i++)
    {
      all_ack_rx[i]->write(0);
      current_level_rx[i] = 0;
      all_buffer_full_status_rx[i]->write(bfs);
    }
    routed_flits = 0;
    local_drained = 0;
  }
  else
  {
    // This process simply sees a flow of incoming flits. All arbitration
    // and wormhole related issues are addressed in the txProcess()
    // assert(false);
    for (size_t i = 0; i < all_flit_rx.size(); i++)
    {
      // To accept a new flit, the following conditions must match:
      // 1) there is an incoming request
      // 2) there is a free slot in the input buffer of direction i
      // LOG<<"****RX****DIRECTION ="<<i<<  endl;
      if (i == 1)
      {
        LOG << " h_flit_rx_down[0] req=" << all_req_rx[i]->read()
            << " current_level_rx=" << current_level_rx[i] << endl;
      }

      if (all_req_rx[i]->read() == 1 - current_level_rx[i])
      {
        Flit received_flit = all_flit_rx[i]->read();

        // LOG<<"request opposite to the current_level, reading flit
        // "<<received_flit<<endl;

        int vc = received_flit.vc_id;
        assert(buffers[i] != nullptr && "Pointer to BufferBank is null!");
        if (!(*buffers[i])[vc].IsFull())
        {

          if (use_predefined_routing &&
              routing_patterns.count(received_flit.data_type) > 0 && received_flit.command != -1)
          {

            const RoutingPattern &pattern =
                routing_patterns[received_flit.data_type];
            if (received_flit.target_role != role)
              received_flit.forward_count = pattern.forward_count; // 从配置获取                   // 重置计数
          }

          received_flit.current_forward = 0;

          (*buffers[i])[vc].Push(received_flit);
          LOG << " Flit " << received_flit << " " << received_flit.flit_type
              << " collected from Input[" << i << "][" << vc << "]" << endl;
          std::cout << "@" << sc_time_stamp() << " [" << name() << "]: "
                    << "[RX_PORT0] Received Flit on VC " << vc
                    << " src_id=" << received_flit.src_id
                    << " dst_id=" << received_flit.dst_id
                    << " flit_type=" << received_flit.flit_type
                    << " buffer_size=" << (*buffers[i])[vc].Size()
                    << " flit_data_type="
                    << DataType_to_str(received_flit.data_type)
                    << " flit_seq_no=" << received_flit.sequence_no
                    << " flit_command=" << received_flit.command << std::endl;

          power.bufferRouterPush();

          // Negate the old value for Alternating Bit Protocol (ABP)
          // LOG<<"INVERTING CL FROM "<< current_level_rx[i]<< " TO "<<  1 -
          // current_level_rx[i]<<endl;
          current_level_rx[i] = 1 - current_level_rx[i];

          // if a new flit is injected from local PE
          if (received_flit.src_id == local_id)
            power.networkInterface();
        }

        else // buffer full
        {
          // should not happen with the new TBufferFullStatus control signals
          // except for flit coming from local PE, which don't use it
          LOG << " Flit " << received_flit << " buffer full Input[" << i << "]["
              << vc << "]" << endl;
          std::cout << "buffer_full_status_rx[" << i << "].mask[" << vc
                    << "] = " << all_buffer_full_status_rx[i]->read().mask[vc]
                    << std::endl;
          assert(port_info_map[i].type == PORT_LOCAL);
        }
      }
      all_ack_rx[i]->write(current_level_rx[i]);
      // updates the mask of VCs to prevent incoming data on full buffers
      TBufferFullStatus bfs;
      for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
      {
        bfs.mask[vc] = (*buffers[i])[vc].IsFull();
      }
      all_buffer_full_status_rx[i]->write(bfs);
    }
  }
}

vector<vector<int>>
Router::getCurrentPortGroups(int forward_count, int current_forward,
                             const vector<vector<int>> &all_groups)
{
  vector<vector<int>> current_groups;

  // 计算每批发送的group数量
  int batch_size = (all_groups.size() + forward_count - 1) / forward_count;

  // 计算当前批次的起始和结束索引
  int start_idx = current_forward * batch_size;
  int end_idx = min(start_idx + batch_size, (int)all_groups.size());

  // 提取当前批次的groups
  for (int i = start_idx; i < end_idx; i++)
  {
    current_groups.push_back(all_groups[i]);
  }

  return current_groups;
}

void Router::txProcess()
{

  if (reset.read())
  {
    // Clear outputs and indexes of transmitting protocol
    for (size_t i = 0; i < all_flit_tx.size(); i++)
    {
      all_req_tx[i]->write(0);
      current_level_tx[i] = 0;
    }
    reservation_table.reset();
  }
  else
  {

    for (size_t j = 0; j < all_flit_rx.size(); j++)
    {
      size_t i = (start_from_port + j) % all_flit_rx.size();

      for (int k = 0; k < GlobalParams::n_virtual_channels; k++)
      {

        int vc = (start_from_vc[i] + k) % (GlobalParams::n_virtual_channels);

        // Uncomment to enable deadlock checking on buffers.
        // Please also set the appropriate threshold.
        // (*buffers[i]).deadlockCheck();

        if (!(*buffers[i])[vc].IsEmpty())
        {

          Flit flit = (*buffers[i])[vc].Front();
          power.bufferRouterFront();

          if (port_info_map[i].type == PORT_DOWN && vc == return_vc_id &&
              is_aggregation)
          {
            if (tryAggregation(i, flit))
            {
              (*buffers[i])[vc].Pop();
              power.bufferRouterPop();
            }
            // 如果返回false(重复添加或属性不匹配),不弹出flit
            continue;
          }

          if (flit.flit_type == FLIT_TYPE_HEAD && flit.current_forward == 0)
          {
            // 统一准备路由数据
            RouteData route_data;
            route_data.current_id = local_id;
            route_data.src_id = flit.src_id;
            // route_data.dst_ids = flit.dst_ids; // 删除：不再使用dst_ids
            route_data.dir_in = i;
            route_data.vc_id = flit.vc_id;
            route_data.is_output = flit.is_output;
            route_data.data_type = flit.data_type;
            route_data.target_role = flit.target_role;
            route_data.command = flit.command;

            // 统一调用route()获取输出端口
            vector<int> output_ports = route(route_data);

            // 调试输出
            cout << "Router " << local_id << " route from input " << i
                 << " to outputs: ";
            for (size_t idx = 0; idx < output_ports.size(); idx++)
            {
              cout << output_ports[idx]
                   << (idx < output_ports.size() - 1 ? ", " : "");
            }
            cout << endl;

            // 处理Hub中继的特殊情况
            if (output_ports.size() == 1 &&
                output_ports[0] >= DIRECTION_HUB_RELAY)
            {
              Flit f = (*buffers[i])[vc].Pop();
              f.hub_relay_node = output_ports[0] - DIRECTION_HUB_RELAY;
              (*buffers[i])[vc].Push(f);
              output_ports[0] = DIRECTION_HUB;
            }

            // 统一的预留逻辑
            TReservation r;
            r.input = i;
            r.vc = vc;

            LOG << " checking availability of Output(s) for Input[" << i << "]["
                << vc << "] flit " << flit << endl;

            int reservation_status =
                reservation_table.checkReservation(r, output_ports);

            if (reservation_status == RT_AVAILABLE)
            {
              LOG << " reserving outputs for flit " << flit << endl;
              reservation_table.reserve(r, output_ports);

              // // 建立output到dst_ids的映射(用于分裂转发)
              // map<int, set<int>> output_to_dsts = buildOutputMapping(flit,
              // output_ports, i); reservation_table.setOutputMapping(r.input,
              // r.vc, output_to_dsts);
            }
            else if (reservation_status == RT_ALREADY_SAME)
            {
              LOG << " RT_ALREADY_SAME reserved outputs for flit " << flit
                  << endl;
            }
            else if (reservation_status == RT_OUTVC_BUSY)
            {
              LOG << " RT_OUTVC_BUSY reservation for flit " << flit << endl;
            }
            else if (reservation_status == RT_ALREADY_OTHER_OUT)
            {
              LOG << "RT_ALREADY_OTHER_OUT: another outputs previously "
                     "reserved for the same flit"
                  << endl;
            }
            else
            {
              assert(false);
            }
          }
        }
      }
      start_from_vc[i] =
          (start_from_vc[i] + 1) % GlobalParams::n_virtual_channels;
    }

    start_from_port = (start_from_port + 1) % all_flit_rx.size();

    if (aggregation_entry.port_flits.size() ==
            aggregation_entry.expected_port_count &&
        is_aggregation)
    {
      cout << name() << " " << sc_time_stamp() << " All "
           << aggregation_entry.expected_port_count
           << " downstream ports ready, triggering aggregation" << endl;

      if (performAggregation())
      {
        aggregation_entry.port_flits.clear();
      }
    }

    //==================================================================
    // 2nd phase: Two-Phase Arbitration & Atomic Forwarding
    // 阶段A: 候选筛选 - 收集所有准备就绪的VC
    //==================================================================
    struct ForwardCandidate
    {
      int input;
      int vc;
      vector<int> target_outputs;
    };

    vector<ForwardCandidate> candidates;

    for (int i = 0; i < all_flit_rx.size(); i++)
      for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
      {
        if ((*buffers[i])[vc].IsEmpty())
          continue;

        auto target_outputs = reservation_table.getReservations(i, vc);

        if (target_outputs.empty())
          continue;

        bool all_outputs_ready = true;
        for (int output_port : target_outputs)
        {
          if (current_level_tx[output_port] !=
                  all_ack_tx[output_port]->read() ||
              all_buffer_full_status_tx[output_port]->read().mask[vc] == 1)
          {
            all_outputs_ready = false;
            break;
          }
        }
        if (all_outputs_ready)
        {
          candidates.push_back({i, vc, target_outputs});
        }
      }

    if (!aggregated_flit_queue.empty())
    {
      auto target_outputs = reservation_table.getReservations(-1, return_vc_id);
      bool all_outputs_ready = true;
      for (int output_port : target_outputs)
      {
        if (current_level_tx[output_port] != all_ack_tx[output_port]->read() ||
            all_buffer_full_status_tx[output_port]->read().mask[return_vc_id] ==
                1)
        {
          all_outputs_ready = false;
          break;
        }
      }
      if (all_outputs_ready)
      {
        candidates.push_back({-1, return_vc_id, target_outputs});
      }
    }

    //==================================================================
    // 阶段B: 仲裁与原子转发
    //==================================================================
    if (!candidates.empty())
    {
      // 用于跟踪已使用的资源
      std::set<int> used_inputs;
      std::set<int> used_outputs;

      while (!candidates.empty())
      {
        // 过滤出仍然可用的候选
        vector<ForwardCandidate> available;
        for (auto &c : candidates)
        {
          // 检查 input 是否已被使用
          if (used_inputs.count(c.input))
            continue;

          // 检查所有 output 是否都未被使用
          bool output_conflict = false;
          for (int o : c.target_outputs)
          {
            if (used_outputs.count(o))
            {
              output_conflict = true;
              break;
            }
          }
          if (output_conflict)
            continue;

          // 添加到可用候选列表
          available.push_back(c);
        }

        if (available.empty())
          break; // 没有可用候选，退出循环

        // 随机选择一个候选进行仲裁
        int winner_idx = rand() % available.size();
        ForwardCandidate &selected = available[winner_idx];
        Flit flit;
        if (selected.input == -1)
        {
          flit = aggregated_flit_queue.front();
          aggregated_flit_queue.pop();
          power.bufferRouterPop();
        }
        else
        {
          Flit &flit_ref = (*buffers[selected.input])[selected.vc].FrontRef();
          // 检查是否完成所有转发
          bool should_pop = true;
          if (use_predefined_routing &&
              routing_patterns.count(flit_ref.data_type) > 0)
          {
            flit_ref.current_forward++;
            // 只有头flit和尾flit才可能复制多份
            if (flit_ref.flit_type == FLIT_TYPE_HEAD ||
                flit_ref.flit_type == FLIT_TYPE_TAIL)
            {
              const RoutingPattern &pattern = routing_patterns[flit_ref.data_type];
              if (flit_ref.current_forward < flit_ref.forward_count)
              {
                should_pop = false; // 还未完成转发，不pop
              }
            }
            // BODY flit 直接弹出，不参与复制计数
          }

          flit = (*buffers[selected.input])[selected.vc].Front();

          if (should_pop)
          {
            (*buffers[selected.input])[selected.vc].Pop();
            power.bufferRouterPop();
          }
        }

        if (flit.target_role == this->role)
        {
          int output_port = selected.target_outputs[0];
          all_flit_tx[output_port]->write(flit);
          current_level_tx[output_port] = 1 - current_level_tx[output_port];
          all_req_tx[output_port]->write(current_level_tx[output_port]);
        }
        else if (selected.input == -1 || flit.command == -1)
        {
          int output_port = selected.target_outputs[0];
          all_flit_tx[output_port]->write(flit);
          current_level_tx[output_port] = 1 - current_level_tx[output_port];
          all_req_tx[output_port]->write(current_level_tx[output_port]);
        }

        // 在转发阶段,检查是否使用预定义路由
        else if (use_predefined_routing &&
                 routing_patterns.count(flit.data_type) > 0)
        {
          const RoutingPattern &pattern = routing_patterns[flit.data_type];
          vector<vector<int>> current_groups =
              getCurrentPortGroups(flit.forward_count, flit.current_forward - 1,
                                   pattern.port_groups);

          // 关键判断:port_groups 的数量决定是否分裂
          bool need_split = (current_groups.size() > 1);

          if (need_split)
          {
            // 分裂模式:为每个 port_group 创建独立的 flit
            for (const vector<int> &group : current_groups)
            {
              Flit split_flit = flit;

              // 根据组的大小决定转发方式
              if (group.size() == 1)
              {
                // 单播到该端口
                all_flit_tx[group[0]]->write(split_flit);
                current_level_tx[group[0]] = 1 - current_level_tx[group[0]];
                all_req_tx[group[0]]->write(current_level_tx[group[0]]);
              }
              else
              {
                // 多播到该组的所有端口
                for (int port : group)
                {
                  all_flit_tx[port]->write(split_flit);
                  current_level_tx[port] = 1 - current_level_tx[port];
                  all_req_tx[port]->write(current_level_tx[port]);
                }
              }
            }
          }
          else
          {
            // 非分裂模式:单个 port_group
            const vector<int> &group = current_groups[0];

            if (group.size() == 1)
            {
              // 单播
              all_flit_tx[group[0]]->write(flit);
              current_level_tx[group[0]] = 1 - current_level_tx[group[0]];
              all_req_tx[group[0]]->write(current_level_tx[group[0]]);
            }
            else
            {
              // 多播到所有端口
              for (int port : group)
              {
                all_flit_tx[port]->write(flit);
                current_level_tx[port] = 1 - current_level_tx[port];
                all_req_tx[port]->write(current_level_tx[port]);
              }
            }
          }
        }

        // 处理 TAIL Flit 的资源释放
        if (flit.flit_type == FLIT_TYPE_TAIL)
        {
          TReservation r;
          r.input = selected.input;
          r.vc = selected.vc;
          if (
              flit.current_forward >= flit.forward_count || flit.command == -1)
            reservation_table.release(r, selected.target_outputs);

          // 功耗与统计（对所有目标端口进行统计）
          for (int output_port : selected.target_outputs)
          {
            if (output_port == DIRECTION_HUB)
            {
              power.r2hLink();
            }
            else
            {
              power.r2rLink();
            }
            power.crossBar();

            if (output_port == DIRECTION_LOCAL)
            {
              power.networkInterface();
              LOG << "Consumed flit " << flit << endl;
              stats.receivedFlit(sc_time_stamp().to_double() /
                                     GlobalParams::clock_period_ps,
                                 flit);
              if (GlobalParams::max_volume_to_be_drained)
              {
                if (drained_volume >= GlobalParams::max_volume_to_be_drained)
                {
                  sc_stop();
                }
                else
                {
                  drained_volume++;
                  local_drained++;
                }
              }
            }
            else if (selected.input != DIRECTION_LOCAL &&
                     selected.input != DIRECTION_LOCAL_2)
            {
              routed_flits++;
            }
          }
        }
        // 标记已使用的资源
        used_inputs.insert(selected.input);
        for (int o : selected.target_outputs)
        {
          used_outputs.insert(o);
        }

        // 从候选列表中移除已处理的或产生冲突的候选
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [&](const ForwardCandidate &c)
                                        {
                                          // 移除使用相同 input 的
                                          if (c.input == selected.input)
                                            return true;

                                          // 移除使用相同 output 的
                                          for (int o : c.target_outputs)
                                          {
                                            if (used_outputs.count(o))
                                              return true;
                                          }
                                          return false;
                                        }),
                         candidates.end());
      }
    }
  }
}

void Router::perCycleUpdate()
{
  if (reset.read())
  {
    return;
  }
  else
  {

    power.leakageRouter();
    for (size_t i = 0; i < all_flit_rx.size() - NUM_LOCAL_PORTS; i++)
    {
      for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
      {
        power.leakageBufferRouter();
        power.leakageLinkRouter2Router();
      }
    }

    power.leakageLinkRouter2Hub();
  }
}

vector<int> Router::routingFunction(const RouteData &route_data)
{

  // TODO: fix all the deprecated verbose mode logs
  if (GlobalParams::verbose_mode > VERBOSE_OFF)
    LOG << "Wired routing for dst = " << route_data.dst_id << endl;

  // not wireless direction taken, apply normal routing
  return routingAlgorithm->route(this, route_data);
}

// map<int, set<int>> Router::buildOutputMapping(const Flit &flit, const
// vector<int> &output_ports, int dir_in)
// {
//     map<int, set<int>> output_to_dsts;

//     // 为每个target计算路由
//     for (int target_id : flit.dst_ids)
//     {
//         RouteData route_data;
//         route_data.current_id = local_id;
//         route_data.src_id = flit.src_id;
//         route_data.dst_ids.push_back(target_id); // 单个目标
//         route_data.dir_in = dir_in;              // 不需要防环路检查
//         route_data.vc_id = flit.vc_id;
//         route_data.is_output = flit.is_output;
//         route_data.command = flit.command;

//         // 调用单播路由获取该target的output端口
//         auto output_port = route(route_data);
//         if (output_port.empty())
//             continue; // 路由失败，跳过

//         // 将target添加到对应的output端口列表中
//         output_to_dsts[output_port[0]].insert(target_id);
//     }

//     return output_to_dsts;
// }

vector<int> Router::route(const RouteData &route_data)
{
  vector<int> output_ports;

  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL)
  {
    // 核心判断: target_role 是否匹配本地角色
    if (route_data.target_role == this->role)
    {
      // 情况1: 目标是本地角色,只需本地投递
      output_ports.push_back(getLogicalPortIndex(PORT_LOCAL, 0));
      return output_ports; // 直接返回,不再转发
    }
    else
    {
      // 情况2: target_role 不是本地角色,只转发不投递

      // 子情况2a: 检查是否是回送包
      if (route_data.command == -1)
      {
        // 回送包向上发送
        int up_port_index = getLogicalPortIndex(PORT_UP, -1);
        assert(up_port_index != -1 &&
               "No UP port found in hierarchical router for return packet");
        if (local_level > 0)
        {
          output_ports.push_back(up_port_index);
        }
      }
      // 子情况2b: 使用预定义路由转发
      else if (use_predefined_routing &&
               routing_patterns.count(route_data.data_type) > 0)
      {
        const RoutingPattern &pattern = routing_patterns[route_data.data_type];
        for (const vector<int> &group : pattern.port_groups)
        {
          for (int port : group)
          {
            output_ports.push_back(port);
          }
        }
      }
      // 注意: 移除了动态路由的回退逻辑
    }
  }
  assert(!output_ports.empty() &&
         "No output ports determined in hierarchical routing");

  return output_ports;
}

vector<int> Router::getMulticastChildren(const vector<int> &dst_ids)
{
  vector<int> child_targets;

  for (int dst_id : dst_ids)
  {
    if (dst_id != this->local_id && isDescendant(dst_id))
    {
      int next_hop_child = getNextHopNode(dst_id);

      // 检查这个目标节点是否已经是我们的直接子节点
      bool is_direct_child = false;
      for (int i = 0; i < GlobalParams::fanouts_per_level
                              [GlobalParams::node_level_map[local_id]];
           i++)
      {
        if (GlobalParams::child_map[local_id][i] == dst_id)
        {
          is_direct_child = true;
          break;
        }
      }

      // 如果目标是直接子节点，直接添加目标节点
      // 如果目标是孙子或更深层节点，添加下一跳子节点
      int child_to_add = is_direct_child ? dst_id : next_hop_child;

      // 避免重复添加相同的子节点
      if (find(child_targets.begin(), child_targets.end(), child_to_add) ==
          child_targets.end())
      {
        child_targets.push_back(child_to_add);
      }
    }
  }

  return child_targets;
}

int Router::selectionFunction(const vector<int> &directions,
                              const RouteData &route_data)
{
  // Hierarchical mode: simple selection
  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL)
  {
    // In hierarchical topology, routing function should return only one
    // direction If multiple directions are returned (shouldn't happen), pick
    // the first one
    if (directions.size() > 0)
    {
      return directions[0];
    }
    return NOT_VALID;
  }
  return -1; // always return the first direction
}

void Router::configure(const int _id, const int _level,
                       const double _warm_up_time,
                       const unsigned int _max_buffer_size,
                       GlobalRoutingTable &grt)
{
  local_id = _id;
  local_level = _level;
  stats.configure(_id, _warm_up_time);
  return_vc_id = 2;
  is_aggregation =
      GlobalParams::hierarchical_config.get_level_config(local_level).aggregate;
  aggregation_entry.expected_port_count =
      GlobalParams::fanouts_per_level[local_level];
  int down_port_offset =
      (local_level > 0) ? 1 + NUM_LOCAL_PORTS : NUM_LOCAL_PORTS;

  const LevelConfig &level_config =
      GlobalParams::hierarchical_config.get_level_config(local_level);
  this->role = level_config.roles;

  if (level_config.has_routing_patterns)
  {
    this->routing_patterns = level_config.routing_patterns;

    // 直接应用offset到所有路由模式
    for (auto &pair : this->routing_patterns)
    {
      RoutingPattern &pattern = pair.second;
      for (auto &group : pattern.port_groups)
      {
        for (int &port : group)
        {
          port += down_port_offset;
        }
      }
    }
    this->use_predefined_routing = true;
  }
  start_from_port = (all_flit_rx.size() > 0)
                        ? getLogicalPortIndex(PORT_LOCAL, 0)
                        : 0; // Start from LOCAL port

  // initPorts();
  // buildUnifiedInterface();
  routingAlgorithm = RoutingAlgorithms::get(GlobalParams::routing_algorithm);

  if (routingAlgorithm == 0)
  {
    cerr << " FATAL: invalid routing -routing "
         << GlobalParams::routing_algorithm << ", check with noxim -help"
         << endl;
    exit(-1);
  }
  if (grt.isValid())
    routing_table.configure(grt, _id);

  reservation_table.setSize(all_flit_rx.size());

  for (size_t i = 0; i < all_flit_rx.size(); i++)
  {
    for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
    {
      (*buffers[i])[vc].SetMaxBufferSize(_max_buffer_size);
      (*buffers[i])[vc].setLabel(string(name()) + "->buffer[" + i_to_string(i) +
                                 "]");
    }
    start_from_vc[i] = 0;
  }
}

bool Router::tryAggregation(int input_port, const Flit &flit)
{
  // 验证是回送包且使用正确的VC
  assert(flit.vc_id == return_vc_id && "Return packet must use designated VC");

  if (aggregation_entry.port_flits.count(input_port))
    return false;

  // 如果是第一个到达的flit,初始化聚合条目
  if (aggregation_entry.port_flits.empty())
  {
    aggregation_entry.payload_data_size = flit.payload_data_size;
    aggregation_entry.flit_type = flit.flit_type;
  }

  // 验证flit属性匹配
  if (aggregation_entry.payload_data_size != flit.payload_data_size ||
      aggregation_entry.flit_type != flit.flit_type)
  {
    assert(false && "mismatch in aggregation flit");
    return false;
  }

  // 将该端口的flit加入聚合缓冲区
  aggregation_entry.port_flits[input_port] = flit;

  // 检查是否所有下游端口都已到达
  return true;
}

bool Router::performAggregation()
{
  if (!aggregated_flit_queue.empty())
  {
    return false; // 队列中还有未转发的聚合flit,暂不聚合新的
  }
  // 创建聚合后的大flit
  Flit aggregated_flit;

  // 删除：不再合并dst_ids
  // aggregated_flit.dst_ids.insert(
  //     aggregated_flit.dst_ids.end(),
  //     f.dst_ids.begin(),
  //     f.dst_ids.end());

  // 设置聚合flit的属性
  aggregated_flit = aggregation_entry.port_flits.begin()->second;
  // aggregated_flit.payload_data_size *= aggregation_entry.expected_port_count;

  if (routing_patterns.count(aggregated_flit.data_type) > 0)
  {
    aggregated_flit.payload_data_size =
        routing_patterns[aggregated_flit.data_type].port_groups.size() *
        aggregated_flit.payload_data_size;
  }
  else
  {
    aggregated_flit.payload_data_size *= aggregation_entry.expected_port_count;
  }

  aggregated_flit.src_id = -1;

  // 路由并预留上游端口
  if (aggregated_flit.flit_type != FlitType::FLIT_TYPE_HEAD)
  {
    aggregated_flit_queue.push(aggregated_flit);
    return true; // 只需要在头flit进行预留
  }
  RouteData route_data;
  route_data.current_id = local_id;
  // route_data.dst_ids = aggregated_flit.dst_ids; // 删除：不再使用dst_ids
  route_data.src_id = -1;
  route_data.dir_in = -2;
  route_data.target_role = aggregated_flit.target_role;
  route_data.command = aggregated_flit.command;

  vector<int> output_ports = route(route_data);

  TReservation r;
  r.input = -1; // 特殊标记
  r.vc = return_vc_id;

  int reservation_status = reservation_table.checkReservation(r, output_ports);
  if (reservation_status == RT_AVAILABLE)
  {
    reservation_table.reserve(r, output_ports);
    aggregated_flit_queue.push(aggregated_flit);
    // map<int, set<int>> output_to_dsts = buildOutputMapping(aggregated_flit,
    // output_ports, -2); reservation_table.setOutputMapping(r.input, r.vc,
    // output_to_dsts);

    return true;
  }

  return false;
}
unsigned long Router::getRoutedFlits() { return routed_flits; }

int Router::reflexDirection(int direction) const
{
  if (direction == DIRECTION_NORTH)
    return DIRECTION_SOUTH;
  if (direction == DIRECTION_EAST)
    return DIRECTION_WEST;
  if (direction == DIRECTION_WEST)
    return DIRECTION_EAST;
  if (direction == DIRECTION_SOUTH)
    return DIRECTION_NORTH;

  // you shouldn't be here
  assert(false);
  return NOT_VALID;
}

int Router::getNeighborId(int _id, int direction) const
{
  assert(GlobalParams::topology == TOPOLOGY_MESH);

  Coord my_coord = id2Coord(_id);

  switch (direction)
  {
  case DIRECTION_NORTH:
    if (my_coord.y == 0)
      return NOT_VALID;
    my_coord.y--;
    break;
  case DIRECTION_SOUTH:
    if (my_coord.y == GlobalParams::mesh_dim_y - 1)
      return NOT_VALID;
    my_coord.y++;
    break;
  case DIRECTION_EAST:
    if (my_coord.x == GlobalParams::mesh_dim_x - 1)
      return NOT_VALID;
    my_coord.x++;
    break;
  case DIRECTION_WEST:
    if (my_coord.x == 0)
      return NOT_VALID;
    my_coord.x--;
    break;
  default:
    LOG << "Direction not valid : " << direction;
    assert(false);
  }

  int neighbor_id = coord2Id(my_coord);

  return neighbor_id;
}

void Router::ShowBuffersStats(std::ostream &out)
{
  for (size_t i = 0; i < all_flit_rx.size(); i++)
    for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
      (*buffers[i])[vc].ShowStats(out);
}

bool Router::connectedHubs(int src_hub, int dst_hub)
{
  vector<int> &first = GlobalParams::hub_configuration[src_hub].txChannels;
  vector<int> &second = GlobalParams::hub_configuration[dst_hub].rxChannels;

  vector<int> intersection;

  for (unsigned int i = 0; i < first.size(); i++)
  {
    for (unsigned int j = 0; j < second.size(); j++)
    {
      if (first[i] == second[j])
        intersection.push_back(first[i]);
    }
  }

  if (intersection.size() == 0)
    return false;
  else
    return true;
}
// Constructor implementation
Router::Router(sc_module_name nm)
{

  // Register SystemC methods
  SC_METHOD(rxProcess);
  sensitive << reset;
  sensitive << clock.neg();

  SC_METHOD(txProcess);
  sensitive << reset;
  sensitive << clock.pos();

  // Use proper scope resolution for SystemC method registration
  SC_METHOD(perCycleUpdate);
  sensitive << reset;
  sensitive << clock.pos();
}

// Destructor implementation
Router::~Router() { cleanupPorts(); }

// Initialize all dynamic ports based on hierarchical configuration
void Router::initPorts()
{
  // Initialize all pointers to nullptr
  h_flit_rx_up = nullptr;
  h_req_rx_up = nullptr;
  h_ack_rx_up = nullptr;
  h_buffer_full_status_rx_up = nullptr;

  h_flit_tx_up = nullptr;
  h_req_tx_up = nullptr;
  h_ack_tx_up = nullptr;
  h_buffer_full_status_tx_up = nullptr;

  for (int i = 0; i < NUM_LOCAL_PORTS; i++)
  {
    h_flit_rx_local[i] = nullptr;
    h_req_rx_local[i] = nullptr;
    h_ack_rx_local[i] = nullptr;
    h_buffer_full_status_rx_local[i] = nullptr;

    h_flit_tx_local[i] = nullptr;
    h_req_tx_local[i] = nullptr;
    h_ack_tx_local[i] = nullptr;
    h_buffer_full_status_tx_local[i] = nullptr;
  }

  // Clear DOWN port vectors
  h_flit_tx_down.clear();
  h_req_tx_down.clear();
  h_ack_tx_down.clear();
  h_buffer_full_status_tx_down.clear();

  h_flit_rx_down.clear();
  h_req_rx_down.clear();
  h_ack_rx_down.clear();
  h_buffer_full_status_rx_down.clear();
}

// Build the unified interface adapter
void Router::buildUnifiedInterface()
{
  // Clear all vectors
  all_flit_rx.clear();
  all_req_rx.clear();
  all_ack_rx.clear();
  all_buffer_full_status_rx.clear();

  all_flit_tx.clear();
  all_req_tx.clear();
  all_ack_tx.clear();
  all_buffer_full_status_tx.clear();

  buffers.clear();
  port_info_map.clear();
  current_level_rx.clear();
  current_level_tx.clear();
  start_from_vc.clear();

  // Define port order: UP -> LOCAL -> DOWN_0 -> DOWN_1 -> ...

  // 1. Add UP port (if this node is not root)
  if (local_level > 0)
  {
    cout << "function in " << local_id << endl;
    std::string up_name = "ROUTER::UP_" + std::to_string(local_id);

    // Create UP ports
    h_flit_rx_up = new sc_in<Flit>((up_name + "_flit_rx").c_str());
    h_req_rx_up = new sc_in<bool>((up_name + "_req_rx").c_str());
    h_ack_rx_up = new sc_out<bool>((up_name + "_ack_rx").c_str());
    h_buffer_full_status_rx_up =
        new sc_out<TBufferFullStatus>((up_name + "_buffer_status_rx").c_str());

    h_flit_tx_up = new sc_out<Flit>((up_name + "_flit_tx").c_str());
    h_req_tx_up = new sc_out<bool>((up_name + "_req_tx").c_str());
    h_ack_tx_up = new sc_in<bool>((up_name + "_ack_tx").c_str());
    h_buffer_full_status_tx_up =
        new sc_in<TBufferFullStatus>((up_name + "_buffer_status_tx").c_str());

    // Add to unified interface
    all_flit_rx.push_back(h_flit_rx_up);
    all_req_rx.push_back(h_req_rx_up);
    all_ack_rx.push_back(h_ack_rx_up);
    all_buffer_full_status_rx.push_back(h_buffer_full_status_rx_up);

    all_flit_tx.push_back(h_flit_tx_up);
    all_req_tx.push_back(h_req_tx_up);
    all_ack_tx.push_back(h_ack_tx_up);
    all_buffer_full_status_tx.push_back(h_buffer_full_status_tx_up);

    // Add port info
    PortInfo up_info = {PORT_UP, -1, up_name};
    port_info_map.push_back(up_info);

    // Add buffer and state
    buffers.push_back(new BufferBank());
    current_level_rx.push_back(false);
    current_level_tx.push_back(false);
    start_from_vc.push_back(0);
  }

  // 2. Add LOCAL ports (always present)
  for (int i = 0; i < NUM_LOCAL_PORTS; i++)
  {
    std::string local_name =
        "ROUTER::LOCAL_" + std::to_string(local_id) + "_" + std::to_string(i);

    // Create LOCAL ports
    h_flit_rx_local[i] = new sc_in<Flit>((local_name + "_flit_rx").c_str());
    h_req_rx_local[i] = new sc_in<bool>((local_name + "_req_rx").c_str());
    h_ack_rx_local[i] = new sc_out<bool>((local_name + "_ack_rx").c_str());
    h_buffer_full_status_rx_local[i] = new sc_out<TBufferFullStatus>(
        (local_name + "_buffer_status_rx").c_str());

    h_flit_tx_local[i] = new sc_out<Flit>((local_name + "_flit_tx").c_str());
    h_req_tx_local[i] = new sc_out<bool>((local_name + "_req_tx").c_str());
    h_ack_tx_local[i] = new sc_in<bool>((local_name + "_ack_tx").c_str());
    h_buffer_full_status_tx_local[i] = new sc_in<TBufferFullStatus>(
        (local_name + "_buffer_status_tx").c_str());

    // Add to unified interface
    all_flit_rx.push_back(h_flit_rx_local[i]);
    all_req_rx.push_back(h_req_rx_local[i]);
    all_ack_rx.push_back(h_ack_rx_local[i]);
    all_buffer_full_status_rx.push_back(h_buffer_full_status_rx_local[i]);

    all_flit_tx.push_back(h_flit_tx_local[i]);
    all_req_tx.push_back(h_req_tx_local[i]);
    all_ack_tx.push_back(h_ack_tx_local[i]);
    all_buffer_full_status_tx.push_back(h_buffer_full_status_tx_local[i]);

    // Add port info
    PortInfo local_info = {PORT_LOCAL, i, local_name};
    port_info_map.push_back(local_info);

    // Add buffer and state
    buffers.push_back(new BufferBank());
    current_level_rx.push_back(false);
    current_level_tx.push_back(false);
    start_from_vc.push_back(0);
  }

  // 3. Add DOWN ports (based on fanout)
  int fanout = 0;
  if (local_level < GlobalParams::num_levels - 1)
  {
    fanout = GlobalParams::fanouts_per_level[local_level];
  }

  for (int i = 0; i < fanout; i++)
  {
    std::string down_name =
        "DOWN_" + std::to_string(local_id) + "_" + std::to_string(i);

    // Create DOWN ports
    sc_out<Flit> *flit_tx = new sc_out<Flit>((down_name + "_flit_tx").c_str());
    sc_out<bool> *req_tx = new sc_out<bool>((down_name + "_req_tx").c_str());
    sc_in<bool> *ack_tx = new sc_in<bool>((down_name + "_ack_tx").c_str());
    sc_in<TBufferFullStatus> *buffer_status_tx =
        new sc_in<TBufferFullStatus>((down_name + "_buffer_status_tx").c_str());

    h_flit_tx_down.push_back(flit_tx);
    h_req_tx_down.push_back(req_tx);
    h_ack_tx_down.push_back(ack_tx);
    h_buffer_full_status_tx_down.push_back(buffer_status_tx);

    sc_in<Flit> *flit_rx = new sc_in<Flit>((down_name + "_flit_rx").c_str());
    sc_in<bool> *req_rx = new sc_in<bool>((down_name + "_req_rx").c_str());
    sc_out<bool> *ack_rx = new sc_out<bool>((down_name + "_ack_rx").c_str());
    sc_out<TBufferFullStatus> *buffer_status_rx = new sc_out<TBufferFullStatus>(
        (down_name + "_buffer_status_rx").c_str());

    h_flit_rx_down.push_back(flit_rx);
    h_req_rx_down.push_back(req_rx);
    h_ack_rx_down.push_back(ack_rx);
    h_buffer_full_status_rx_down.push_back(buffer_status_rx);

    // Add to unified interface
    all_flit_rx.push_back(flit_rx);
    all_req_rx.push_back(req_rx);
    all_ack_rx.push_back(ack_rx);
    all_buffer_full_status_rx.push_back(buffer_status_rx);

    all_flit_tx.push_back(flit_tx);
    all_req_tx.push_back(req_tx);
    all_ack_tx.push_back(ack_tx);
    all_buffer_full_status_tx.push_back(buffer_status_tx);

    // Add port info
    PortInfo down_info = {PORT_DOWN, i, down_name};
    port_info_map.push_back(down_info);

    buffers.push_back(new BufferBank());
    current_level_rx.push_back(false);
    current_level_tx.push_back(false);
    start_from_vc.push_back(0);
  }
}

// Cleanup all dynamically allocated ports
void Router::cleanupPorts()
{
  // Clean up UP ports
  if (h_flit_rx_up)
    delete h_flit_rx_up;
  if (h_req_rx_up)
    delete h_req_rx_up;
  if (h_ack_rx_up)
    delete h_ack_rx_up;
  if (h_buffer_full_status_rx_up)
    delete h_buffer_full_status_rx_up;
  if (h_flit_tx_up)
    delete h_flit_tx_up;
  if (h_req_tx_up)
    delete h_req_tx_up;
  if (h_ack_tx_up)
    delete h_ack_tx_up;
  if (h_buffer_full_status_tx_up)
    delete h_buffer_full_status_tx_up;

  for (int i = 0; i < NUM_LOCAL_PORTS; i++)
  {
    delete h_flit_rx_local[i];
    delete h_req_rx_local[i];
    delete h_ack_rx_local[i];
    delete h_buffer_full_status_rx_local[i];
    delete h_flit_tx_local[i];
    delete h_req_tx_local[i];
    delete h_ack_tx_local[i];
    delete h_buffer_full_status_tx_local[i];
  }

  // Clean up DOWN ports
  for (auto port : h_flit_tx_down)
    delete port;
  for (auto port : h_req_tx_down)
    delete port;
  for (auto port : h_ack_tx_down)
    delete port;
  for (auto port : h_buffer_full_status_tx_down)
    delete port;

  for (auto port : h_flit_rx_down)
    delete port;
  for (auto port : h_req_rx_down)
    delete port;
  for (auto port : h_ack_rx_down)
    delete port;
  for (auto port : h_buffer_full_status_rx_down)
    delete port;

  // Clean up buffers
  for (auto buffer : buffers)
    delete buffer;
}

int Router::getLogicalPortIndex(LogicalPortType type,
                                int instance_index) const
{
  // 遍历整个 port_info_map
  for (size_t i = 0; i < port_info_map.size(); i++)
  {
    // 检查端口类型是否匹配
    if (port_info_map[i].type == type)
    {
      // 检查该类型的实例索引是否匹配
      if (port_info_map[i].instance_index == instance_index)
      {
        return static_cast<int>(i); // 找到了完全匹配的端口，返回其逻辑ID
      }
    }
  }

  // 如果遍历完整个 map 都没有找到
  return -1;
}

bool Router::isDescendant(int dst_id) const
{
  // 安全检查：如果目标就是自己，不是自己的子孙
  if (dst_id == this->local_id)
  {
    return false;
  }

  // 从目标节点开始，向上遍历父节点链
  int current_node_id = dst_id;

  // 只要还没到根节点，就继续向上找
  while (GlobalParams::parent_map[current_node_id] != -1)
  {
    // 获取当前节点的父节点
    int parent_id = GlobalParams::parent_map[current_node_id];

    // 检查父节点是否是我们正在寻找的 local_id
    if (parent_id == this->local_id)
    {
      return true; // 找到了！dst_id 是我们的子孙
    }

    // 继续向上一层
    current_node_id = parent_id;
  }

  // 遍历到根节点都没找到，说明不是子孙
  return false;
}

int Router::getNextHopNode(int dst_id) const
{
  // 安全检查和前提条件
  assert(isDescendant(dst_id) &&
         "getNextHopNode should only be called for descendant nodes.");

  int current_node_id = dst_id;
  int previous_node_id = dst_id; // 用于记录回溯路径上的前一个节点

  // 只要还没到根节点，就继续向上找
  while (GlobalParams::parent_map[current_node_id] != -1)
  {
    // 获取当前节点的父节点
    int parent_id = GlobalParams::parent_map[current_node_id];

    // 检查父节点是否是我们的 local_id
    if (parent_id == this->local_id)
    {
      // 找到了！那么回溯路径上的前一个节点 previous_node_id
      // 就是 local_id 的那个直接子节点。
      return current_node_id;
    }

    // 更新并继续向上一层
    current_node_id = parent_id;
  }

  // 如果 isDescendant() 判断正确，代码理论上不应该执行到这里
  assert(
      false &&
      "Logical error in getNextHopNode: descendant not found in parent chain.");
  return -1; // 表示错误
}