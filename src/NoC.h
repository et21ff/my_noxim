/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file represents the top-level testbench
 */

#ifndef __NOXIMNOC_H__
#define __NOXIMNOC_H__

#include <systemc.h>
#include "Tile.h"
#include "GlobalRoutingTable.h"
#include "GlobalTrafficTable.h"
#include "Hub.h"
#include "Channel.h"
#include "TokenRing.h"
#include "GlobalParams.h"
#

using namespace std;

template <typename T>
struct sc_signal_NSWE
{
    sc_signal<T> east;
    sc_signal<T> west;
    sc_signal<T> south;
    sc_signal<T> north;
};

template <typename T>
struct sc_signal_NSWEH
{
    sc_signal<T> east;
    sc_signal<T> west;
    sc_signal<T> south;
    sc_signal<T> north;
    sc_signal<T> to_hub;
    sc_signal<T> from_hub;
};

// New hierarchical signal structure
template <typename T>
struct sc_signal_Hierarchical
{
    sc_signal<T> up;        // 向上级连接
    sc_signal<T> down_0;    // 向下级连接0
    sc_signal<T> down_1;    // 向下级连接1
    sc_signal<T> down_2;    // 向下级连接2
    sc_signal<T> down_3;    // 向下级连接3
};

enum HierarchicalDirection {
    DIR_UP = 0,        // 向上级连接
    DIR_DOWN_0 = 1,   // 向下级连接0
    DIR_DOWN_1 = 2,   // 向下级连接1
    DIR_DOWN_2 = 3,   // 向下级连接2
    DIR_DOWN_3 = 4,   // 向下级连接3
    DIR_HIERARCHICAL_COUNT = 5
};

SC_MODULE(NoC)
{
    std::vector<std::vector<sc_signal<int> *>> pe_ready_signals_x;
    sc_signal<int> dummy_signal;

public:
    bool SwitchOnly; // true if the tile are switch only
    // I/O Ports
    sc_in_clk clock;   // The input clock for the NoC
    sc_in<bool> reset; // The reset signal for the NoC

    // Signals mesh and switch bloc in delta topologies
    sc_signal_NSWEH<bool> **req;
    sc_signal_NSWEH<bool> **ack;
    sc_signal_NSWEH<TBufferFullStatus> **buffer_full_status;
    sc_signal_NSWEH<Flit> **flit;
    sc_signal_NSWE<int> **free_slots;

    // NoP
    sc_signal_NSWE<NoP_data> **nop_data;

    // signals for connecting Core2Hub (just to test wireless in Butterfly)
    sc_signal<Flit> *flit_from_hub;
    sc_signal<Flit> *flit_to_hub;

    sc_signal<bool> *req_from_hub;
    sc_signal<bool> *req_to_hub;

    sc_signal<bool> *ack_from_hub;
    sc_signal<bool> *ack_to_hub;

    sc_signal<TBufferFullStatus> *buffer_full_status_from_hub;
    sc_signal<TBufferFullStatus> *buffer_full_status_to_hub;

    // Hierarchical structure signals - redesigned for tree topology
    sc_signal_Hierarchical<bool> **hierarchical_req;        // 层次化请求信号
    sc_signal_Hierarchical<bool> **hierarchical_ack;        // 层次化应答信号
    sc_signal_Hierarchical<TBufferFullStatus> **hierarchical_buffer_full_status;  // 层次化缓冲区状态
    sc_signal_Hierarchical<Flit> **hierarchical_flit;        // 层次化数据流信号
    
    // Hierarchical topology parameters
    int num_levels;                          // 层次化层级数
    int* nodes_per_level;                    // 每层的节点数数组
    int total_nodes;                         // 总节点数
    int* node_level_map;                     // 节点->层级映射 (1D数组)
    int* parent_map;                         // 节点->父节点映射 (1D数组)
    int** child_map;                         // 节点->子节点映射 (2D数组)

    // Tile storage for hierarchical topology
    Tile ***t;                                 // 2D数组存储所有Tile，按ID索引
    Tile **t_h;                                // 1D数组存储所有Tile，按层级和ID索引
    Tile **core;                              // 核心Tile数组
    
    // Hierarchical connection management
    void setupHierarchicalTopology();         // 设置层次化拓扑
    void setupHierarchicalConnections();      // 建立层次化连接
    int getParentNode(int node_id);           // 获取节点的父节点
    const int* getChildNodes(int node_id);    // 获取节点的子节点数组
    int getLevelOfNode(int node_id);          // 获取节点所在的层级

    map<int, Hub *> hub;
    map<int, Channel *> channel;

    TokenRing *token_ring;

    // Global tables
    GlobalRoutingTable grtable;
    GlobalTrafficTable gttable;

    // Constructor

    SC_CTOR(NoC)
    {
        cout<<"NOC is Initializing"<<endl;

        dummy_signal = sc_signal<bool>("dummy_signal");

        if (GlobalParams::topology == TOPOLOGY_MESH)
            // Build the Mesh
            buildMesh();
        else if (GlobalParams::topology == TOPOLOGY_BUTTERFLY)
            buildButterfly();
        else if (GlobalParams::topology == TOPOLOGY_BASELINE)
            buildBaseline();
        else if (GlobalParams::topology == TOPOLOGY_OMEGA)
            buildOmega();
        else if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL)
            buildHierarchical();
        else
        {
            cerr << "ERROR: Topology " << GlobalParams::topology << " is not yet supported." << endl;
            exit(0);
        }
        GlobalParams::channel_selection = CHSEL_RANDOM;
        // out of yaml configuration (experimental features)
        // GlobalParams::channel_selection = CHSEL_FIRST_FREE;

        if (GlobalParams::ascii_monitor)
        {
            SC_METHOD(asciiMonitor);
            sensitive << clock.pos();
        }
    }

    // Support methods
    Tile *searchNode(const int id) const;

private:
    void buildMesh();
    void buildButterfly();
    void buildBaseline();
    void buildOmega();
    void buildHierarchical();
    void buildCommon();
    void asciiMonitor();
    int *hub_connected_ports;
};

// Hub * dd;

#endif
