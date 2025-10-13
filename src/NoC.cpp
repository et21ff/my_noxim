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
    // hierarchical_req = new sc_signal_Hierarchical<bool>*[total_nodes];
    // hierarchical_ack = new sc_signal_Hierarchical<bool>*[total_nodes];
    // hierarchical_buffer_full_status = new sc_signal_Hierarchical<TBufferFullStatus>*[total_nodes];
    // hierarchical_flit = new sc_signal_Hierarchical<Flit>*[total_nodes];
    
    // for (int i = 0; i < total_nodes; i++) {
    //     hierarchical_req[i] = new sc_signal_Hierarchical<bool>();
    //     hierarchical_ack[i] = new sc_signal_Hierarchical<bool>();
    //     hierarchical_buffer_full_status[i] = new sc_signal_Hierarchical<TBufferFullStatus>();
    //     hierarchical_flit[i] = new sc_signal_Hierarchical<Flit>();
    // }
    
    //==================================================================
    // 4. 创建Tile数组 (1D结构，适合层次化拓扑)
    //==================================================================
    t = new Tile*[total_nodes];
    
    //==================================================================
    // 5. 创建和配置所有节点
    //==================================================================
    for (int node_id = 0; node_id < total_nodes; node_id++) {
        char tile_name[64];
        sprintf(tile_name, "HNode_%d", node_id);

        
        // 创建Tile
        t[node_id] = new Tile(tile_name, node_id,node_level_map[node_id]);
        
        // 配置Router
        t[node_id]->r->configure(node_id,GlobalParams::node_level_map[node_id],
                                 GlobalParams::stats_warm_up_time,
                                 GlobalParams::buffer_depth,
                                 grtable);
        t[node_id]->r->power.configureRouter(GlobalParams::flit_size,
                                              GlobalParams::buffer_depth,
                                              GlobalParams::flit_size,
                                              string(GlobalParams::routing_algorithm),
                                              "default");
        
        // 配置ProcessingElement
        t[node_id]->pe->local_id = node_id;
        t[node_id]->pe->traffic_table = &gttable;
        t[node_id]->pe->never_transmit = true;
        
        // 连接时钟和复位
        t[node_id]->clock(clock);
        t[node_id]->reset(reset);

        int level = getLevelOfNode(node_id);
        cout << "创建节点 " << node_id << " (Level " << level << ")" << endl;
    }
    
    //==================================================================
    // 6. 建立层次化连接
    //==================================================================
    // 建立层次化连接关系
// 在 NoC.cpp 的构造函数 SC_CTOR(NoC) 中
// ...
// 为Tile分配内存后 ...

    // 为层次化信号数组分配内存
    hierarchical_flit = new sc_signal<Flit>*[GlobalParams::num_nodes];
    hierarchical_req  = new sc_signal<bool>*[GlobalParams::num_nodes];
    hierarchical_ack  = new sc_signal<bool>*[GlobalParams::num_nodes];
    hierarchical_buffer_full_status = new sc_signal<TBufferFullStatus>*[GlobalParams::num_nodes];
    downstream_ready_signals = new sc_signal<int>*[GlobalParams::num_nodes];


// 为每个节点的连接分配两个方向的信号
for (int i = 0; i < GlobalParams::num_nodes; i++) {
    // 0: C->P (UP), 1: P->C (DOWN)
    hierarchical_flit[i] = new sc_signal<Flit>[2];
    hierarchical_req[i]  = new sc_signal<bool>[2];
    hierarchical_ack[i]  = new sc_signal<bool>[2];
    hierarchical_buffer_full_status[i] = new sc_signal<TBufferFullStatus>[2];
    downstream_ready_signals[i] = new sc_signal<int>[1];
}

for (int i = 0; i < GlobalParams::num_nodes; i++) {
    // 0: C->P (UP), 1: P->C (DOWN)
    hierarchical_flit[i] = new sc_signal<Flit>[2];
    hierarchical_req[i]  = new sc_signal<bool>[2];
    hierarchical_ack[i]  = new sc_signal<bool>[2];
    hierarchical_buffer_full_status[i] = new sc_signal<TBufferFullStatus>[2];
}
    setupHierarchicalConnections();
    
    cout << "=== 层次化NoC拓扑构建完成 ===" << endl;
    cout << "注意: 需要修改Router类支持层次化路由" << endl;

    setupLocalConnections();
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
    writeToGlobalParams();
    
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
// 描述: 建立层次化节点间的双向网络连接
//======================================================================
// 在 NoC.cpp 中
// 在 NoC.cpp 中
void NoC::setupHierarchicalConnections() {
    cout << "[连接] 正在建立 Tile 间的层次化连接..." << endl;

    t[0]->pe->downstream_ready_out.bind(dummy_signal); // make systemc satisfied


    // 遍历所有非根节点（从1开始）
    for (int i = 1; i < GlobalParams::num_nodes; i++) {
        int parent_id = GlobalParams::parent_map[i];
        assert(parent_id != -1 && "每个非根节点都必须有一个父节点");

        // 找到当前节点在其父节点的子节点列表中的索引
        int child_index = -1;
        int num_children = GlobalParams::fanouts_per_level[GlobalParams::node_level_map[parent_id]];
        for (int j = 0; j < num_children; j++) {
            if (GlobalParams::child_map[parent_id][j] == i) {
                child_index = j;
                break;
            }
        }
        assert(child_index != -1 && "无法在父节点的子节点列表中找到当前节点");

        // --- [DEBUG] 打印当前正在处理的连接 ---
        cout << "--- [DEBUG] 开始连接 Node " << i << " (UP) <--> Node " << parent_id << " (DOWN " << child_index << ") ---" << endl;

        // =================================================================
        //                 方向 1: 子节点 -> 父节点
        // =================================================================
        {

                        cout << "  [NOC BIND DEBUG] Child " << i << " UP TX status port addr: " << t[i]->hierarchical_buffer_full_status_up_tx << endl;
            assert(t[i]->hierarchical_buffer_full_status_up_tx != nullptr && "Tile's own UP TX status port is NULL!");

            cout << "  [DEBUG] 方向 C->P: 正在绑定..." << endl;
            
            // --- [DEBUG] 打印关键指针地址 ---
            cout << "    - Child  (UP) TX flit   port addr: " << t[i]->hierarchical_flit_up_tx << endl;
            cout << "    - Parent (DW) RX flit   port addr: " << t[parent_id]->hierarchical_flit_down_rx[child_index] << endl;
            cout << "    - Child  (UP) TX status port addr: " << t[i]->hierarchical_buffer_full_status_up_tx << endl;
            cout << "    - Parent (DW) RX status port addr: " << t[parent_id]->hierarchical_buffer_full_status_down_rx[child_index] << endl;

            // --- [DEBUG] 添加断言进行运行时检查 ---
            assert(t[i]->hierarchical_flit_up_tx != nullptr && "Child UP TX Flit Port is NULL!");
            assert(t[parent_id]->hierarchical_flit_down_rx[child_index] != nullptr && "Parent DOWN RX Flit Port is NULL!");
            assert(t[i]->hierarchical_buffer_full_status_up_tx != nullptr && "Child UP TX Status Port is NULL!");
            assert(t[parent_id]->hierarchical_buffer_full_status_down_rx[child_index] != nullptr && "Parent DOWN RX Status Port is NULL!");

            // --- 执行绑定 ---
            t[i]->hierarchical_flit_up_tx->bind(hierarchical_flit[i][0]);
            t[i]->hierarchical_req_up_tx->bind(hierarchical_req[i][0]);
            t[i]->hierarchical_ack_up_tx->bind(hierarchical_ack[i][0]);
            t[i]->hierarchical_buffer_full_status_up_tx->bind(hierarchical_buffer_full_status[i][0]);
            
            t[parent_id]->hierarchical_flit_down_rx[child_index]->bind(hierarchical_flit[i][0]);
            t[parent_id]->hierarchical_req_down_rx[child_index]->bind(hierarchical_req[i][0]);
            t[parent_id]->hierarchical_ack_down_rx[child_index]->bind(hierarchical_ack[i][0]);
            t[parent_id]->hierarchical_buffer_full_status_down_rx[child_index]->bind(hierarchical_buffer_full_status[i][0]);



            t[parent_id]->pe->downstream_ready_in[child_index]->bind(downstream_ready_signals[i][0]);
            t[i]->pe->downstream_ready_out.bind(downstream_ready_signals[i][0]);
            
            cout << "    - C->P Bind OK." << endl;
        }

        // =================================================================
        //                 方向 2: 父节点 -> 子节点
        // =================================================================
        {


            cout << "  [DEBUG] 方向 P->C: 正在绑定..." << endl;

            // --- [DEBUG] 打印关键指针地址 ---
            cout << "    - Parent (DW) TX flit   port addr: " << t[parent_id]->hierarchical_flit_down_tx[child_index] << endl;
            cout << "    - Child  (UP) RX flit   port addr: " << t[i]->hierarchical_flit_up_rx << endl;
            cout << "    - Parent (DW) TX status port addr: " << t[parent_id]->hierarchical_buffer_full_status_down_tx[child_index] << endl;
            cout << "    - Child  (UP) RX status port addr: " << t[i]->hierarchical_buffer_full_status_up_rx << endl;
            
            // --- [DEBUG] 添加断言进行运行时检查 ---
            assert(t[parent_id]->hierarchical_flit_down_tx[child_index] != nullptr && "Parent DOWN TX Flit Port is NULL!");
            assert(t[i]->hierarchical_flit_up_rx != nullptr && "Child UP RX Flit Port is NULL!");
            assert(t[parent_id]->hierarchical_buffer_full_status_down_tx[child_index] != nullptr && "Parent DOWN TX Status Port is NULL!");
            assert(t[i]->hierarchical_buffer_full_status_up_rx != nullptr && "Child UP RX Status Port is NULL!");

            // --- 执行绑定 ---
            t[parent_id]->hierarchical_flit_down_tx[child_index]->bind(hierarchical_flit[i][1]);
            t[parent_id]->hierarchical_req_down_tx[child_index]->bind(hierarchical_req[i][1]);
            t[parent_id]->hierarchical_ack_down_tx[child_index]->bind(hierarchical_ack[i][1]);
            t[parent_id]->hierarchical_buffer_full_status_down_tx[child_index]->bind(hierarchical_buffer_full_status[i][1]);
            
            t[i]->hierarchical_flit_up_rx->bind(hierarchical_flit[i][1]);
            t[i]->hierarchical_req_up_rx->bind(hierarchical_req[i][1]);
            t[i]->hierarchical_ack_up_rx->bind(hierarchical_ack[i][1]);
            t[i]->hierarchical_buffer_full_status_up_rx->bind(hierarchical_buffer_full_status[i][1]);

            cout << "    - P->C Bind OK." << endl;
        }
        cout << "--- [DEBUG] Node " << i << " 连接完成 ---" << endl << endl;
    }
    cout << "[连接] Tile 间的层次化连接建立完成。" << endl;
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
    if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) 
    {
        for(int i=0; i<total_nodes; i++)
		if (t[i]->r->local_id == id)
		    return t[i];
    }
    return NULL;
}

void NoC::writeToGlobalParams()
{
    // Make hierarchical topology information available globally
    GlobalParams::node_level_map = node_level_map;
    GlobalParams::parent_map = parent_map;
    GlobalParams::child_map = child_map;
    GlobalParams::num_nodes = total_nodes;
    GlobalParams::num_levels = num_levels;
    
    cout << "已将拓扑映射信息写入GlobalParams" << endl;
}

void NoC::asciiMonitor()
{
	//cout << sc_time_stamp().to_double()/GlobalParams::clock_period_ps << endl;
	system("clear");
	//
	// asciishow proof-of-concept #1 free slots

	if (GlobalParams::topology != TOPOLOGY_HIERARCHICAL)
	{
		cout << "Delta topologies are not supported for asciimonitor option!";
		assert(false);
	}
	for (int i = 0; i < GlobalParams::num_nodes; i++)
	{
		for (int s = 0; s<3; s++)
		{
			{
				if (s==0)
					std::printf("|  %d  ",(*t[i]->r->buffers[s])[0].getCurrentFreeSlots());
				else
				if (s==1)
					std::printf("|%d   %d", (*t[i]->r->buffers[s])[0].getCurrentFreeSlots(), (*t[i]->r->buffers[3])[0].getCurrentFreeSlots());
				else
					std::printf("|__%d__",(*t[i]->r->buffers[2])[0].getCurrentFreeSlots());
			}
			cout << endl;
		}
	}
}

void NoC::setupLocalConnections() {
    // cout << "[连接] 正在建立 Tile 和 MockPE 间的本地连接..." << endl;

    // 将这些信号声明为 NoC 的成员变量，以便在析构函数中释放它们
    // 例如在 NoC.h 中:
    // std::vector<sc_signal<Flit>*> local_flit_signals;
    // ... 其他信号向量 ...

    for (int i = 0; i < GlobalParams::num_nodes; i++) {
        
    if(node_level_map[i] > 0) {
        t[i]->r->h_flit_rx_up->bind(*(t[i]->hierarchical_flit_up_rx));
        t[i]->r->h_req_rx_up->bind(*(t[i]->hierarchical_req_up_rx));
	    t[i]->r->h_ack_rx_up->bind(*(t[i]->hierarchical_ack_up_rx));
        t[i]->r->h_buffer_full_status_rx_up->bind(*(t[i]->hierarchical_buffer_full_status_up_rx));

        t[i]->r->h_flit_tx_up->bind(*(t[i]->hierarchical_flit_up_tx));
	    t[i]->r->h_req_tx_up->bind(*(t[i]->hierarchical_req_up_tx));
	    t[i]->r->h_ack_tx_up->bind(*(t[i]->hierarchical_ack_up_tx));
	    t[i]->r->h_buffer_full_status_tx_up->bind(*(t[i]->hierarchical_buffer_full_status_up_tx));

        cout<< "[连接] Tile"<<i<< "和其Router的UP绑定建立完成。" << endl;
    }
        dbg(i,GlobalParams::node_level_map[i],GlobalParams::fanouts_per_level[GlobalParams::node_level_map[i]]);

    for(int j=0;j<GlobalParams::fanouts_per_level[GlobalParams::node_level_map[i]];j++) {
        t[i]->r->h_flit_rx_down[j]->bind(*(t[i]->hierarchical_flit_down_rx[j]));
        t[i]->r->h_req_rx_down[j]->bind(*(t[i]->hierarchical_req_down_rx[j]));
        t[i]->r->h_ack_rx_down[j]->bind(*(t[i]->hierarchical_ack_down_rx[j]));
        t[i]->r->h_buffer_full_status_rx_down[j]->bind(*(t[i]->hierarchical_buffer_full_status_down_rx[j]));

        t[i]->r->h_flit_tx_down[j]->bind(*(t[i]->hierarchical_flit_down_tx[j]));
        t[i]->r->h_req_tx_down[j]->bind(*(t[i]->hierarchical_req_down_tx[j]));
        t[i]->r->h_ack_tx_down[j]->bind(*(t[i]->hierarchical_ack_down_tx[j]));
        t[i]->r->h_buffer_full_status_tx_down[j]->bind(*(t[i]->hierarchical_buffer_full_status_down_tx[j]));
        
        cout<< "[连接] Tile"<<i<< "和其Router的DOWN"<<j<<"绑定建立完成。" << endl;
    }

}


}

NoC::~NoC() {
    // 这是一个示例，您需要根据您实际分配的成员变量来编写
    
    // 释放 Tiles
    for (int i = 0; i < GlobalParams::num_nodes; i++) {
        delete t[i]; // t[i] 是通过 new Tile(...) 创建的
    }
    delete[] t; // t 本身是通过 new Tile*[] 创建的

    // 释放 PEs (如果您是在NoC中创建的)
    // ...

    // 释放所有 sc_signal 数组
    // 例如，为层次化连接创建的信号
    for (int i = 1; i < GlobalParams::num_nodes; i++) {
        // 确保指针非空
        if (hierarchical_flit[i] != nullptr) delete[] hierarchical_flit[i];
        if (hierarchical_req[i] != nullptr) delete[] hierarchical_req[i];
        // ...
    }
    delete[] hierarchical_flit;
    delete[] hierarchical_req;
    // ...

    // 释放其他任何在构造函数中 new 出来的东西
    // delete grtable; // 假设 grtable 是 new 出来的
}