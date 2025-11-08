#include <systemc.h>
#include "../src/Tile.h"
#include "../src/GlobalParams.h"
#include "../src/ConfigurationManager.h"
#include "../src/GlobalRoutingTable.h"
#include "../src/GlobalTrafficTable.h"
#include "../src/DataStructs.h"  // 添加缺失的头文件
#include "../src/NoC.h"
#include "../src/HierarchicalTopologyManager.h"
unsigned int drained_volume;

// Mock 节点组件（修正接口类型）
SC_MODULE(MockNode) {
    sc_in_clk clock;
    sc_in<bool> reset;
    
    // 使用与Tile兼容的信号类型
    sc_in<Flit> flit_rx;
    sc_in<bool> req_rx;
    sc_out<bool> ack_rx;
    sc_out<TBufferFullStatus> buffer_full_status_rx;
    
    sc_out<Flit> flit_tx;
    sc_out<bool> req_tx;
    sc_in<bool> ack_tx;
    sc_in<TBufferFullStatus> buffer_full_status_tx;
    
    string node_name;
    
    void rxProcess() {
        if (reset.read()) {
            ack_rx.write(0);
        } else {
            if (req_rx.read()) {
                Flit received_flit = flit_rx.read();
                cout << "[" << sc_time_stamp() << "] " << node_name 
                     << " received flit: src=" << received_flit.src_id 
                     << " dst=" << received_flit.dst_id << endl;
                ack_rx.write(1);
            } else {
                ack_rx.write(0);
            }
        }
        
        // 报告缓冲区未满
    TBufferFullStatus bfs;
    for (int i = 0; i < GlobalParams::n_virtual_channels; i++) {
        bfs.mask[i] = false;  // 或者直接写 0
    }
    buffer_full_status_rx.write(bfs);
    }
    
    SC_CTOR(MockNode) : node_name("MockNode") {
        SC_METHOD(rxProcess);
        sensitive << reset << clock.pos();
    }
};
 
int sc_main(int argc, char* argv[]) {
    // 1. 初始化全局配置
    configure(argc, argv);
    
    cout << "Configuration loaded" << endl;
    cout << "Topology: " << GlobalParams::topology << endl;
    cout << "Number of levels: " << GlobalParams::num_levels << endl;
    
    // 2. 创建SystemC信号
    sc_clock clock("clock", GlobalParams::clock_period_ps, SC_PS);
    sc_signal<bool> reset;

    cout << "开始构建层次化拓扑..." << endl;
    
    // 创建拓扑管理器实例
    HierarchicalTopologyManager topology_manager;
    
    // 初始化并构建拓扑
    if (!topology_manager.initialize()) {
        cerr << "拓扑管理器初始化失败" << endl;
        return 0;
    }
    
    topology_manager.buildTopology();
    topology_manager.printTopologyInfo();
    
    // 验证拓扑结构
    if (!topology_manager.validateTopology()) {
        cerr << "拓扑验证失败" << endl;
    }
    
    // 3. 创建路由表和流量表
    GlobalRoutingTable grtable;
    GlobalTrafficTable gttable;
    
    if (GlobalParams::traffic_distribution == TRAFFIC_TABLE_BASED) {
        gttable.load(GlobalParams::traffic_table_filename.c_str());
    }
    
    // 4. 创建Tile（GLB节点）
    int node_id = 1;
    char tile_name[20];
    sprintf(tile_name, "Tile[%d]", node_id);
    
    Tile* glb_tile = new Tile(tile_name, node_id, GlobalParams::node_level_map[node_id]);
    
    // 关键：初始化层次化端口
    // glb_tile->initHierarchicalPorts();
    
    // 配置Router
    glb_tile->r->configure(node_id,   
                          GlobalParams::node_level_map[node_id],
                          GlobalParams::stats_warm_up_time,
                          GlobalParams::buffer_depth,
                          grtable);
    
    // 配置ProcessingElement
    glb_tile->pe->local_id = node_id;
    glb_tile->pe->traffic_table = &gttable;
    glb_tile->pe->never_transmit = false;
    
    // 连接时钟和复位
    glb_tile->clock(clock);
    glb_tile->reset(reset);
    
    // 5. 连接Mock节点（修正端口访问方式）
    
    // 连接Distribution节点（DOWN方向）
    MockNode* mock_dist[4];
    sc_signal<Flit> flit_to_dist[4], flit_from_dist[4];
    sc_signal<bool> req_to_dist[4], req_from_dist[4];
    sc_signal<bool> ack_to_dist[4], ack_from_dist[4];
    sc_signal<TBufferFullStatus> bfs_to_dist[4], bfs_from_dist[4];
    
    for (int i = 0; i < 4 && i < glb_tile->hierarchical_flit_down_tx.size(); i++) {
        char name[32];
        sprintf(name, "MockDist_%d", i);
        mock_dist[i] = new MockNode(name);
        mock_dist[i]->node_name = name;
        mock_dist[i]->clock(clock);
        mock_dist[i]->reset(reset);
        
        // GLB -> Distribution (使用解引用的指针)
        glb_tile->hierarchical_flit_down_tx[i]->bind(flit_to_dist[i]);
        glb_tile->hierarchical_req_down_tx[i]->bind(req_to_dist[i]);
        glb_tile->hierarchical_ack_down_tx[i]->bind(ack_from_dist[i]);
        glb_tile->hierarchical_buffer_full_status_down_tx[i]->bind(bfs_from_dist[i]);
        
        mock_dist[i]->flit_rx(flit_to_dist[i]);
        mock_dist[i]->req_rx(req_to_dist[i]);
        mock_dist[i]->ack_rx(ack_from_dist[i]);
        mock_dist[i]->buffer_full_status_rx(bfs_from_dist[i]);
        
        // Distribution -> GLB
        glb_tile->hierarchical_flit_down_rx[i]->bind(flit_from_dist[i]);
        glb_tile->hierarchical_req_down_rx[i]->bind(req_from_dist[i]);
        glb_tile->hierarchical_ack_down_rx[i]->bind(ack_to_dist[i]);
        glb_tile->hierarchical_buffer_full_status_down_rx[i]->bind(bfs_to_dist[i]);
        
        mock_dist[i]->flit_tx(flit_from_dist[i]);
        mock_dist[i]->req_tx(req_from_dist[i]);
        mock_dist[i]->ack_tx(ack_to_dist[i]);
        mock_dist[i]->buffer_full_status_tx(bfs_to_dist[i]);
    }
    
    sc_signal<Flit> flit_from_dram, flit_to_dram;
    sc_signal<bool> req_from_dram, req_to_dram;
    sc_signal<bool> ack_from_dram, ack_to_dram;
    sc_signal<TBufferFullStatus> bfs_from_dram, bfs_to_dram;
    // 连接DRAM节点（UP方向）
    if (glb_tile->hierarchical_flit_up_tx != nullptr) {
        MockNode* mock_dram = new MockNode("MockDRAM");
        mock_dram->clock(clock);
        mock_dram->reset(reset);
        

        
        // DRAM -> GLB
        glb_tile->hierarchical_flit_up_rx->bind(flit_from_dram);
        glb_tile->hierarchical_req_up_rx->bind(req_from_dram);
        glb_tile->hierarchical_ack_up_rx->bind(ack_from_dram);
        glb_tile->hierarchical_buffer_full_status_up_rx->bind(bfs_from_dram);
        
        mock_dram->flit_tx(flit_from_dram);
        mock_dram->req_tx(req_from_dram);
        mock_dram->ack_tx(ack_from_dram);
        mock_dram->buffer_full_status_tx(bfs_from_dram);
        
        // GLB -> DRAM
        glb_tile->hierarchical_flit_up_tx->bind(flit_to_dram);
        glb_tile->hierarchical_req_up_tx->bind(req_to_dram);
        glb_tile->hierarchical_ack_up_tx->bind(ack_from_dram);
        glb_tile->hierarchical_buffer_full_status_up_tx->bind(bfs_to_dram);
        
        mock_dram->flit_rx(flit_to_dram);
        mock_dram->req_rx(req_to_dram);
        mock_dram->ack_rx(ack_from_dram);
        mock_dram->buffer_full_status_rx(bfs_to_dram);
    }
    
    // 6. 运行仿真
    cout << "\n=== Starting simulation ===" << endl;
    
    reset.write(1);
    sc_start(GlobalParams::reset_time, SC_NS);

    auto sender_pe = glb_tile->pe;
    reset.write(0);
    sender_pe->buffer_manager_->OnDataReceived(DataType::WEIGHT,3072);
    sender_pe->buffer_manager_->OnDataReceived(DataType::INPUT,576);
    sender_pe->output_buffer_manager_->OnDataReceived(DataType::OUTPUT,512);
    cout << "Reset completed, starting main simulation..." << endl;

    sc_start(100, SC_NS);

    cout << "\n=== Simulation completed ===" << endl;
    
    return 0;
}