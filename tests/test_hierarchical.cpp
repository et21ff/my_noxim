/*
 * test_hierarchical.cpp - Hierarchical NoC Connection Test
 *
 * 这个测试程序用于验证层次化路由器的连接和功能。
 * 它创建一个简单的层次化网络拓扑，并通过MockPE模块进行测试。
 */

#include "NoC.h"
#include "GlobalParams.h"
#include "Tile.h"
#include "MockPE.h"
#include <iostream>
#include <iomanip>
#include "ConfigurationManager.h"

using namespace std;

unsigned int drained_volume = 0; 

// 全局变量用于测试监控
int test_cycles = 0;
const int MAX_TEST_CYCLES = 1000;

// ================================================================
// 测试监视器：监控整个层次化网络的运行状态
// ================================================================
SC_MODULE(TestMonitor) {

    // 时钟和复位信号
    sc_in<bool> clock;
    sc_in<bool> reset;

    // 对NoC实例的引用
    NoC* noc;

    void monitorProcess() {
        while (true) {
            wait();

            test_cycles++;

            // 每隔100个周期打印一次状态
            if (test_cycles % 100 == 0) {
                cout << "\n===== 测试周期 " << test_cycles << " =====" << endl;

                // 打印所有MockPE的状态
                for (int i = 0; i < GlobalParams::num_nodes; i++) {
                    if (noc->t[i]->pe != nullptr) {
                        MockPE* pe = static_cast<MockPE*>(noc->t[i]->pe);
                        pe->printStatus();
                    }
                }
            }

            // 检查测试完成条件
            if (test_cycles >= MAX_TEST_CYCLES) {
                cout << "\n===== 测试完成 =====" << endl;
                printFinalStatistics();
                sc_stop();  // 停止仿真
            }
        }
    }

    void printFinalStatistics() {
        cout << "\n===== 最终测试统计 =====" << endl;

        int total_sent = 0;
        int total_received = 0;

        for (int i = 0; i < GlobalParams::num_nodes; i++) {
            if (noc->t[i]->pe != nullptr) {
                MockPE* pe = static_cast<MockPE*>(noc->t[i]->pe);
                total_sent += pe->getPacketsSent();
                total_received += pe->getPacketsReceived();
            }
        }

        cout << "总发送包数: " << total_sent << endl;
        cout << "总接收包数: " << total_received << endl;
        cout << "测试周期数: " << test_cycles << endl;

        // 验证层次化连接的统计信息
        cout << "\n===== 层次化连接验证 =====" << endl;
        cout << "网络节点总数: " << GlobalParams::num_nodes << endl;
        cout << "网络层数: " << GlobalParams::num_levels << endl;

        for (int level = 0; level < GlobalParams::num_levels; level++) {
            cout << "第 " << level << " 层扇出数: " << GlobalParams::fanouts_per_level[level] << endl;
        }
    }

    SC_CTOR(TestMonitor) {
        SC_THREAD(monitorProcess);
        sensitive << clock.pos();

        cout << "[TestMonitor] 测试监视器已创建" << endl;
    }
};

// ================================================================
// 主测试函数：设置和运行层次化网络测试
// ================================================================
int sc_main(int argc, char* argv[]) {

    cout << "\n====================================================" << endl;
    cout << "    层次化NoC连接测试开始" << endl;
    cout << "====================================================" << endl;

    cout << "--- Manually setting GlobalParams and loading configs for testbench ---" << endl;
    const char* mocked_argv[] = {
        "test_hierarchical",          // argv[0]: 程序名
        "-config",                    // argv[1]: 选项
        "../config_examples/hirearchy.yaml", // argv[2]: 选项的值
        // 你可以在这里添加更多的默认参数
        // "-power",
        // "power.yaml" 
    };

    int mocked_argc = sizeof(mocked_argv) / sizeof(char*);

    configure(mocked_argc, const_cast<char**>(mocked_argv));  // 加载配置文件

    


    // 设置时钟周期
    sc_clock clock("clock", GlobalParams::clock_period_ps, SC_PS);

    // 设置复位信号
    sc_signal<bool> reset;

    // 初始化全局参数为层次化拓扑
    GlobalParams::topology = TOPOLOGY_HIERARCHICAL;
    GlobalParams::num_levels = 3;
    GlobalParams::fanouts_per_level = new int[3];
    GlobalParams::fanouts_per_level[0] = 4;  // 根节点有4个子节点
    GlobalParams::fanouts_per_level[1] = 4;  // 中间节点有4个子节点
    GlobalParams::fanouts_per_level[2] = 0;  // 叶节点没有子节点
    // Set buffer depth
    GlobalParams::buffer_depth = 8;

    // Set flit size
    GlobalParams::flit_size = 32;

    // Set link lengths
    GlobalParams::r2h_link_length = 2.0;
    GlobalParams::r2r_link_length = 1.0;

    // Set virtual channels
    GlobalParams::n_virtual_channels = 1;

    // Set routing algorithm
    GlobalParams::routing_algorithm = "XY";
    GlobalParams::routing_table_filename = "";
    GlobalParams::dyad_threshold = 0.6;

    // Set selection strategy
    GlobalParams::selection_strategy = "RANDOM";

    // Disable wireless features
    GlobalParams::use_winoc = false;
    GlobalParams::use_powermanager = false;

    // Set simulation parameters
    GlobalParams::clock_period_ps = 1000;
    GlobalParams::reset_time = 10;
    GlobalParams::simulation_time = 10000;
    GlobalParams::stats_warm_up_time = 0;
    GlobalParams::detailed = false;
    GlobalParams::max_volume_to_be_drained = 0;
    GlobalParams::show_buffer_stats = false;

    // Set verbosity
    GlobalParams::verbose_mode = VERBOSE_OFF;

    // Set trace mode
    GlobalParams::trace_mode = false;
    GlobalParams::trace_filename = "debug_trace.log";

    // Disable NoXim's built-in traffic injection
    GlobalParams::min_packet_size = 8;
    GlobalParams::max_packet_size = 8;
    GlobalParams::packet_injection_rate = 0.0;
    GlobalParams::probability_of_retransmission = 0.0;

    // Set traffic distribution
    GlobalParams::traffic_distribution = TRAFFIC_TABLE_BASED;
    GlobalParams::traffic_table_filename = "empty_traffic.txt";

    // 计算总节点数
    // GlobalParams::num_nodes = 3;  // 根节点(1) + 中间节点(4) + 叶节点(16)

    cout << "[配置] 层次化拓扑参数:" << endl;
    cout << "  - 网络层数: " << GlobalParams::num_levels << endl;
    cout << "  - 总节点数: " << GlobalParams::num_nodes << endl;
    cout << "  - 层0扇出: " << GlobalParams::fanouts_per_level[0] << endl;
    cout << "  - 层1扇出: " << GlobalParams::fanouts_per_level[1] << endl;

    // 创建NoC实例
    NoC noc("NoC");
    noc.clock(clock);
    noc.reset(reset);

    // sc_trace_file *tf = sc_create_vcd_trace_file("waveform");
    //  tf->set_time_unit(1, SC_NS);
    //  sc_trace(tf, clock, "clock"); 
     
    //  sc_trace(tf,*(noc.t[0]->r->h_ack_rx_local[0]), "node0_flit_rx");
    //  sc_trace(tf,noc.t[0]->pe->ack_tx, "node0_req_rx");

    //创建测试监视器
    TestMonitor monitor("TestMonitor");
    monitor.clock(clock);
    monitor.reset(reset);
    monitor.noc = &noc;

    // 初始化层次化连接
    cout << "\n[初始化] 构建层次化网络连接..." << endl;
    

    cout << "[初始化] 层次化网络构建完成" << endl;

    // 准备测试数据包
    cout << "\n[测试准备] 向MockPE注入测试包..." << endl;



    // 开始仿真
    cout << "\n[仿真开始] 启动层次化网络测试..." << endl;

    // 复位序列
    reset.write(true);
    cout << "[周期 0] 复位激活" << endl;

    // 运行几个复位周期
    sc_start(5, SC_NS);

    // 释放复位
    reset.write(false);
    cout << "[周期 5] 复位释放，开始正常仿真" << endl;

    // MockPE* pe = static_cast<MockPE*>(noc.t[1]->pe);
    // pe->injectTestPacket(0, 5);  // 发送5个flit的包（HEAD + 3 BODY + TAIL）


    // 为叶节点注入测试包（节点5-20是叶节点）
    for (int i = 5; i < min(21, GlobalParams::num_nodes); i++) {
        if (noc.t[i]->pe != nullptr) {
            MockPE* pe = static_cast<MockPE*>(noc.t[i]->pe);

            // 注入不同目标的测试包
            int target_id = GlobalParams::parent_map[i];  // 目标为中间节点1-4
            pe->injectTestPacket(target_id, 3);  // 发送3个flit的包

            cout << "  - 节点 " << i << " -> 节点 " << target_id << " (3 flits)" << endl;
        }
    }

    // 为中间节点注入测试包（节点1-4是中间节点）
    for (int i = 1; i < 5; i++) {
        if (noc.t[i]->pe != nullptr) {
            MockPE* pe = static_cast<MockPE*>(noc.t[i]->pe);

            // 中间节点向根节点发送测试包
            pe->injectTestPacket(0, 3);  // 发送2个flit的包到根节点

            cout << "  - 中间节点 " << i << " -> 根节点 0 (2 flits)" << endl;
        }
    }

    // 运行仿真
    sc_start(300,SC_NS);
    // sc_close_vcd_trace_file(tf);

    cout << "\n====================================================" << endl;
    cout << "    层次化NoC连接测试完成" << endl;
    cout << "====================================================" << endl;

    return 0;
}