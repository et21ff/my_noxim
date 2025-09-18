/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the declaration of the tile
 */

#ifndef __NOXIMTILE_H__
#define __NOXIMTILE_H__

#include <systemc.h>
#include "Router.h"
#include "ProcessingElement.h"
#include "MockPE.h"
#include <dbg.h>
using namespace std;

SC_MODULE(Tile)
{
    SC_HAS_PROCESS(Tile);

    

    // I/O Ports
    sc_in_clk clock;		                // The input clock for the tile
    sc_in <bool> reset;	                        // The reset signal for the tile

    int local_id; // Unique ID
	int local_level; // Level in the hierarchy


    // Hierarchical ports for tree topology (dynamic vector-based)
    // UP ports (connection to parent)
    sc_in <Flit>* hierarchical_flit_up_rx;	        // UP方向输入 (from parent)
    sc_in <bool>* hierarchical_req_up_rx;	        // UP方向请求输入
    sc_out <bool>* hierarchical_ack_up_rx;	        // UP方向应答输出
    sc_out <TBufferFullStatus>* hierarchical_buffer_full_status_up_rx;

	sc_out <Flit>* hierarchical_flit_up_tx;	        // UP方向输出 (to parent)
	sc_out <bool>* hierarchical_req_up_tx;	        // UP方向请求输出
	sc_in <bool>* hierarchical_ack_up_tx;	        // UP方向应答输入
	sc_in <TBufferFullStatus>* hierarchical_buffer_full_status_up_tx;

    // DOWN ports (connection to children) - dynamic vectors
    std::vector<sc_out<Flit>*> hierarchical_flit_down_tx;     // DOWN方向输出 (to children)
    std::vector<sc_out<bool>*> hierarchical_req_down_tx;      // DOWN方向请求输出
    std::vector<sc_in<bool>*> hierarchical_ack_down_tx;       // DOWN方向应答输入
    std::vector<sc_in<TBufferFullStatus>*> hierarchical_buffer_full_status_down_tx;

	std::vector<sc_in<Flit>*> hierarchical_flit_down_rx;      // DOWN方向输入 (from children)
	std::vector<sc_in<bool>*> hierarchical_req_down_rx;       // DOWN方向请求输入
	std::vector<sc_out<bool>*> hierarchical_ack_down_rx;      // DOWN方向应答输出
	std::vector<sc_out<TBufferFullStatus>*> hierarchical_buffer_full_status_down_rx;

    sc_signal<Flit>*  sig_flit_p2r;
    sc_signal<bool>*  sig_req_p2r;
    // 反向信号: Router -> PE
    sc_signal<bool>*  sig_ack_r2p;
    sc_signal<TBufferFullStatus>* sig_stat_r2p;

        // --- 链路 2: 数据从 Router 发往 PE (R2P) ---
    sc_signal<Flit>*  sig_flit_r2p;
    sc_signal<bool>*  sig_req_r2p;  
    // 反向信号: PE -> Router
    sc_signal<bool>*  sig_ack_p2r;  
    sc_signal<TBufferFullStatus>* sig_stat_p2r;

    // Hierarchical port management
    void initHierarchicalPorts();  // 初始化层次化端口
    void cleanupHierarchicalPorts();   
      // 清理层次化端口

    // Destructor
    ~Tile() {
        cleanupHierarchicalPorts();
        delete r;
        delete pe;
    }

    // Signals required for Router-PE connection (Primary - LOCAL)
    // sc_signal <Flit> flit_rx_local;	
    // sc_signal <bool> req_rx_local;     
    // sc_signal <bool> ack_rx_local;
    // sc_signal <TBufferFullStatus> buffer_full_status_rx_local;

    // sc_signal <Flit> flit_tx_local;
    // sc_signal <bool> req_tx_local;
    // sc_signal <bool> ack_tx_local;
    // sc_signal <TBufferFullStatus> buffer_full_status_tx_local;
 
    // // Signals required for Router-PE connection (Secondary - LOCAL_2)
    // sc_signal <Flit> flit_rx_local_2;	
    // sc_signal <bool> req_rx_local_2;     
    // sc_signal <bool> ack_rx_local_2;
    // sc_signal <TBufferFullStatus> buffer_full_status_rx_local_2;

    // sc_signal <Flit> flit_tx_local_2;
    // sc_signal <bool> req_tx_local_2;
    // sc_signal <bool> ack_tx_local_2;
    // sc_signal <TBufferFullStatus> buffer_full_status_tx_local_2;





    Router *r;		                // Router instance
    MockPE *pe;	                // Processing Element instance
	    GlobalRoutingTable grtable;


// 在 Tile.cpp 中
Tile(sc_module_name nm, int id, int level);





};

#endif
