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
#include <systemc.h>
#include <dbg.h>


inline int toggleKthBit(int n, int k)
{
	return (n ^ (1 << (k-1)));
}

void Router::process()
{
    txProcess();
    rxProcess();
}

void Router::rxProcess()
{
    if (reset.read()) {
	TBufferFullStatus bfs;
	// Clear outputs and indexes of receiving protocol
	for (size_t i = 0; i < all_flit_rx.size(); i++) {
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
	//assert(false);
	for (size_t i = 0; i < all_flit_rx.size(); i++) {
	    // To accept a new flit, the following conditions must match:
	    // 1) there is an incoming request
	    // 2) there is a free slot in the input buffer of direction i
	    //LOG<<"****RX****DIRECTION ="<<i<<  endl;

	    if (all_req_rx[i]->read() == 1 - current_level_rx[i])
	    {
		Flit received_flit = all_flit_rx[i]->read();
		//LOG<<"request opposite to the current_level, reading flit "<<received_flit<<endl;

		int vc = received_flit.vc_id;
        assert(buffers[i] != nullptr && "Pointer to BufferBank is null!");
		if (!(*buffers[i])[vc].IsFull())
		{

            
		    (*buffers[i])[vc].Push(received_flit);
		    LOG << " Flit " << received_flit <<" "<<received_flit.flit_type<< " collected from Input[" << i << "][" << vc <<"]" << endl;

		    power.bufferRouterPush();

		    // Negate the old value for Alternating Bit Protocol (ABP)
		    //LOG<<"INVERTING CL FROM "<< current_level_rx[i]<< " TO "<<  1 - current_level_rx[i]<<endl;
		    current_level_rx[i] = 1 - current_level_rx[i];

		    // if a new flit is injected from local PE
		    if (received_flit.src_id == local_id)
			power.networkInterface();
		}

		else  // buffer full
		{
		    // should not happen with the new TBufferFullStatus control signals
		    // except for flit coming from local PE, which don't use it
		    LOG << " Flit " << received_flit << " buffer full Input[" << i << "][" << vc <<"]" << endl;
		    assert(port_info_map[i].type == PORT_LOCAL);
		}

	    }
	    all_ack_rx[i]->write(current_level_rx[i]);
	    // updates the mask of VCs to prevent incoming data on full buffers
	    TBufferFullStatus bfs;
	    for (int vc=0;vc<GlobalParams::n_virtual_channels;vc++)
		bfs.mask[vc] = (*buffers[i])[vc].IsFull();
	    all_buffer_full_status_rx[i]->write(bfs);
	}
    }
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

	  for (int k = 0;k < GlobalParams::n_virtual_channels; k++)
	  {
	      int vc = (start_from_vc[i]+k)%(GlobalParams::n_virtual_channels);

	      // Uncomment to enable deadlock checking on buffers.
	      // Please also set the appropriate threshold.
	      // (*buffers[i]).deadlockCheck();

	      if (!(*buffers[i])[vc].IsEmpty()) 
	      {

		  Flit flit = (*buffers[i])[vc].Front();
		  power.bufferRouterFront();

		  if (flit.flit_type == FLIT_TYPE_HEAD)
		    {
		      // prepare data for routing
		      if (flit.is_multicast) {
		          // 多播路由处理
		          MulticastRouteData multicast_route_data;
		          multicast_route_data.current_id = local_id;
		          multicast_route_data.src_id = flit.src_id;
		          multicast_route_data.dst_ids = flit.multicast_dst_ids;
		          multicast_route_data.dir_in = i;
		          multicast_route_data.vc_id = flit.vc_id;
		          multicast_route_data.is_output = flit.is_output;

		          // 调用多播路由函数
		          vector<int> output_ports = routeMulticast(multicast_route_data);
		          cout << "Router " << local_id << " multicast route from input " << i << " to outputs: ";
		          for (size_t idx = 0; idx < output_ports.size(); idx++) {
		              cout << output_ports[idx] << (idx < output_ports.size() - 1 ? ", " : "");
		          }
		          cout << endl;

		          // 预留所有输出端口（原子操作）
		          TReservation r;
		          r.input = i;
		          r.vc = vc;
		          int reservation_status = reservation_table.checkReservation(r, output_ports);

		          if (reservation_status == RT_AVAILABLE) {
		              LOG << " reserving multicast outputs for flit " << flit << endl;
		              reservation_table.reserve(r, output_ports);
		          } else if (reservation_status == RT_ALREADY_SAME) {
		              LOG << " RT_ALREADY_SAME reserved multicast outputs for flit " << flit << endl;
		          } else if (reservation_status == RT_OUTVC_BUSY) {
		              LOG << " RT_OUTVC_BUSY reservation for multicast flit " << flit << endl;
		          } else if (reservation_status == RT_ALREADY_OTHER_OUT) {
		              LOG << "RT_ALREADY_OTHER_OUT: another outputs previously reserved for the same multicast flit" << endl;
		          } else {
		              assert(false); // no meaningful status here
		          }
		      } else {
		          // 单播路由处理
		          RouteData route_data;
		          route_data.current_id = local_id;
		          //LOG<< "current_id= "<< route_data.current_id <<" for sending " << flit << endl;
		          route_data.src_id = flit.src_id;
		          route_data.dst_id = flit.dst_id;
		          route_data.dir_in = i;
		          route_data.vc_id = flit.vc_id;
			      route_data.is_output = flit.is_output; // 新增：标记是否为output包

		          // TODO: see PER POSTERI (adaptive routing should not recompute route if already reserved)
		          int o = route(route_data);
		          cout << "Router " << local_id << " route from input " << i << " to output " << o << endl;

		          // manage special case of target hub not directly connected to destination
		          if (o >= DIRECTION_HUB_RELAY) {
		              Flit f = (*buffers[i])[vc].Pop();
		              f.hub_relay_node = o - DIRECTION_HUB_RELAY;
		              (*buffers[i])[vc].Push(f);
		              o = DIRECTION_HUB;
		          }

		          TReservation r;
		          r.input = i;
		          r.vc = vc;

		          LOG << " checking availability of Output[" << o << "] for Input[" << i << "][" << vc << "] flit " << flit << endl;

		          int rt_status = reservation_table.checkReservation(r, o);

		          if (rt_status == RT_AVAILABLE) {
		              LOG << " reserving direction " << o << " for flit " << flit << endl;
		              reservation_table.reserve(r, o);
		          } else if (rt_status == RT_ALREADY_SAME) {
		              LOG << " RT_ALREADY_SAME reserved direction " << o << " for flit " << flit << endl;
		          } else if (rt_status == RT_OUTVC_BUSY) {
		              LOG << " RT_OUTVC_BUSY reservation direction " << o << " for flit " << flit << endl;
		          } else if (rt_status == RT_ALREADY_OTHER_OUT) {
		              LOG << "RT_ALREADY_OTHER_OUT: another output previously reserved for the same flit " << endl;
		          } else {
		              assert(false); // no meaningful status here
		          }
		      }
		    }
		}
	  }
	    start_from_vc[i] = (start_from_vc[i]+1)%GlobalParams::n_virtual_channels;
	}

      start_from_port = (start_from_port + 1) % all_flit_rx.size();

      //==================================================================
    // 2nd phase: Two-Phase Arbitration & Atomic Forwarding
    // 阶段A: 候选筛选 - 收集所有准备就绪的VC
    //==================================================================
    std::vector<std::pair<int, int>> ready_vcs; // (input_port, vc) pairs

    for (size_t input_port = 0; input_port < all_flit_rx.size(); input_port++) {
        // 获取该输入端口的所有预留
        std::map<int, std::vector<int>> vc_reservations = reservation_table.getReservations(input_port);

        for (auto& vc_entry : vc_reservations) {
            int vc = vc_entry.first;
            const std::vector<int>& output_ports = vc_entry.second;

            // 预检查：所有目标输出端口是否都准备就绪
            bool all_outputs_ready = true;
            for (int output_port : output_ports) {
                if (!(current_level_tx[output_port] == all_ack_tx[output_port]->read() &&
                      all_buffer_full_status_tx[output_port]->read().mask[vc] == false)) {
                    all_outputs_ready = false;
                    break;
                }
            }

            // 如果所有输出端口都准备就绪，且缓冲区非空，则加入候选列表
            if (all_outputs_ready && !(*buffers[input_port])[vc].IsEmpty()) {
                ready_vcs.emplace_back(input_port, vc);
            }
        }
    }

    //==================================================================
    // 阶段B: 仲裁与原子转发
    //==================================================================
    if (!ready_vcs.empty()) {
        // 仲裁：随机选择一个准备就绪的VC
        int winner_idx = rand() % ready_vcs.size();
        int winner_input = ready_vcs[winner_idx].first;
        int winner_vc = ready_vcs[winner_idx].second;

        // 获取获胜VC的所有目标输出端口
        std::map<int, std::vector<int>> winner_reservations = reservation_table.getReservations(winner_input);
        const std::vector<int>& target_outputs = winner_reservations[winner_vc];

        // 原子转发：复制Flit到所有目标输出端口
        Flit flit = (*buffers[winner_input])[winner_vc].Front();
        LOG << "Atomic Forwarding: Input[" << winner_input << "][" << winner_vc << "] -> ";
        for (size_t i = 0; i < target_outputs.size(); i++) {
            int output_port = target_outputs[i];
            all_flit_tx[output_port]->write(flit);
            current_level_tx[output_port] = 1 - current_level_tx[output_port];
            all_req_tx[output_port]->write(current_level_tx[output_port]);
            LOG << "Output[" << output_port << (i < target_outputs.size() - 1 ? ", " : "");
        }
        LOG << ", flit: " << flit << endl;

        // 原子弹出：只从输入缓冲区弹出一次
        (*buffers[winner_input])[winner_vc].Pop();

        // 处理TAIL Flit的资源释放
        if (flit.flit_type == FLIT_TYPE_TAIL) {
            TReservation r;
            r.input = winner_input;
            r.vc = winner_vc;
            reservation_table.release(r, target_outputs);
        }

        // 功耗与统计（对所有目标端口进行统计）
        for (int output_port : target_outputs) {
            if (output_port == DIRECTION_HUB) power.r2hLink();
            else power.r2rLink();

            if (output_port == DIRECTION_LOCAL) {
                power.networkInterface();
                LOG << "Consumed flit " << flit << endl;
                stats.receivedFlit(sc_time_stamp().to_double() / GlobalParams::clock_period_ps, flit);
                if (GlobalParams::max_volume_to_be_drained) {
                    if (drained_volume >= GlobalParams::max_volume_to_be_drained)
                        sc_stop();
                    else {
                        drained_volume++;
                        local_drained++;
                    }
                }
            } else if (winner_input != DIRECTION_LOCAL && winner_input != DIRECTION_LOCAL_2) {
                routed_flits++;
            }
        }

        power.bufferRouterPop();
        power.crossBar();
    }

    if ((int)(sc_time_stamp().to_double() / GlobalParams::clock_period_ps)%2==0)
	reservation_table.updateIndex();
    }   
}

// NoP_data Router::getCurrentNoPData()
// {
//     NoP_data NoP_data;

//     for (int j = 0; j < DIRECTIONS; j++) {
// 	try {
// 		NoP_data.channel_status_neighbor[j].free_slots = free_slots_neighbor[j].read();
// 		NoP_data.channel_status_neighbor[j].available = (reservation_table.isNotReserved(j));
// 	}
// 	catch (int e)
// 	{
// 	    if (e!=NOT_VALID) assert(false);
// 	    // Nothing to do if an NOT_VALID direction is caught
// 	};
//     }

//     NoP_data.sender_id = local_id;

//     return NoP_data;
// }

void Router::perCycleUpdate()
{
    if (reset.read()) {
		return;

    } else {

	power.leakageRouter();
	for (size_t i = 0; i < all_flit_rx.size()-NUM_LOCAL_PORTS; i++)
	{
	    for (int vc=0;vc<GlobalParams::n_virtual_channels;vc++)
	    {
		power.leakageBufferRouter();
		power.leakageLinkRouter2Router();
	    }
	}

	power.leakageLinkRouter2Hub();
    }
}


vector < int > Router::routingFunction(const RouteData & route_data)
{

	// TODO: fix all the deprecated verbose mode logs
	if (GlobalParams::verbose_mode > VERBOSE_OFF)
		LOG << "Wired routing for dst = " << route_data.dst_id << endl;

	// not wireless direction taken, apply normal routing
	return routingAlgorithm->route(this, route_data);
}



int Router::route(const RouteData & route_data)
{
    if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
        
        // 规则 1: 检查本地
        if (this->local_id == route_data.dst_id) {
            return getLogicalPortIndex(PORT_LOCAL, 0);
        }
        
        // 规则 2: 检查子孙 (向下路由)
        if (isDescendant(route_data.dst_id)) {
            int next_hop_child_id = getNextHopNode(route_data.dst_id);
            
            for(int i=0; i<GlobalParams::fanouts_per_level[GlobalParams::node_level_map[local_id]]; i++) {
                if (GlobalParams::child_map[local_id][i] == next_hop_child_id) {
                    return getLogicalPortIndex(PORT_DOWN, i);
                }
            }

            // 如果 for 循环结束还没返回，说明有严重逻辑错误！
            // getNextHopChild() 的结果在 child_map 中找不到。
            cout << "FATAL ERROR in Router " << local_id << ": Cannot find next hop child " 
                 << next_hop_child_id << " for destination " << route_data.dst_id << endl;
            assert(false);
            return -1; // 或者其他错误码
        }
        
        // 规则 3: 向上路由 (如果不是本地也不是子孙)
        else { // <--- 使用 else
            if (local_level > 0) {
                return getLogicalPortIndex(PORT_UP, -1);
            } else {
                // 这种情况现在只会在根节点发生
                cout << "FATAL ERROR in Root Router " << local_id << ": Unroutable destination " 
                     << route_data.dst_id << endl;
                assert(false);
                return -1;
            }
        }
    }

    return -2;
}

  vector<int> Router::routeMulticast(const MulticastRouteData & route_data)
  {
      vector<int> output_ports;

      if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
          // 检查是否有本地目标
          bool has_local_target = false;
          for (int dst_id : route_data.dst_ids) {
              if (dst_id == this->local_id) {
                  has_local_target = true;
                  break;
              }
          }

          // 如果有本地目标，添加本地端口
          if (has_local_target) {
              output_ports.push_back(getLogicalPortIndex(PORT_LOCAL, 0));
          }

          // 获取所有需要向下路由的子节点
          vector<int> child_targets = getMulticastChildren(route_data.dst_ids);

          // 为每个子目标添加对应的下行端口
          for (int child_id : child_targets) {
              for(int i=0; i<GlobalParams::fanouts_per_level[GlobalParams::node_level_map[local_id]]; i++) {
                  if (GlobalParams::child_map[local_id][i] == child_id) {
                      output_ports.push_back(getLogicalPortIndex(PORT_DOWN, i));
                      break;
                  }
              }
          }

          // 如果存在非本地且非子孙的目标，需要向上路由
          bool has_upward_target = false;
          for (int dst_id : route_data.dst_ids) {
              if (dst_id != this->local_id && !isDescendant(dst_id)) {
                  has_upward_target = true;
                  break;
              }
          }

          // 防环路规则：只有当数据包不是从UP端口来时，才允许向上转发
          // 这避免了从父节点接收的多播包再次被送回父节点造成环路
          int up_port_index = getLogicalPortIndex(PORT_UP, -1);
          if (has_upward_target && local_level > 0 && route_data.dir_in != up_port_index) {
              output_ports.push_back(up_port_index);
          }
      }

      return output_ports;
  }

  vector<int> Router::getMulticastChildren(const vector<int>& dst_ids)
  {
      vector<int> child_targets;

      for (int dst_id : dst_ids) {
          if (dst_id != this->local_id && isDescendant(dst_id)) {
              int next_hop_child = getNextHopNode(dst_id);

              // 检查这个目标节点是否已经是我们的直接子节点
              bool is_direct_child = false;
              for(int i=0; i<GlobalParams::fanouts_per_level[GlobalParams::node_level_map[local_id]]; i++) {
                  if (GlobalParams::child_map[local_id][i] == dst_id) {
                      is_direct_child = true;
                      break;
                  }
              }

              // 如果目标是直接子节点，直接添加目标节点
              // 如果目标是孙子或更深层节点，添加下一跳子节点
              int child_to_add = is_direct_child ? dst_id : next_hop_child;

              // 避免重复添加相同的子节点
              if (find(child_targets.begin(), child_targets.end(), child_to_add) == child_targets.end()) {
                  child_targets.push_back(child_to_add);
              }
          }
      }

      return child_targets;
  }

int Router::selectionFunction(const vector < int >&directions,
				   const RouteData & route_data)
{
    // Hierarchical mode: simple selection
    if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
        // In hierarchical topology, routing function should return only one direction
        // If multiple directions are returned (shouldn't happen), pick the first one
        if (directions.size() > 0) {
            return directions[0];
        }
        return NOT_VALID;
    }
    return -1; // always return the first direction

}

void Router::configure(const int _id, const int _level,
			    const double _warm_up_time,
			    const unsigned int _max_buffer_size,
			    GlobalRoutingTable & grt)
{
    local_id = _id;
    local_level = _level;
    stats.configure(_id, _warm_up_time);

    start_from_port = (all_flit_rx.size() > 0) ? getLogicalPortIndex(PORT_LOCAL, 0) : 0; // Start from LOCAL port
  
    // initPorts();
    // buildUnifiedInterface();
    routingAlgorithm = RoutingAlgorithms::get(GlobalParams::routing_algorithm);

    if (routingAlgorithm == 0)
        {
            cerr << " FATAL: invalid routing -routing " << GlobalParams::routing_algorithm << ", check with noxim -help" << endl;
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
	    (*buffers[i])[vc].setLabel(string(name())+"->buffer["+i_to_string(i)+"]");
	}
	start_from_vc[i] = 0;
    }


    // if (GlobalParams::topology == TOPOLOGY_MESH)
    // {
	// int row = _id / GlobalParams::mesh_dim_x;
	// int col = _id % GlobalParams::mesh_dim_x;

	// for (int vc = 0; vc<GlobalParams::n_virtual_channels; vc++)
	// {
	//     if (row == 0)
	//       buffer[DIRECTION_NORTH][vc].Disable();
	//     if (row == GlobalParams::mesh_dim_y-1)
	//       buffer[DIRECTION_SOUTH][vc].Disable();
	//     if (col == 0)
	//       buffer[DIRECTION_WEST][vc].Disable();
	//     if (col == GlobalParams::mesh_dim_x-1)
	//       buffer[DIRECTION_EAST][vc].Disable();
	// }
	
	// // Ensure LOCAL and LOCAL_2 ports are always enabled in mesh topology
	// buffer[DIRECTION_LOCAL][vc].Enable();
	// buffer[DIRECTION_LOCAL_2][vc].Enable();
    }


unsigned long Router::getRoutedFlits()
{
    return routed_flits;
}


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

    switch (direction) {
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

// bool Router::inCongestion()
// {
//     for (int i = 0; i < DIRECTIONS; i++) {

// 	if (free_slots_neighbor[i]==NOT_VALID) continue;

// 	int flits = GlobalParams::buffer_depth - free_slots_neighbor[i];
// 	if (flits > (int) (GlobalParams::buffer_depth * GlobalParams::dyad_threshold))
// 	    return true;
//     }

//     return false;
// }

void Router::ShowBuffersStats(std::ostream & out)
{
  for (size_t i=0; i<all_flit_rx.size(); i++)
      for (int vc=0; vc<GlobalParams::n_virtual_channels;vc++)
	    (*buffers[i])[vc].ShowStats(out);
}


bool Router::connectedHubs(int src_hub, int dst_hub) {
    vector<int> &first = GlobalParams::hub_configuration[src_hub].txChannels;
    vector<int> &second = GlobalParams::hub_configuration[dst_hub].rxChannels;

    vector<int> intersection;

    for (unsigned int i = 0; i < first.size(); i++) {
        for (unsigned int j = 0; j < second.size(); j++) {
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
Router::Router(sc_module_name nm) {

    // Register SystemC methods
    SC_METHOD(process);
    sensitive << reset;
    sensitive << clock.pos();

    // Use proper scope resolution for SystemC method registration
    SC_METHOD(perCycleUpdate);
    sensitive << reset;
    sensitive << clock.pos();
    sensitive << clock.pos();

    // // Initialize dynamic ports
    // initPorts();
    // cout<<"function!"<<endl;
    // buildUnifiedInterface();

    // routingAlgorithm = RoutingAlgorithms::get(GlobalParams::routing_algorithm);

    // if (routingAlgorithm == 0)
    // {
    //     cerr << " FATAL: invalid routing -routing " << GlobalParams::routing_algorithm << ", check with noxim -help" << endl;
    //     exit(-1);
    // }
}

// Destructor implementation
Router::~Router() {
    cleanupPorts();
}

// Initialize all dynamic ports based on hierarchical configuration
void Router::initPorts() {
    // Initialize all pointers to nullptr
    h_flit_rx_up = nullptr;
    h_req_rx_up = nullptr;
    h_ack_rx_up = nullptr;
    h_buffer_full_status_rx_up = nullptr;

    h_flit_tx_up = nullptr;
    h_req_tx_up = nullptr;
    h_ack_tx_up = nullptr;
    h_buffer_full_status_tx_up = nullptr;

    for (int i = 0; i < NUM_LOCAL_PORTS; i++) {
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
void Router::buildUnifiedInterface() {
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
    if (local_level > 0) {
        cout<<"function in "<<local_id<<endl;
        std::string up_name = "ROUTER::UP_" + std::to_string(local_id);

        // Create UP ports
        h_flit_rx_up = new sc_in<Flit>((up_name + "_flit_rx").c_str());
        h_req_rx_up = new sc_in<bool>((up_name + "_req_rx").c_str());
        h_ack_rx_up = new sc_out<bool>((up_name + "_ack_rx").c_str());
        h_buffer_full_status_rx_up = new sc_out<TBufferFullStatus>((up_name + "_buffer_status_rx").c_str());

        h_flit_tx_up = new sc_out<Flit>((up_name + "_flit_tx").c_str());
        h_req_tx_up = new sc_out<bool>((up_name + "_req_tx").c_str());
        h_ack_tx_up = new sc_in<bool>((up_name + "_ack_tx").c_str());
        h_buffer_full_status_tx_up = new sc_in<TBufferFullStatus>((up_name + "_buffer_status_tx").c_str());

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
for (int i = 0; i < NUM_LOCAL_PORTS; i++) {
    std::string local_name = "ROUTER::LOCAL_" + std::to_string(local_id) + "_" + std::to_string(i);

    // Create LOCAL ports
    h_flit_rx_local[i] = new sc_in<Flit>((local_name + "_flit_rx").c_str());
    h_req_rx_local[i] = new sc_in<bool>((local_name + "_req_rx").c_str());
    h_ack_rx_local[i] = new sc_out<bool>((local_name + "_ack_rx").c_str());
    h_buffer_full_status_rx_local[i] = new sc_out<TBufferFullStatus>((local_name + "_buffer_status_rx").c_str());

    h_flit_tx_local[i] = new sc_out<Flit>((local_name + "_flit_tx").c_str());
    h_req_tx_local[i] = new sc_out<bool>((local_name + "_req_tx").c_str());
    h_ack_tx_local[i] = new sc_in<bool>((local_name + "_ack_tx").c_str());
    h_buffer_full_status_tx_local[i] = new sc_in<TBufferFullStatus>((local_name + "_buffer_status_tx").c_str());

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
    if (local_level < GlobalParams::num_levels - 1) {
        fanout = GlobalParams::fanouts_per_level[local_level];
    }

    for (int i = 0; i < fanout; i++) {
        std::string down_name = "DOWN_" + std::to_string(local_id) + "_" + std::to_string(i);

        // Create DOWN ports
        sc_out<Flit>* flit_tx = new sc_out<Flit>((down_name + "_flit_tx").c_str());
        sc_out<bool>* req_tx = new sc_out<bool>((down_name + "_req_tx").c_str());
        sc_in<bool>* ack_tx = new sc_in<bool>((down_name + "_ack_tx").c_str());
        sc_in<TBufferFullStatus>* buffer_status_tx = new sc_in<TBufferFullStatus>((down_name + "_buffer_status_tx").c_str());

        h_flit_tx_down.push_back(flit_tx);
        h_req_tx_down.push_back(req_tx);
        h_ack_tx_down.push_back(ack_tx);
        h_buffer_full_status_tx_down.push_back(buffer_status_tx);

        sc_in<Flit>* flit_rx = new sc_in<Flit>((down_name + "_flit_rx").c_str());
        sc_in<bool>* req_rx = new sc_in<bool>((down_name + "_req_rx").c_str());
        sc_out<bool>* ack_rx = new sc_out<bool>((down_name + "_ack_rx").c_str());
        sc_out<TBufferFullStatus>* buffer_status_rx = new sc_out<TBufferFullStatus>((down_name + "_buffer_status_rx").c_str());

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
void Router::cleanupPorts() {
    // Clean up UP ports
    if (h_flit_rx_up) delete h_flit_rx_up;
    if (h_req_rx_up) delete h_req_rx_up;
    if (h_ack_rx_up) delete h_ack_rx_up;
    if (h_buffer_full_status_rx_up) delete h_buffer_full_status_rx_up;
    if (h_flit_tx_up) delete h_flit_tx_up;
    if (h_req_tx_up) delete h_req_tx_up;
    if (h_ack_tx_up) delete h_ack_tx_up;
    if (h_buffer_full_status_tx_up) delete h_buffer_full_status_tx_up;

    for (int i = 0; i < NUM_LOCAL_PORTS; i++) {
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
    for (auto port : h_flit_tx_down) delete port;
    for (auto port : h_req_tx_down) delete port;
    for (auto port : h_ack_tx_down) delete port;
    for (auto port : h_buffer_full_status_tx_down) delete port;

    for (auto port : h_flit_rx_down) delete port;
    for (auto port : h_req_rx_down) delete port;
    for (auto port : h_ack_rx_down) delete port;
    for (auto port : h_buffer_full_status_rx_down) delete port;

    // Clean up buffers
    for (auto buffer : buffers) delete buffer;
}


int Router::getLogicalPortIndex(LogicalPortType type, int instance_index) const {
    // 遍历整个 port_info_map
    for (size_t i = 0; i < port_info_map.size(); i++) {
        // 检查端口类型是否匹配
        if (port_info_map[i].type == type) {
            // 检查该类型的实例索引是否匹配
            if (port_info_map[i].instance_index == instance_index) {
                return static_cast<int>(i); // 找到了完全匹配的端口，返回其逻辑ID
            }
        }
    }

    // 如果遍历完整个 map 都没有找到
    return -1; 
}

bool Router::isDescendant(int dst_id) const {
    // 安全检查：如果目标就是自己，不是自己的子孙
    if (dst_id == this->local_id) {
        return false;
    }

    // 从目标节点开始，向上遍历父节点链
    int current_node_id = dst_id;

    // 只要还没到根节点，就继续向上找
    while (GlobalParams::parent_map[current_node_id] != -1) {
        // 获取当前节点的父节点
        int parent_id = GlobalParams::parent_map[current_node_id];

        // 检查父节点是否是我们正在寻找的 local_id
        if (parent_id == this->local_id) {
            return true; // 找到了！dst_id 是我们的子孙
        }

        // 继续向上一层
        current_node_id = parent_id;
    }

    // 遍历到根节点都没找到，说明不是子孙
    return false;
}

int Router::getNextHopNode(int dst_id) const {
    // 安全检查和前提条件
    assert(isDescendant(dst_id) && "getNextHopNode should only be called for descendant nodes.");

    int current_node_id = dst_id;
    int previous_node_id = dst_id; // 用于记录回溯路径上的前一个节点

    // 只要还没到根节点，就继续向上找
    while (GlobalParams::parent_map[current_node_id] != -1) {
        // 获取当前节点的父节点
        int parent_id = GlobalParams::parent_map[current_node_id];

        // 检查父节点是否是我们的 local_id
        if (parent_id == this->local_id) {
            // 找到了！那么回溯路径上的前一个节点 previous_node_id
            // 就是 local_id 的那个直接子节点。
            return current_node_id;
        }

        // 更新并继续向上一层
        current_node_id = parent_id;
    }

    // 如果 isDescendant() 判断正确，代码理论上不应该执行到这里
    assert(false && "Logical error in getNextHopNode: descendant not found in parent chain.");
    return -1; // 表示错误
}