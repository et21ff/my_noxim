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
#include <map>

#include "DataStructs.h"
#include "GlobalTrafficTable.h"
#include "Utils.h"
#include "smartbuffer/BufferManager.h"
#include <memory>
#include "dbg.h"
#include "taskmanager/TaskManager.h"
#include "GlobalParams.h"
#include "Buffer.h"

// Hierarchical topology configuration structures
#define NUM_LOCAL_PORTS 1

using namespace std;

SC_MODULE(ProcessingElement)
{

    // I/O Ports

    // **** 我们新增的、用于握手的端口 ****

    sc_in_clk clock;		// The input clock for the PE
    sc_in < bool > reset;	// The reset signal for the PE

    // Primary and Secondary connections as arrays
    sc_in < Flit > flit_rx[NUM_LOCAL_PORTS];	// The input channels [0: PRIMARY, 1: SECONDARY]
    sc_in < bool > req_rx[NUM_LOCAL_PORTS];	// The request signals associated with input channels
    sc_out < bool > ack_rx[NUM_LOCAL_PORTS];	// The outgoing ack signals associated with input channels
    sc_out < TBufferFullStatus > buffer_full_status_rx[NUM_LOCAL_PORTS];	

    sc_out < Flit > flit_tx[NUM_LOCAL_PORTS];	// The output channels [0: PRIMARY, 1: SECONDARY]
    sc_out < bool > req_tx[NUM_LOCAL_PORTS];	// The request signals associated with output channels
    sc_in < bool > ack_tx[NUM_LOCAL_PORTS];	// The incoming ack signals associated with output channels
    sc_in < TBufferFullStatus > buffer_full_status_tx[NUM_LOCAL_PORTS];


    // Registers
    int local_id;		// Unique identification number
    bool current_level_rx[NUM_LOCAL_PORTS];	// Current level for Alternating Bit Protocol (ABP)
    bool current_level_tx[NUM_LOCAL_PORTS];	// Current level for Alternating Bit Protocol (ABP)
    std::vector<std::queue<Packet>> packet_queues_;  // VC-aware packet queues
    bool transmittedAtPreviousCycle;	// Used for distributions with memory

    BufferBank rx_buffer; // 物理输入缓冲区

    // [新增] 用于跟踪正在接收、但未完全提交到逻辑缓冲区的数据的总大小
    size_t main_receiving_size_;
    size_t output_receiving_size_;

    // Functions
    void rxProcess();		// The receiving process
    void txProcess();		// The transmitting process
    Flit nextFlit();	// Take the next flit of the current packet
    Flit nextOutputFlit();
    
    // Traffic-related functions removed (not used in current implementation)
    // bool canShot(Packet & packet);
    // Packet trafficTest();
    // Packet trafficRandom();
    // Packet trafficTranspose1();
    // Packet trafficTranspose2();
    // Packet trafficBitReversal();
    // Packet trafficShuffle();
    // Packet trafficButterfly();
    // Packet trafficLocal();
    // Packet trafficULocal();

    GlobalTrafficTable *traffic_table;	// Reference to the Global traffic Table
    bool never_transmit;	// true if the PE does not transmit any packet 
    //  (valid only for the table based traffic)

    // Utility functions - only keeping used ones
    int randInt(int min, int max);	// Extracts a random integer number between min and max
    
    // Unused utility functions removed
    // void fixRanges(const Coord, Coord &);
    // int getRandomSize();
    // void setBit(int &x, int w, int v);
    // int getBit(int x, int w);
    // double log2ceil(double x);
    // int roulett();
    // int findRandomDestination(int local_id,int hops);
    // unsigned int getQueueSize() const;
public:
    //========================================================================
    // I. 核心身份与物理属性 (Core Identity & Physical Attributes)
    //========================================================================
    
    
    PE_Role role;

    int max_capacity;     // 单位: Bytes
    sc_signal<int> current_data_size; // 单位: Bytes

    // --- 新增：LivenessAwareBuffer 现在是缓冲区管理的核心 ---
    std::unique_ptr<BufferManager> buffer_manager_; // <--- 3. 添加 BufferManager 实例
    std::unique_ptr<BufferManager> output_buffer_manager_; // <--- 新增：output smartbuffer
    std::unique_ptr<TaskManager> task_manager_;   // 任务管理器实例
    DispatchTask current_dispatch_task_; // 当前任务
    size_t outputs_received_count_;
    size_t outputs_required_count_;
    
    //========================================================================
    // II. 网络接口与通信 (Network Interface & Communication)
    //========================================================================

    // --- 与下游的连接 ---
    std::vector<int> downstream_node_ids;
    std::vector<int> upstream_node_ids; 
    int current_downstream_target_index; // 用于多播或轮询

    //========================================================================
    // III. 任务与循环控制 (Task & Loop Control) - 通用机制
    //========================================================================
    
    struct LoopTask {
        int iterations;
    };

    // --- 接收/消耗任务队列 ---
    std::vector<int> receive_task_queue;
    std::vector<int>      receive_loop_counters;
    int                   current_receive_loop_level;
    bool                  all_receive_tasks_finished;

    // --- 发送/生产任务队列 ---
    std::vector<LoopTask> transfer_task_queue;
    std::vector<int>      transfer_loop_counters;
    int                   current_transfer_loop_level;
    bool                  all_transfer_tasks_finished;

    // --- 通用阶段控制 ---
    enum DataStage { STAGE_FILL, STAGE_DELTA };
    DataStage current_stage;

    //========================================================================
    // IV. 特定角色的行为属性 (Role-Specific Behavioral Attributes)
    //========================================================================
    
    // --- 专属于“生产者” (DRAM, GLB) ---
    int transfer_fill_size;   // 发送FILL包的大小 (Bytes)
    int transfer_delta_size;  // 发送DELTA包的大小 (Bytes)
    int total_bytes_sent; // 总发送量 (Bytes)

    // --- 专属于“最终消费者” (Buffer_PE) ---
    bool is_consuming;         // 是否正在进行抽象消耗
    int  consume_cycles_left;  // 抽象消耗剩余周期

    //========================================================================
    // VI. GLB 专用：基于 Map 的发送同步机制 (GLB Dispatch Sync Mechanism)
    //========================================================================


    bool dispatch_in_progress_ = false;                // 标记是否正在进行一轮发送

    // --- GLB 逻辑时间戳控制 ---

  // 新增：层次化配置相关成员变量
    int level_index;  // 当前PE所在的层级索引

public: // 建议将内部状态变量设为私有
    //========================================================================
    // V. 内部状态与辅助变量 (Internal State & Helper Variables)
    //========================================================================

    int logical_timestamp;
    // +++ 新增：一个专门用于通知缓冲区状态改变的事件 +++
    sc_event buffer_state_changed_event;
    sc_event output_buffer_state_changed_event; // 新增：output buffer状态改变事件
    int command_to_send;
    std::map<int, int> pending_commands_;
    bool compute_in_progress_;
    bool is_compute_complete;
    int compute_cycles;


    sc_signal<bool> is_receiving_packet;
    std::string role_to_str(const PE_Role& role); // 用于将角色转换为字符串 // 一个新的SC_METHOD,用于向上级存储器更新当前空闲状态
    void run_storage_logic();
    void run_compute_logic();

    int get_command_to_send();

    void reset_logic(); //达到timestamp上限时的重置行为

    void internal_transfer_process();

    // 新增：统一的VC发送处理函数
    void handle_tx_for_all_vcs();

    // 新增：辅助函数
    bool packet_queues_are_empty() const;
    int get_vc_id_for_packet(const Packet& pkt) const;
    int get_vc_id_for_packet_by_task(DataDispatchInfo type) const;

    int find_child_id(int id);
    Flit generate_next_flit_from_queue(std::queue<Packet>& queue);

    unsigned int getQueueSize() const;

    // 新增：动态配置函数
    void configure(int id, int level_idx, const HierarchicalConfig& topology_config); 
    // Constructor
    

    SC_CTOR(ProcessingElement) {
        // 初始化新的成员变量
        main_receiving_size_ = 0;
        output_receiving_size_ = 0;

        // 初始化VC队列
        packet_queues_.resize(GlobalParams::n_virtual_channels);

    // SC_METHOD(pe_init);
    // sensitive << reset;


        SC_METHOD(rxProcess);
        sensitive << reset;
        sensitive << clock.neg();

        SC_METHOD(txProcess);
        sensitive << reset;
        sensitive << clock.pos();

    }

};

#endif
