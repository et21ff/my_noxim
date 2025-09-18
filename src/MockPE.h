/*
 * MockPE.h - Mock Processing Element for Hierarchical NoC Testing
 *
 * 这是一个队列驱动的测试模块，专门用于验证层次化路由器的连接和功能。
 * 该模块模拟一个简化的处理单元，通过预定义的数据包队列来产生网络流量。
 *
 * 主要特性：
 * - 队列驱动：完全由 packet_queue 控制发送行为
 * - 简化协议：只使用标准的Noxim flit握手，不包含业务逻辑握手
 * - 可测试性：提供丰富的统计信息和状态监控方法
 * - 中文注释：详细的中文注释便于理解和调试
 */

#ifndef __MOCK_PE_H__
#define __MOCK_PE_H__

#include <systemc.h>
#include <queue>
#include <iostream>
#include <string>
#include "GlobalTrafficTable.h"

// 引入Noxim的基本数据类型定义
#include "DataStructs.h"
#include "Utils.h"

using namespace std;

// ================================================================
// 测试包结构：用于定义要发送的测试数据包
// ================================================================
struct TestPacket {
    int dst_id;     // 目标节点ID
    int size;       // 包的大小（flit数量）
    int src_id;     // 源节点ID（用于日志记录）
    int vc_id;      // 虚拟通道ID
};

// ================================================================
// MockPE模块：队列驱动的测试处理单元
//
// 这个模块模拟一个简化的处理单元，主要用于测试层次化网络连接。
//  * 核心设计理念：
//  * 1. 队列驱动：所有发送行为由 packet_queue 队列控制
//  * 2. 简化握手：只使用标准的Noxim flit握手协议
//  * 3. 确定性：行为完全可预测，便于调试和验证
//  * 4. 可观测：提供详细的日志和统计信息
// ================================================================
SC_MODULE(MockPE) {


    // ===== I/O端口定义 (全部带有明确的名称) =====

    // 基本系统信号
    sc_in_clk   clock{"clock"};      // 时钟信号
    sc_in<bool> reset{"reset"};      // 复位信号

    // 标准Noxim flit通信端口（发送方向）
    sc_out<Flit>  flit_tx{"flit_tx"};  // 发送flit输出端口
    sc_out<bool>  req_tx{"req_tx"};   // 发送请求输出端口
    sc_in<bool>   ack_tx{"ack_tx"};   // 发送应答输入端口
    sc_in<TBufferFullStatus>   buffer_full_status_tx{"buffer_full_status_tx"}; // 发送缓冲区满状态输入端口

    // 标准Noxim flit通信端口（接收方向）
    sc_in<Flit>   flit_rx{"flit_rx"};  // 接收flit输入端口
    sc_in<bool>   req_rx;   // 接收请求输入端口
    sc_out<bool>  ack_rx{"ack_rx"};   // 接收应答输出端口
    sc_out<TBufferFullStatus>  buffer_full_status_rx{"buffer_full_status_rx"}; // 接收缓冲区满状态输出端口

    // ===== 核心状态变量 =====

    int local_id;                           // 当前MockPE的唯一标识符
    bool current_level_tx;                  // 发送方向的当前电平（ABP协议）
    bool current_level_rx;                  // 接收方向的当前电平（ABP协议）
    int flit_left_in_packet;               // 当前正在发送的包剩余的flit数量
    GlobalTrafficTable *traffic_table;
    bool never_transmit;

    // vector<sc_in<int>*> downstream_ready_in; // 从下游PE接收的"ready"信号
    // sc_out<int> downstream_ready_out; // 向上游PE发送的"ready"信号




    // ===== 测试数据队列 =====

    queue<TestPacket> packet_queue;        // 待发送的测试包队列
    TestPacket current_packet;              // 当前正在发送的包

    // ===== 接收状态管理 =====

    vector<Flit> received_packet;          // 当前正在接收的包缓存
    bool receiving_in_progress;            // 是否正在接收包的标志

    // ===== 统计信息 =====

    int total_packets_sent;                // 总发送包数量统计
    int total_packets_received;            // 总接收包数量统计
    int total_flits_sent;                  // 总发送flit数量统计
    int total_flits_received;              // 总接收flit数量统计

    // ===== 测试配置 =====

    int fixed_destination;                 // 固定目标ID（-1表示不使用固定目标）

    // ===== SystemC进程方法 =====

    void txProcess();                       // 发送进程：处理数据包发送
    void rxProcess();                       // 接收进程：处理数据包接收

    // ===== 公共接口方法 =====

    // 测试包注入方法
    void injectTestPacket(int dst_id, int size, int src_id = -1);
    void setFixedDestination(int dst_id);
    void sendFixedPacket(int dst_id, int size);

    // 状态查询方法
    int getPacketsSent() const;
    int getFlitsSent() const;
    int getPacketsReceived() const;
    int getFlitsReceived() const;
    unsigned int getQueueSize() const;

    // 调试和监控方法
    void printStatus() const;

    // ===== 构造函数 =====

    SC_CTOR(MockPE);
    MockPE(sc_module_name nm, int id);
    virtual ~MockPE() = default;

    // ===== 私有辅助方法 =====

private:
    Flit nextFlit();                           // 生成下一个要发送的flit
    string getFlitTypeString(FlitType type);  // 将flit类型转换为可读字符串
};

#endif // __MOCK_PE_H__