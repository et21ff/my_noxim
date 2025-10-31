#include "MockPE.h"
#include <cassert>

// ================================================================
// 构造函数：初始化MockPE模块的所有组件和状态
// ================================================================
MockPE::MockPE(sc_module_name nm) : sc_module(nm){
    // 注意：这个构造函数缺少local_id初始化，应该使用带id参数的构造函数
    local_id = -1;  // 临时设置为无效值，应该避免使用这个构造函数

    cout << "[MockPE_" << local_id << "] 警告：使用了缺少id参数的构造函数！" << endl;

    // 初始化核心状态变量
    current_level_tx = false;              // 发送电平初始化为低电平
    current_level_rx = false;              // 接收电平初始化为低电平
    flit_left_in_packet = 0;               // 当前包剩余flit数量初始化为0
    total_packets_sent = 0;                // 总发送包计数器清零
    total_packets_received = 0;            // 总接收包计数器清零
    total_flits_sent = 0;                  // 总发送flit计数器清零
    total_flits_received = 0;              // 总接收flit计数器清零

    // 初始化current_packet为安全状态
    current_packet.dst_id = -1;
    current_packet.size = 0;
    current_packet.src_id = -1;
    current_packet.vc_id = 0;

    receiving_in_progress = false;         // 接收状态标志初始化
    fixed_destination = -1;                // 固定目标初始化为无效值

    // 初始化SystemC进程
    SC_METHOD(txProcess);                  // 注册发送进程方法
    sensitive << reset.pos();              // 对复位信号上升沿敏感
    sensitive << clock.pos();              // 对时钟上升沿敏感（发送逻辑）

    SC_METHOD(rxProcess);                  // 注册接收进程方法
    sensitive << reset.pos();              // 对复位信号上升沿敏感
    sensitive << clock.neg();              // 对时钟下降沿敏感（接收逻辑）

    cout << "[MockPE_" << local_id << "] 构造函数完成，时钟和复位信号已连接" << endl;
}

// ================================================================
// 带ID参数的构造函数：推荐的构造函数
// ================================================================
MockPE::MockPE(sc_module_name nm, int id) : sc_module(nm), local_id(id) {

    cout << "[MockPE_" << local_id << "] 构造函数启动..." << endl;

    // 初始化核心状态变量
    current_level_tx = false;              // 发送电平初始化为低电平
    current_level_rx = false;              // 接收电平初始化为低电平
    flit_left_in_packet = 0;               // 当前包剩余flit数量初始化为0
    total_packets_sent = 0;                // 总发送包计数器清零
    total_packets_received = 0;            // 总接收包计数器清零
    total_flits_sent = 0;                  // 总发送flit计数器清零
    total_flits_received = 0;              // 总接收flit计数器清零

    // 初始化current_packet为安全状态
    current_packet.dst_id = -1;
    current_packet.size = 0;
    current_packet.src_id = -1;
    current_packet.vc_id = 0;

    receiving_in_progress = false;         // 接收状态标志初始化
    fixed_destination = -1;                // 固定目标初始化为无效值

    // 初始化SystemC进程
    SC_METHOD(txProcess);                  // 注册发送进程方法
    sensitive << reset.pos();              // 对复位信号上升沿敏感
    sensitive << clock.pos();              // 对时钟上升沿敏感（发送逻辑）

    SC_METHOD(rxProcess);                  // 注册接收进程方法
    sensitive << reset.pos();              // 对复位信号上升沿敏感
    sensitive << clock.neg();              // 对时钟下降沿敏感（接收逻辑）

    cout << "[MockPE_" << local_id << "] 构造函数完成，时钟和复位信号已连接" << endl;
}

// ================================================================
// 发送进程：负责从packet_queue中取出数据包并发送flits
// 这是模块的核心驱动逻辑，完全由packet_queue队列控制
// ================================================================
void MockPE::txProcess()
{
    // --- 复位处理 ---
    if (reset.read()) {
        // 复位时清空所有状态和信号
        while (!packet_queue.empty()) {
            packet_queue.pop();
        }
        flit_left_in_packet = 0;           // 清空当前包计数器
        current_level_tx = false;          // 重置发送电平
        req_tx.write(false);               // 清除发送请求
        total_packets_sent = 0;            // 清零发送统计
        total_flits_sent = 0;              // 清零flit统计
        // cout << "[MockPE_" << local_id << "] txProcess: 复位完成，所有状态已清零" << endl;
        return;
    }

    // --- 核心ABP流控逻辑 ---
    // 如果收到的确认信号电平，正是我当前期望的电平，说明对方已准备好接收
    if (ack_tx.read() == current_level_tx ) {

        // 只有在可以发送，并且确实有东西要发时，才执行操作
        if (!packet_queue.empty()) {

            // 检查是否需要开始发送新包
            if (flit_left_in_packet == 0) {
                // 从队列头部取出下一个要发送的测试包
                current_packet = packet_queue.front();
                flit_left_in_packet = current_packet.size;
                cout << "[MockPE_" << local_id << "] txProcess: 开始发送新包 "
                     << "[Dst:" << current_packet.dst_id
                     << ", Size:" << current_packet.size << " flits]" << endl;
            }

            // 安全检查：确保current_packet有效
            if (flit_left_in_packet <= 0) {
                cout << "[MockPE_" << local_id << "] txProcess: 错误：flit_left_in_packet <= 0 但仍在发送！" << endl;
                return;
            }

            if (buffer_full_status_tx.read().mask[current_packet.vc_id]) {
                // 如果目标VC满了，等待下次机会
                cout << "[MockPE_" << local_id << "] txProcess: 目标VC " << current_packet.vc_id << " 满，等待下次发送机会" << endl;
                return;
            }

            // 调用nextFlit()生成下一个flit
            Flit flit = nextFlit();

            // 将flit放到发送端口上
            flit_tx.write(flit);

            // **核心ABP协议**: 翻转我期望的ack电平，并发起新请求
            current_level_tx = 1 - current_level_tx;
            req_tx.write(current_level_tx);

            // 更新统计信息
            total_flits_sent++;
            if(flit.is_multicast==false)
                cout << "[MockPE_" << local_id << "] txProcess: 发送" << getFlitTypeString(flit.flit_type)
                 << " flit -> Dst:" << flit.dst_id
                 << ", 电平:" << (current_level_tx ? "HIGH" : "LOW");\
            else
                cout << "[MockPE_" << local_id << "] txProcess: 发送" << getFlitTypeString(flit.flit_type)
                 << " flit -> Dsts:";
                for(size_t i=0;i<flit.multicast_dst_ids.size();i++){
                    cout<<flit.multicast_dst_ids[i];
                    if(i!=flit.multicast_dst_ids.size()-1) cout<<",";
                }
                cout<<", 电平:" << (current_level_tx ? "HIGH" : "LOW");

            if (flit.flit_type == FLIT_TYPE_HEAD) {
                total_packets_sent++;
                cout << ", 总包数:" << total_packets_sent;
            }

            cout << ", 剩余:" << flit_left_in_packet << endl;

            // 检查包是否发送完成
            if (flit_left_in_packet == 0) {
                cout << "[MockPE_" << local_id << "] txProcess: 包发送完成! "
                     << "[Dst:" << current_packet.dst_id
                     << ", Size:" << current_packet.size << " flits]" << endl;
                packet_queue.pop(); // 从队列中移除已发送的包
            }
        }
    }
    else if(flit_left_in_packet > 0) {
        // 如果有包在发送但对方还没准备好，等待下次发送机会
        // 正常的流控状态，无需打印调试信息
    }
}

// ================================================================
// 接收进程：负责处理从路由器接收到的flits
// 监听输入端口，按照ABP协议正确接收和组装数据包
// ================================================================
void MockPE::rxProcess() {

    // --- 复位处理 ---
    if (reset.read() == true) {
        // 复位时清空所有接收状态
        current_level_rx = false;          // 重置接收电平
        ack_rx.write(false);               // 清除应答信号
        received_packet.clear();           // 清空当前接收包缓存
        total_packets_received = 0;        // 清零接收统计
        receiving_in_progress = false;     // 重置接收状态标志

        // cout << "[MockPE_" << local_id << "] rxProcess: 复位完成，接收状态已清零" << endl;
        return;
    }

    // --- 正常接收逻辑 ---

    // 检查发送方是否有新的请求（ABP协议：必须匹配电平）
    bool req_level = req_rx.read();
    bool expected_req_level = 1-current_level_rx; // 期望的请求电平

    
    if (req_rx.read() == 1-current_level_rx) {
        // 电平匹配，可以接收这个flit
        cout << "[MockPE_" << local_id << "] rxProcess: 电平匹配 ("
             << (req_level ? "HIGH" : "LOW") << ")，准备接收flit" << endl;

        Flit received_flit = flit_rx.read();   // 从输入端口读取flit

        // 接收成功，发送应答信号（翻转电平）
        bool new_ack_level = !current_level_rx;
        ack_rx.write(new_ack_level);
        current_level_rx = new_ack_level;   // 更新接收电平

        // 更新统计信息
        total_flits_received++;                 // 全局flit接收计数器递增

        cout << "[MockPE_" << local_id << "] rxProcess: 接收flit <- "
             << "Src:" << received_flit.src_id
             << ", Type:" << getFlitTypeString(received_flit.flit_type)
             << ", 发送ACK电平:" << (new_ack_level ? "HIGH" : "LOW") << endl;

            // --- 根据flit类型进行不同的处理 ---

            if (received_flit.flit_type == FLIT_TYPE_HEAD) {
                // HEAD flit：新包的开始
                received_packet.clear();                    // 清空之前的包缓存
                received_packet.push_back(received_flit);   // 将HEAD flit加入缓存
                receiving_in_progress = true;               // 标记正在接收包

                cout << "[MockPE_" << local_id << "] rxProcess: 开始接收新包 <- "
                     << "Src:" << received_flit.src_id << endl;
            }
            else if (receiving_in_progress) {
                // BODY或TAIL flit：正在接收的包的后续部分
                received_packet.push_back(received_flit);
                total_flits_received++;   // 将flit加入缓存

                if (received_flit.flit_type == FLIT_TYPE_TAIL) {
                    // TAIL flit：包的结束
                    total_packets_received++;               // 包接收计数器递增
                    receiving_in_progress = false;          // 标记包接收完成

                    // 打印完整的包接收信息
                    cout << "[MockPE_" << local_id << "] rxProcess: 包接收完成! <- "
                         << "Src:" << received_flit.src_id
                         << ", Size:" << received_packet.size() << " flits"
                         << ", 总包数:" << total_packets_received << endl;
                }
            }
            else {
                // 收到BODY/TAIL但没有HEAD，这可能是协议错误
                cout << "[MockPE_" << local_id << "] rxProcess: 警告：收到非HEAD flit但不在接收状态!" << endl;
            }
        }
    else {
        ack_rx.write(current_level_rx);  // 保持当前电平作为应答
    }
}

// ================================================================
// 公共接口方法：向MockPE注入测试包
// 这个方法被测试程序调用，用于向packet_queue添加测试数据
// ================================================================
void MockPE::injectTestPacket(int dst_id, int size, int src_id) {

    // 输入参数验证
    if (dst_id < 0) {
        cerr << "[MockPE_" << local_id << "] 错误：无效的目标ID " << dst_id << endl;
        return;
    }

    if (size <= 0) {
        cerr << "[MockPE_" << local_id << "] 错误：包大小必须大于0 " << size << endl;
        return;
    }

    // 创建测试包结构
    TestPacket test_packet;
    test_packet.dst_id = dst_id;     // 设置目标ID
    test_packet.size = size;         // 设置包大小（flit数量）
    test_packet.src_id = src_id;     // 设置源ID（用于日志）
    test_packet.vc_id = std::rand() % GlobalParams::n_virtual_channels;

    // 将测试包加入发送队列
    packet_queue.push(test_packet);

    cout << "[MockPE_" << local_id << "] 注入测试包 -> Dst:" << dst_id
         << ", Size:" << size << " flits, 队列长度:" << packet_queue.size() << endl;
}

void MockPE::injectTestPacket(vector<int> dst_ids, int size,int src_id) {
    if(size <= 0) {
        cerr << "[MockPE_" << local_id << "] 错误：包大小必须大于0 " << size << endl;
        return;
    }

    for(auto dis_id : dst_ids) {
        if(dis_id < 0) {
            cerr << "[MockPE_" << local_id << "] 错误：无效的目标ID " << dis_id << endl;
            return;
        }
        
    }

    TestPacket test_packet;
    test_packet.size = size;         // 设置包大小（flit数量）
    test_packet.src_id = src_id;     // 设置源ID（用于日志）
    test_packet.vc_id = std::rand() % GlobalParams::n_virtual_channels;
    test_packet.is_multicast = true;
    test_packet.multicast_dst_ids = dst_ids;
    packet_queue.push(test_packet);
    cout << "[MockPE_" << local_id << "] 注入多播测试包 -> Dsts: ";
    for(auto dis_id : dst_ids) {
        cout << dis_id << " ";
    }   
}
// ================================================================
// 公共接口方法：设置固定的测试目标
// 简化测试场景，所有包都发送到同一个目标
// ================================================================
void MockPE::setFixedDestination(int dst_id) {
    fixed_destination = dst_id;
    cout << "[MockPE_" << local_id << "] 设置固定目标: " << dst_id << endl;
}

// ================================================================
// 公共接口方法：生成并发送一个固定大小的测试包
// 便捷方法，用于快速测试
// ================================================================
void MockPE::sendFixedPacket(int dst_id, int size) {

    // 设置目标ID（如果使用固定目标模式）
    if (fixed_destination >= 0) {
        dst_id = fixed_destination;
    }

    // 注入测试包到队列
    injectTestPacket(dst_id, size, local_id);
}

// ================================================================
// 公共接口方法：获取发送统计信息
// 用于验证测试结果和性能分析
// ================================================================
int MockPE::getPacketsSent() const {
    return total_packets_sent;
}

int MockPE::getFlitsSent() const {
    return total_flits_sent;
}

int MockPE::getPacketsReceived() const {
    return total_packets_received;
}

int MockPE::getFlitsReceived() const {
    return total_flits_received;
}

unsigned int MockPE::getQueueSize() const
{
    return packet_queue.size();
}

// ================================================================
// 公共接口方法：打印详细的状态信息
// 用于调试和监控MockPE的运行状态
// ================================================================
void MockPE::printStatus() const {
    cout << "\n=== MockPE_" << local_id << " 状态报告 ===" << endl;
    cout << "基础信息:" << endl;
    cout << "  - 节点ID: " << local_id << endl;
    cout << "  - 固定目标: " << (fixed_destination >= 0 ? to_string(fixed_destination) : "无") << endl;
    cout << "  - 发送电平: " << (current_level_tx ? "HIGH" : "LOW") << endl;
    cout << "  - 接收电平: " << (current_level_rx ? "HIGH" : "LOW") << endl;

    cout << "\n发送统计:" << endl;
    cout << "  - 总发送包数: " << total_packets_sent << endl;
    cout << "  - 总发送flit数: " << total_flits_sent << endl;
    cout << "  - 当前包剩余flits: " << flit_left_in_packet << endl;
    cout << "  - 发送队列长度: " << packet_queue.size() << endl;

    cout << "\n接收统计:" << endl;
    cout << "  - 总接收包数: " << total_packets_received << endl;
    cout << "  - 总接收flit数: " << total_flits_received << endl;
    cout << "  - 接收进行中: " << (receiving_in_progress ? "是" : "否") << endl;
    cout << "  - 当前缓存flits大小: " << received_packet.size() << endl;

    cout << "\n端口状态:" << endl;
    cout << "  - req_tx: " << (req_tx.read() ? "HIGH" : "LOW") << endl;
    cout << "  - ack_tx: " << (ack_tx.read() ? "HIGH" : "LOW") << endl;
    cout << "  - req_rx: " << (req_rx.read() ? "HIGH" : "LOW") << endl;
    cout << "  - ack_rx: " << (ack_rx.read() ? "HIGH" : "LOW") << endl;
    cout << "================================\n" << endl;
}

// ================================================================
// 辅助函数：生成下一个要发送的flit
// 参考ProcessingElement::nextFlit的实现，适配MockPE的数据结构
// ================================================================
Flit MockPE::nextFlit()
{
    Flit flit;
    TestPacket packet = current_packet;

    flit.src_id = local_id;
    flit.vc_id = packet.vc_id;
    flit.logical_timestamp = sc_time_stamp().to_double();
    flit.sequence_no = packet.size - flit_left_in_packet + 1;
    flit.sequence_length = packet.size;
    flit.hop_no = 0;
    flit.payload_data_size = packet.size;
    flit.is_output = false;
    flit.hub_relay_node = NOT_VALID;
    if(packet.is_multicast) {
        flit.is_multicast = true;
        flit.multicast_dst_ids = packet.multicast_dst_ids;

        if(flit_left_in_packet == packet.size) {
            flit.flit_type = FLIT_TYPE_HEAD;
            cout << "[MockPE_" << local_id << "] nextFlit: 创建MULTICAST HEAD flit for 包 -> Dsts: ";
            for(auto dis_id : packet.multicast_dst_ids) {
                cout << dis_id << " ";
            }
            cout << endl;
        } else if (flit_left_in_packet == 1) {
            flit.flit_type = FLIT_TYPE_TAIL;
            cout << "[MockPE_" << local_id << "] nextFlit: 创建MULTICAST TAIL flit for 包 -> Dsts: ";
            for(auto dis_id : packet.multicast_dst_ids) {
                cout << dis_id << " ";
            }
            cout << endl;
        } else {
            flit.flit_type = FLIT_TYPE_BODY;
            cout << "[MockPE_" << local_id << "] nextFlit: 创建MULTICAST BODY flit for 包 -> Dsts: ";
            for(auto dis_id : packet.multicast_dst_ids) {
                cout << dis_id << " ";
            }
            cout << endl;
        }
    } 
    else {
        flit.is_multicast = false;
        flit.dst_id = packet.dst_id;
        flit.multicast_dst_ids.clear();

            // 根据剩余flit数量确定类型
        if (flit_left_in_packet == packet.size) {
            flit.flit_type = FLIT_TYPE_HEAD;
            cout << "[MockPE_" << local_id << "] nextFlit: 创建HEAD flit for 包 -> Dst:" << packet.dst_id << endl;
        } else if (flit_left_in_packet == 1) {
            flit.flit_type = FLIT_TYPE_TAIL;
            cout << "[MockPE_" << local_id << "] nextFlit: 创建TAIL flit for 包 -> Dst:" << packet.dst_id << endl;
        } else {
            flit.flit_type = FLIT_TYPE_BODY;
            cout << "[MockPE_" << local_id << "] nextFlit: 创建BODY flit for 包 -> Dst:" << packet.dst_id << endl;
        }

    }


    // 减少剩余flit计数
    flit_left_in_packet--;

    return flit;
}

// ================================================================
// 辅助函数：将flit类型转换为可读字符串
// 用于调试和日志输出
// ================================================================
std::string MockPE::getFlitTypeString(FlitType type) {
    switch (type) {
        case FLIT_TYPE_HEAD:  return "HEAD";
        case FLIT_TYPE_BODY:  return "BODY";
        case FLIT_TYPE_TAIL:  return "TAIL";
        default:              return "UNKNOWN";
    }
}