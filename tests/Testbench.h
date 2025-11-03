#pragma once // 确保头文件只被包含一次

#include "systemc.h"
#include "ProcessingElement.h" // 包含我们待测的模块
#include <queue>
#include <vector>

// -----------------------------------------------------------------------------
// Testbench Module Definition
// -----------------------------------------------------------------------------

SC_MODULE(Testbench) {

    // =========================================================================
    // 1. 时钟、复位和待测设备 (DUT)
    // =========================================================================
    sc_in_clk clock;
    sc_signal<bool> reset;
    
    ProcessingElement* dut; // 指向我们待测的 ProcessingElement
    ProcessingElement* sender_pe; // 用于发送数据的 PE 模拟器实例
    // =========================================================================
    // 2. 用于连接 DUT 所有端口的信号
    // =========================================================================

    // --- 输入到 DUT 的信号 (由 Testbench 驱动) ---
    // Port 0 (主数据)
    sc_signal<Flit> flit_to_dut[2];       // flit_rx[0]
    sc_signal<bool> req_to_dut[2];        // req_rx[0]
    sc_signal<bool> ack_to_dut[2];        // ack_tx[0]
    sc_signal<TBufferFullStatus> buffer_full_status_to_dut[2]; // buffer_full_status_rx[0]
    // Port 1 (输出数据)
    // sc_signal<Flit> flit_to_dut_1;     // flit_rx[1]
    // sc_signal<bool> req_to_dut_1;      // req_rx[1]
    // sc_signal<bool> ack_to_dut_1;      // ack_tx[1]

    // Ready 信号 (从下游模拟器 -> DUT)
    sc_signal<int> ready_to_dut[2];

    // --- 从 DUT 输出的信号 (由 Testbench 监控) ---
    // Port 0 (主数据)
    sc_signal<Flit> flit_from_dut[2];     // flit_tx[0]
    sc_signal<bool> req_from_dut[2];      // req_tx[0]
    sc_signal<bool> ack_from_dut[2];      // ack_rx[0]
    sc_signal<TBufferFullStatus> buffer_full_status_from_dut[2]; // buffer_full_status_tx[0]
    // Port 1 (输出数据)
    // sc_signal<Flit> flit_from_dut_1;   // flit_tx[1]
    // sc_signal<bool> req_from_dut_1;    // req_tx[1]
    // sc_signal<bool> ack_from_dut_1;    // ack_rx[1]
    int current_level_rx[2];
    // Ready 信号 (从 DUT -> 上游模拟器)
    sc_signal<int> ready_from_dut;
    sc_signal<int> dummy;


    // =========================================================================
    // 3. Testbench 内部状态和数据记录
    // =========================================================================
    
    // 存储从 DUT 接收到的 Flit，用于后续验证
    std::queue<Flit> received_flits[2]; 


    // =========================================================================
    // 4. 模拟进程 (Processes)
    // =========================================================================

    void downstream_mock_process();

    /**
     * @brief [修正版] 监控并记录 DUT 发送的 Flit (只负责观察)
     */
    void monitor_flit_output_process();
    
    // 我们将在下一步实现 test_scenario_driver 线程
    void run_test_scenario(); 

    // =========================================================================
    // 5. 构造函数 - 完成所有实例化和端口绑定
    // =========================================================================
    SC_CTOR(Testbench) { // 1ns 时钟周期
        
        // --- 实例化 DUT ---
        dut = new ProcessingElement("DUT_PE");
        
        // --- 绑定时钟和复位 ---
        dut->reset(reset);
        dut->clock(clock);
        dut->local_id = 2; // 设置一个本地 ID，视测试需求而定
        
        // --- 绑定端口数组 (使用循环，代码更简洁) ---
        for (int i = 0; i < 2; ++i) {
            // RX (输入到 DUT)
            dut->flit_rx[i](flit_to_dut[i]);
            dut->req_rx[i](req_to_dut[i]);
            dut->ack_rx[i](ack_from_dut[i]);
            dut->buffer_full_status_rx[i](buffer_full_status_to_dut[i]);
            
            // TX (从 DUT 输出)
            dut->flit_tx[i](flit_from_dut[i]);
            dut->req_tx[i](req_from_dut[i]);
            dut->ack_tx[i](ack_to_dut[i]);
            dut->buffer_full_status_tx[i](buffer_full_status_from_dut[i]);
        }
        
        // --- 绑定 Ready 信号 ---
        // 注意：downstream_ready_in 是一个指针向量
        // 我们需要先将信号转换为指针再 push_back
        dut->downstream_ready_in.reserve(2);
        dut->downstream_ready_in[0] = new sc_in<int>();
        dut->downstream_ready_in[1] = new sc_in<int>();
        dut->downstream_ready_in[0]->bind(ready_to_dut[0]);
        dut->downstream_ready_in[1]->bind(ready_to_dut[1]);
        dut->downstream_ready_out(ready_from_dut);
        dut->configure(2,2,GlobalParams::hierarchical_config);

        sender_pe = new ProcessingElement("Sender_PE");
        sender_pe->reset(reset);
        sender_pe->clock(clock);
        sender_pe->local_id = 1; // 设置发送器 PE 的本地 ID
        sender_pe->configure(1,1,GlobalParams::hierarchical_config);

        for (int i = 0; i < 2; ++i)
        {
            // RX (输入到 Sender PE)
            sender_pe->flit_rx[i](flit_from_dut[i]);
            sender_pe->req_rx[i](req_from_dut[i]);
            sender_pe->ack_rx[i](ack_to_dut[i]);
            sender_pe->buffer_full_status_rx[i](buffer_full_status_from_dut[i]);
            
            // TX (从 Sender PE 输出)
            sender_pe->flit_tx[i](flit_to_dut[i]);
            sender_pe->req_tx[i](req_to_dut[i]);
            sender_pe->ack_tx[i](ack_from_dut[i]);
            sender_pe->buffer_full_status_tx[i](buffer_full_status_to_dut[i]);
        }

        sender_pe->downstream_ready_in.reserve(2);
        sender_pe->downstream_ready_in[0] = new sc_in<int>();
        sender_pe->downstream_ready_in[1] = new sc_in<int>();
        sender_pe->downstream_ready_in[0]->bind(ready_from_dut);
        sender_pe->downstream_ready_in[1]->bind(ready_from_dut);
        sender_pe->downstream_ready_out(dummy); // 连接到 DUT 的 ready 输出信号

        // --- 注册进程 ---
        SC_THREAD(run_test_scenario); // 我们的主测试驱动线程

        // SC_METHOD(downstream_mock_process);
        // sensitive << clock.pos();

        // SC_METHOD(monitor_flit_output_process);
        // sensitive << clock.pos();

        // 初始化内部状态
        last_req[0] = last_req[1] = false;
        current_level_rx[0] = current_level_rx[1] = false;
    }
    
    // 析构函数，释放 DUT 内存
    ~Testbench() {
        delete dut;
    }

private:
    bool last_req[2]; // 用于 downstream_mock_process 的状态
    // 辅助函数，将 FlitType 转换为字符串
    const char* flit_type_to_str(FlitType ft) {
        switch (ft) {
            case FLIT_TYPE_HEAD: return "HEAD";
            case FLIT_TYPE_BODY: return "BODY";
            case FLIT_TYPE_TAIL: return "TAIL";
            default: return "UNKNOWN";
        }
    }
};