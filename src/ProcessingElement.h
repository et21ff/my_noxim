/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the declaration of the processing element
 */

#ifndef __NOXIMPROCESSINGELEMENT_H__
#define __NOXIMPROCESSINGELEMENT_H__

#include <queue>
#include <systemc.h>

#include "DataStructs.h"
#include "GlobalTrafficTable.h"
#include "Utils.h"

using namespace std;

SC_MODULE(ProcessingElement)
{

    // I/O Ports

    // **** 我们新增的、用于握手的端口 ****
    sc_in<int> downstream_ready_in; // 从下游PE接收的"ready"信号
    sc_out<int> downstream_ready_out; // 向上游PE发送的"ready"信号

    sc_in_clk clock;		// The input clock for the PE
    sc_in < bool > reset;	// The reset signal for the PE

    sc_in < Flit > flit_rx;	// The input channel
    sc_in < bool > req_rx;	// The request associated with the input channel
    sc_out < bool > ack_rx;	// The outgoing ack signal associated with the input channel
    sc_out < TBufferFullStatus > buffer_full_status_rx;	

    sc_out < Flit > flit_tx;	// The output channel
    sc_out < bool > req_tx;	// The request associated with the output channel
    sc_in < bool > ack_tx;	// The outgoing ack signal associated with the output channel
    sc_in < TBufferFullStatus > buffer_full_status_tx;

    sc_in < int >free_slots_neighbor;

    // Registers
    int local_id;		// Unique identification number
    bool current_level_rx;	// Current level for Alternating Bit Protocol (ABP)
    bool current_level_tx;	// Current level for Alternating Bit Protocol (ABP)
    queue < Packet > packet_queue;	// Local queue of packets
    bool transmittedAtPreviousCycle;	// Used for distributions with memory

    // Functions
    void rxProcess();		// The receiving process
    void txProcess();		// The transmitting process
    bool canShot(Packet & packet);	// True when the packet must be shot
    Flit nextFlit();	// Take the next flit of the current packet
    Packet trafficTest();	// used for testing traffic
    Packet trafficRandom();	// Random destination distribution
    Packet trafficTranspose1();	// Transpose 1 destination distribution
    Packet trafficTranspose2();	// Transpose 2 destination distribution
    Packet trafficBitReversal();	// Bit-reversal destination distribution
    Packet trafficShuffle();	// Shuffle destination distribution
    Packet trafficButterfly();	// Butterfly destination distribution
    Packet trafficLocal();	// Random with locality
    Packet trafficULocal();	// Random with locality

    GlobalTrafficTable *traffic_table;	// Reference to the Global traffic Table
    bool never_transmit;	// true if the PE does not transmit any packet 
    //  (valid only for the table based traffic)

    void fixRanges(const Coord, Coord &);	// Fix the ranges of the destination
    int randInt(int min, int max);	// Extracts a random integer number between min and max
    int getRandomSize();	// Returns a random size in flits for the packet
    void setBit(int &x, int w, int v);
    int getBit(int x, int w);
    double log2ceil(double x);

    int roulett();
    int findRandomDestination(int local_id,int hops);
    unsigned int getQueueSize() const;
public:
    // --- 角色定义 ---
    enum PE_Role { ROLE_UNUSED,ROLE_GLB, ROLE_SPAD, ROLE_COMPUTE };
    PE_Role role;

    int max_capacity;

    // --- 生产者状态 (GLB and SPAD) ---
    int transfer_chunk_size;
    int current_downstream_target_index;
    std::vector<int> downstream_node_ids;


    // --- 消费者状态 (ComputePE) ---
    bool is_computing;
    int compute_cycles_left;
    bool is_stalled_waiting_for_data;
    int required_data_per_compute;
    int  receive_chunk_size;; // 期望从上级获取的数据量

    sc_signal<int> current_data_size;
    sc_signal<bool> is_receiving_packet;
    std::string role_to_str(const PE_Role& role); // 用于将角色转换为字符串
     void update_ready_signal(); // 一个新的SC_METHOD,用于向上级存储器更新当前空闲状态
    void pe_init();
    void run_storage_logic();
    void run_glb_logic();
    void run_spad_logic();
    void run_compute_logic();
    // Constructor
    SC_CTOR(ProcessingElement) {
    SC_METHOD(pe_init);
    sensitive << reset;


	SC_METHOD(rxProcess);
	sensitive << reset;
	sensitive << clock.pos();

	SC_METHOD(txProcess);
	sensitive << reset;
	sensitive << clock.pos();

    SC_METHOD(update_ready_signal);
    sensitive << reset;
    sensitive << current_data_size;      // 3. 直接对信号敏感
    sensitive << is_receiving_packet;    // 3. 直接对信号敏感


    }

};

#endif
