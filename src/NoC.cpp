/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the Network-on-Chip
 */

#include "NoC.h"
#include <dbg.h>

using namespace std;

inline int toggleKthBit(int n, int k) 
{ 
    return (n ^ (1 << (k-1))); 
}

void NoC::buildCommon()
{
	token_ring = new TokenRing("tokenring");
	token_ring->clock(clock);
	token_ring->reset(reset);


	char channel_name[16];
	for (map<int, ChannelConfig>::iterator it = GlobalParams::channel_configuration.begin();
		 it != GlobalParams::channel_configuration.end();
		 ++it)
	{
		int channel_id = it->first;
		sprintf(channel_name, "Channel_%d", channel_id);
		channel[channel_id] = new Channel(channel_name, channel_id);
	}

	char hub_name[16];
	for (map<int, HubConfig>::iterator it = GlobalParams::hub_configuration.begin();
		 it != GlobalParams::hub_configuration.end();
		 ++it)
	{
		int hub_id = it->first;
		//LOG << " hub id " <<  hub_id;
		HubConfig hub_config = it->second;

		sprintf(hub_name, "Hub_%d", hub_id);
		hub[hub_id] = new Hub(hub_name, hub_id,token_ring);
		hub[hub_id]->clock(clock);
		hub[hub_id]->reset(reset);


		// Determine, from configuration file, which Hub is connected to which Tile
		for(vector<int>::iterator iit = hub_config.attachedNodes.begin();
			iit != hub_config.attachedNodes.end();
			++iit)
		{
			GlobalParams::hub_for_tile[*iit] = hub_id;
			//LOG<<"I am hub "<<hub_id<<" and I amconnecting to "<<*iit<<endl;

		}
		//for (map<int, int>::iterator it1 = GlobalParams::hub_for_tile.begin(); it1 != GlobalParams::hub_for_tile.end(); it1++ )
		//LOG<<"it1 first "<< it1->first<< "second"<< it1->second<<endl;

		// Determine, from configuration file, which Hub is connected to which Channel
		for(vector<int>::iterator iit = hub_config.txChannels.begin();
			iit != hub_config.txChannels.end();
			++iit)
		{
			int channel_id = *iit;
			//LOG << "Binding " << hub[hub_id]->name() << " to txChannel " << channel_id << endl;
			hub[hub_id]->init[channel_id]->socket.bind(channel[channel_id]->targ_socket);
			//LOG << "Binding " << hub[hub_id]->name() << " to txChannel " << channel_id << endl;
			hub[hub_id]->setFlitTransmissionCycles(channel[channel_id]->getFlitTransmissionCycles(),channel_id);
		}

		for(vector<int>::iterator iit = hub_config.rxChannels.begin();
			iit != hub_config.rxChannels.end();
			++iit)
		{
			int channel_id = *iit;
			//LOG << "Binding " << hub[hub_id]->name() << " to rxChannel " << channel_id << endl;
			channel[channel_id]->init_socket.bind(hub[hub_id]->target[channel_id]->socket);
			channel[channel_id]->addHub(hub[hub_id]);
		}

		// TODO FIX
		// Hub Power model does not currently support different data rates for single hub
		// If multiple channels are connected to an Hub, the data rate
		// of the first channel will be used as default

		int no_channels = hub_config.txChannels.size();

		int data_rate_gbs;

		if (no_channels > 0) {
			data_rate_gbs = GlobalParams::channel_configuration[hub_config.txChannels[0]].dataRate;
		}
		else
			data_rate_gbs = NOT_VALID;

		// TODO: update power model (configureHub to support different tx/tx buffer depth in the power breakdown
		// Currently, an averaged value is used when accounting in Power class methods

		hub[hub_id]->power.configureHub(GlobalParams::flit_size,
										GlobalParams::hub_configuration[hub_id].toTileBufferSize,
										GlobalParams::hub_configuration[hub_id].fromTileBufferSize,
										GlobalParams::flit_size,
										GlobalParams::hub_configuration[hub_id].rxBufferSize,
										GlobalParams::hub_configuration[hub_id].txBufferSize,
										GlobalParams::flit_size,
										data_rate_gbs);
	}


	// Check for routing table availability
	if (GlobalParams::routing_algorithm == ROUTING_TABLE_BASED)
		assert(grtable.load(GlobalParams::routing_table_filename.c_str()));

	// Check for traffic table availability
	if (GlobalParams::traffic_distribution == TRAFFIC_TABLE_BASED)
		assert(gttable.load(GlobalParams::traffic_table_filename.c_str()));

	// Var to track Hub connected ports
	hub_connected_ports = (int *) calloc(GlobalParams::hub_configuration.size(), sizeof(int));

}



//======================================================================
// 方法: buildHierarchical()
// 描述: 构建层次化NoC拓扑结构
// 结构: 树状层次结构，适用于加速器模拟
//
// 层次化设计:
//   Level 0: Root节点 (1个) - 系统根节点
//   Level 1: Intermediate节点 (4个) - 中间层节点  
//   Level 2: Leaf节点 (16个) - 叶子计算节点
//
// 连接方式:
//   - Root: 4个DOWN端口连接到Intermediate节点
//   - Intermediate: 1个UP端口连接到Root, 4个DOWN端口连接到Leaf节点
//   - Leaf: 1个UP端口连接到Intermediate节点
//
// 节点ID分配:
//   - Level 0: ID 0
//   - Level 1: ID 1-4 
//   - Level 2: ID 5-20
//======================================================================
void NoC::buildHierarchical()
{
    cout << "=== 构建层次化NoC拓扑结构 ===" << endl;
    
    // 调用通用构建方法
    buildCommon();
    
    //==================================================================
    // 1. 初始化层次化拓扑参数
    //==================================================================
    if (GlobalParams::num_levels <= 0 || GlobalParams::fanouts_per_level == nullptr) {
        // 使用 cerr 输出错误信息，这是标准错误流
        std::cerr << "错误: 全局配置 (GlobalParams) 无效或未初始化。" << std::endl;
        std::cerr << "  - GlobalParams::num_levels: " << GlobalParams::num_levels << std::endl;
        std::cerr << "  - GlobalParams::nodes_per_level is " 
                  << (GlobalParams::fanouts_per_level == nullptr ? "nullptr" : "not null") << std::endl;
        
        // 验证失败，终止当前函数的执行
        return;
    }

    num_levels = GlobalParams::num_levels;
    nodes_per_level = new int[num_levels];
	for (int i = 0; i < num_levels; i++) {
		if(i==0)
			nodes_per_level[i] = 1; // 根节点
		else
			nodes_per_level[i] = nodes_per_level[i-1]* GlobalParams::fanouts_per_level[i-1];
		dbg(i,GlobalParams::fanouts_per_level[i-1],nodes_per_level[i]);
	}
    
    // 计算总节点数
    total_nodes = 0;
    for (int i = 0; i < num_levels; i++) {
        total_nodes += nodes_per_level[i];
    }
    
    cout << "层次化结构: " << num_levels << "层" << endl;
    cout << "节点分布: ";
    for (int i = 0; i < num_levels; i++) {
        cout << "L" << i << "(" << nodes_per_level[i] << ") ";
    }
    cout << "= 总计 " << total_nodes << " 个节点" << endl;
    
    //==================================================================
    // 2. 创建层次化映射关系
    //==================================================================
    setupHierarchicalTopology();
    
    //==================================================================
    // 3. 分配层次化信号
    //    使用1D数组，按节点ID索引
    //==================================================================
    hierarchical_req = new sc_signal_Hierarchical<bool>*[total_nodes];
    hierarchical_ack = new sc_signal_Hierarchical<bool>*[total_nodes];
    hierarchical_buffer_full_status = new sc_signal_Hierarchical<TBufferFullStatus>*[total_nodes];
    hierarchical_flit = new sc_signal_Hierarchical<Flit>*[total_nodes];
    
    for (int i = 0; i < total_nodes; i++) {
        hierarchical_req[i] = new sc_signal_Hierarchical<bool>();
        hierarchical_ack[i] = new sc_signal_Hierarchical<bool>();
        hierarchical_buffer_full_status[i] = new sc_signal_Hierarchical<TBufferFullStatus>();
        hierarchical_flit[i] = new sc_signal_Hierarchical<Flit>();
    }
    
    //==================================================================
    // 4. 创建Tile数组 (1D结构，适合层次化拓扑)
    //==================================================================
    t_h = new Tile*[total_nodes];
    
    //==================================================================
    // 5. 创建和配置所有节点
    //==================================================================
    for (int node_id = 0; node_id < total_nodes; node_id++) {
        char tile_name[64];
        sprintf(tile_name, "HNode_%d", node_id);

        
        // 创建Tile
        t_h[node_id] = new Tile(tile_name, node_id,node_level_map[node_id]);
        
        // 配置Router
        t_h[node_id]->r->configure(node_id,
                                 GlobalParams::stats_warm_up_time,
                                 GlobalParams::buffer_depth,
                                 grtable);
        t_h[node_id]->r->power.configureRouter(GlobalParams::flit_size,
                                              GlobalParams::buffer_depth,
                                              GlobalParams::flit_size,
                                              string(GlobalParams::routing_algorithm),
                                              "default");
        
        // 配置ProcessingElement
        t_h[node_id]->pe->local_id = node_id;
        t_h[node_id]->pe->traffic_table = &gttable;
        t_h[node_id]->pe->never_transmit = true;
        
        // 连接时钟和复位
        t_h[node_id]->clock(clock);
        t_h[node_id]->reset(reset);

        int level = getLevelOfNode(node_id);
        cout << "创建节点 " << node_id << " (Level " << level << ")" << endl;
    }
    
    //==================================================================
    // 6. 建立层次化连接
    //==================================================================
    // 建立层次化连接关系
    setupHierarchicalConnections();
    
    cout << "=== 层次化NoC拓扑构建完成 ===" << endl;
    cout << "注意: 需要修改Router类支持层次化路由" << endl;
}

//======================================================================
// 方法: setupHierarchicalTopology()
// 描述: 根据num_levels和nodes_per_level建立层次化映射关系
//======================================================================
void NoC::setupHierarchicalTopology()
{
    cout << "建立层次化映射关系..." << endl;
    
    // 分配映射数组
    node_level_map = new int[total_nodes];
    parent_map = new int[total_nodes];
    child_map = new int*[total_nodes];
    
    // 初始化
    for (int i = 0; i < total_nodes; i++) {
        node_level_map[i] = -1;
        parent_map[i] = -1;
        child_map[i] = NULL;
    }
    
    // 使用num_levels和nodes_per_level动态构建
    int node_id = 0;
    
    for (int level = 0; level < num_levels; level++) {
        int level_start = node_id;
        int level_end = node_id + nodes_per_level[level];
        
        // 设置节点层级
        for (int i = level_start; i < level_end; i++) {
            node_level_map[i] = level;
        }
        
        // 非根节点设置父节点
        if (level > 0) {
            int parent_level_start = 0;
            for (int p = 0; p < level - 1; p++) {
                parent_level_start += nodes_per_level[p];
            }
            
            for (int i = 0; i < nodes_per_level[level]; i++) {
                int current_node = level_start + i;
                int parent_id = parent_level_start + (i % nodes_per_level[level - 1]);
                parent_map[current_node] = parent_id;
                int node_num = nodes_per_level[level]/nodes_per_level[level - 1];
                // 为父节点分配子节点数组
                if (child_map[parent_id] == NULL) {
                    child_map[parent_id] = new int[node_num];
                    for (int j = 0; j < node_num; j++) {
                        child_map[parent_id][j] = -1;
                    }
                }
                
                // 添加子节点
                for (int j = 0; j < node_num; j++) {
                    if (child_map[parent_id][j] == -1) {
                        child_map[parent_id][j] = current_node;
                        break;
                    }
                }
            }
        }
        
        node_id += nodes_per_level[level];
    }
    
    // 根节点无父节点
    parent_map[0] = -1;
    
    cout << "层次化映射建立完成" << endl;
}

void NoC::buildButterfly()
{
	return;

}

void NoC::buildBaseline()
{
	return;

}

void NoC::buildMesh()
{
	return;

}

void NoC::buildOmega()
{
	return;

}

//======================================================================
// 方法: setupHierarchicalConnections()
// 描述: 建立层次化节点间的连接
//======================================================================
void NoC::setupHierarchicalConnections()
{
    cout << "建立层次化连接..." << endl;
    
    // 为每个节点创建downstream_ready信号
    sc_signal<int>** ready_signals = new sc_signal<int>*[total_nodes];
    for (int i = 0; i < total_nodes; i++) {
        ready_signals[i] = new sc_signal<int>();
    }
    
    // 遍历所有节点，建立层次化连接
    for (int i = 0; i < total_nodes; i++) {
        int level = node_level_map[i];
        int parent = parent_map[i];
        const int* children = child_map[i];
        
        int child_num = 0;
        if (level < num_levels - 1) {
            child_num = nodes_per_level[level + 1] / nodes_per_level[level];
        }
        
        cout << "节点" << i << "(L" << level << ")";
        
        // 正确的ready信号连接逻辑
        // 规则：downstream_ready_in接收下游的ready信号，downstream_ready_out向上游发送ready信号
        
        // 1. 如果有子节点（是父节点），则监听子节点的ready状态
        if (children != NULL && child_num > 0) {
            cout << " -> [";
            int count = 0;
            for (int j = 0; j < child_num; j++) {
                if (children[j] != -1) {
                    cout << children[j];
                    if (count < child_num - 1) cout << ",";
                    
                    // 父节点的downstream_ready_in[j]监听子节点j的ready信号
                    if (j >= t_h[i]->pe->downstream_ready_in.size()) {
                        t_h[i]->pe->downstream_ready_in.resize(j + 1);
						 std::string port_name = "hub_" + std::to_string(i) + "_pe_downstream_ready_in_" + std::to_string(j);
						 t_h[i]->pe->downstream_ready_in[j] = new sc_in<int>(port_name.c_str());
                    }


                    t_h[i]->pe->downstream_ready_in[j]->bind(*ready_signals[children[j]]);
                    count++;
                }
            }
            cout << "]";
        }
        
        // 2. 所有节点都向上游（父节点）报告自己的ready状态
        t_h[i]->pe->downstream_ready_out(*ready_signals[i]);  // 当前节点发送自己的ready状态
        
        // 3. 处理特殊节点
        if (parent == -1) {
            cout << " <- ROOT";
        } else {
            cout << " <- " << parent;
        }
        
        if (children == NULL || child_num == 0) {
            // 叶子节点没有下游，downstream_ready_in需要连接
            if (t_h[i]->pe->downstream_ready_in.empty()) {
                t_h[i]->pe->downstream_ready_in.resize(1);
				std::string port_name = "leaf_" + std::to_string(i) + "_pe_downstream_ready_in_0";
				t_h[i]->pe->downstream_ready_in[0] = new sc_in<int>(port_name.c_str());
            }
            sc_signal<int>* dummy_signal = new sc_signal<int>();
            dummy_signal->write(1); // 叶子节点的downstream_ready_in永远ready（因为没有下游）
            t_h[i]->pe->downstream_ready_in[0]->bind(*dummy_signal);
            cout << " <- LEAF";
        }
        
        cout << endl;
    }
    
    // 打印层次化结构信息
    cout << "\n层次化连接建立完成：" << endl;
    cout << "根节点: 0" << endl;
    cout << "中间节点: 1-4" << endl;
    cout << "叶子节点: 5-20" << endl;
    cout << "父子关系: 0->[1,2,3,4], 1->[5,9,13,17], 2->[6,10,14,18], 3->[7,11,15,19], 4->[8,12,16,20]" << endl;
}

//======================================================================
// 层次化拓扑辅助方法实现
//======================================================================

int NoC::getParentNode(int node_id)
{
    if (node_id >= 0 && node_id < total_nodes) {
        return parent_map[node_id];
    }
    return -1;
}

const int* NoC::getChildNodes(int node_id)
{
    if (node_id >= 0 && node_id < total_nodes) {
        return child_map[node_id];
    }
    return NULL;
}

int NoC::getLevelOfNode(int node_id)
{
    if (node_id >= 0 && node_id < total_nodes) {
        return node_level_map[node_id];
    }
    return -1;
}

Tile *NoC::searchNode(const int id) const
{
    if (GlobalParams::topology == TOPOLOGY_MESH) 
    {
	for (int i = 0; i < GlobalParams::mesh_dim_x; i++)
	    for (int j = 0; j < GlobalParams::mesh_dim_y; j++)
		if (t[i][j]->r->local_id == id)
		    return t[i][j];
    }
    else // in delta topologies id equals to the vector index
	    return core[id];
    return NULL;
}

void NoC::asciiMonitor()
{
	//cout << sc_time_stamp().to_double()/GlobalParams::clock_period_ps << endl;
	system("clear");
	//
	// asciishow proof-of-concept #1 free slots

	if (GlobalParams::topology != TOPOLOGY_MESH)
	{
		cout << "Delta topologies are not supported for asciimonitor option!";
		assert(false);
	}
	for (int j = 0; j < GlobalParams::mesh_dim_y; j++)
	{
		for (int s = 0; s<3; s++)
		{
			for (int i = 0; i < GlobalParams::mesh_dim_x; i++)
			{
				if (s==0)
					std::printf("|  %d  ",(*t[i][j]->r->buffers[s])[0].getCurrentFreeSlots());
				else
				if (s==1)
					std::printf("|%d   %d", (*t[i][j]->r->buffers[s])[0].getCurrentFreeSlots(), (*t[i][j]->r->buffers[3])[0].getCurrentFreeSlots());
				else
					std::printf("|__%d__",(*t[i][j]->r->buffers[2])[0].getCurrentFreeSlots());
			}
			cout << endl;
		}
	}
}

