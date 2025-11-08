#include "Tile.h"

void Tile::initHierarchicalPorts() {
    
    //----------------------------------------------------------------
    // 1. Initialize UP Ports (Connection to Parent)
    //----------------------------------------------------------------
    if (local_level > 0) { // Only non-root nodes have UP ports
        std::string name;

        // --- UP TX Path: Data flowing FROM this router TO the parent ---
        name = "h_" + std::to_string(local_id) + "_flit_up_tx";
        hierarchical_flit_up_tx = new sc_out<Flit>(name.c_str());

        name = "h_" + std::to_string(local_id) + "_req_up_tx";
        hierarchical_req_up_tx = new sc_out<bool>(name.c_str());

        name = "h_" + std::to_string(local_id) + "_ack_up_tx";
        hierarchical_ack_up_tx = new sc_in<bool>(name.c_str());

        name = "h_" + std::to_string(local_id) + "_buffer_status_up_tx";
        hierarchical_buffer_full_status_up_tx = new sc_in<TBufferFullStatus>(name.c_str());

        // --- UP RX Path: Data flowing FROM parent TO this router ---
        name = "h_" + std::to_string(local_id) + "_flit_up_rx";
        hierarchical_flit_up_rx = new sc_in<Flit>(name.c_str());

        name = "h_" + std::to_string(local_id) + "_req_up_rx";
        hierarchical_req_up_rx = new sc_in<bool>(name.c_str());

        name = "h_" + std::to_string(local_id) + "_ack_up_rx";
        hierarchical_ack_up_rx = new sc_out<bool>(name.c_str());

        name = "h_" + std::to_string(local_id) + "_buffer_status_up_rx";
        hierarchical_buffer_full_status_up_rx = new sc_out<TBufferFullStatus>(name.c_str());

    } else { // Root node: no parent, so all UP ports are null
        hierarchical_flit_up_tx = nullptr;
        hierarchical_req_up_tx = nullptr;
        hierarchical_ack_up_tx = nullptr;
        hierarchical_buffer_full_status_up_tx = nullptr;
        
        hierarchical_flit_up_rx = nullptr;
        hierarchical_req_up_rx = nullptr;
        hierarchical_ack_up_rx = nullptr;
        hierarchical_buffer_full_status_up_rx = nullptr;
    }

    //----------------------------------------------------------------
    // 2. Initialize DOWN Ports (Connections to Children)
    //----------------------------------------------------------------
    int fanout = 0;
    if (local_level < GlobalParams::num_levels - 1) { // Only non-leaf nodes have DOWN ports
        fanout = GlobalParams::fanouts_per_level[local_level];
    }

    // Pre-allocate vector space for efficiency (optional but good practice)
    hierarchical_flit_down_tx.reserve(fanout);
    hierarchical_req_down_tx.reserve(fanout);
    hierarchical_ack_down_tx.reserve(fanout);
    hierarchical_buffer_full_status_down_tx.reserve(fanout);

    hierarchical_flit_down_rx.reserve(fanout);
    hierarchical_req_down_rx.reserve(fanout);
    hierarchical_ack_down_rx.reserve(fanout);
    hierarchical_buffer_full_status_down_rx.reserve(fanout);

    // this->pe->downstream_ready_in.reserve(fanout); //在这里完成对downstream_ready_in的初始化

    // this->pe->downstream_ready_out = new sc_out<int>((std::to_string(local_id) + " downstream_ready_out").c_str());


    // Allocate DOWN ports for each child connection
    for (int i = 0; i < fanout; i++) {
        std::string name_prefix = "h_" + std::to_string(local_id) + "_down_" + std::to_string(i);
        
        // --- DOWN TX Path: Data flowing FROM this router TO child[i] ---
        hierarchical_flit_down_tx.push_back(new sc_out<Flit>((name_prefix + "_flit_tx").c_str()));
        hierarchical_req_down_tx.push_back(new sc_out<bool>((name_prefix + "_req_tx").c_str()));
        hierarchical_ack_down_tx.push_back(new sc_in<bool>((name_prefix + "_ack_tx").c_str()));
        hierarchical_buffer_full_status_down_tx.push_back(new sc_in<TBufferFullStatus>((name_prefix + "_buffer_status_tx").c_str()));
        
        // --- DOWN RX Path: Data flowing FROM child[i] TO this router ---
        hierarchical_flit_down_rx.push_back(new sc_in<Flit>((name_prefix + "_flit_rx").c_str()));
        hierarchical_req_down_rx.push_back(new sc_in<bool>((name_prefix + "_req_rx").c_str()));
        hierarchical_ack_down_rx.push_back(new sc_out<bool>((name_prefix + "_ack_rx").c_str()));
        hierarchical_buffer_full_status_down_rx.push_back(new sc_out<TBufferFullStatus>((name_prefix + "_buffer_status_rx").c_str()));

        // this->pe->downstream_ready_in.push_back(new sc_in<int>((name_prefix + "_downstream_ready_in").c_str()));


    }
}

void Tile::cleanupHierarchicalPorts() {
    // Clean up UP ports
    if (hierarchical_flit_up_rx) delete hierarchical_flit_up_rx;
    if (hierarchical_req_up_rx) delete hierarchical_req_up_rx;
    if (hierarchical_ack_up_rx) delete hierarchical_ack_up_rx;
    if (hierarchical_buffer_full_status_up_rx) delete hierarchical_buffer_full_status_up_rx;

    if (hierarchical_flit_up_tx) delete hierarchical_flit_up_tx;
    if (hierarchical_req_up_tx) delete hierarchical_req_up_tx;
    if (hierarchical_ack_up_tx) delete hierarchical_ack_up_tx;
    if (hierarchical_buffer_full_status_up_tx) delete hierarchical_buffer_full_status_up_tx;

    // Clean up DOWN ports
    for (auto port : hierarchical_flit_down_tx) delete port;
    for (auto port : hierarchical_req_down_tx) delete port;
    for (auto port : hierarchical_ack_down_tx) delete port;
    for (auto port : hierarchical_buffer_full_status_down_tx) delete port;

    for (auto port : hierarchical_flit_down_rx) delete port;
    for (auto port : hierarchical_req_down_rx) delete port;
    for (auto port : hierarchical_ack_down_rx) delete port;
    for (auto port : hierarchical_buffer_full_status_down_rx) delete port;

    // Clear vectors
    hierarchical_flit_down_tx.clear();
    hierarchical_req_down_tx.clear();
    hierarchical_ack_down_tx.clear();
    hierarchical_buffer_full_status_down_tx.clear();

    hierarchical_flit_down_rx.clear();
    hierarchical_req_down_rx.clear();
    hierarchical_ack_down_rx.clear();
    hierarchical_buffer_full_status_down_rx.clear();
}

void Tile::connectRouterToHierarchicalPorts() {
    // =================================================================
    // 1. 连接UP方向端口（与父节点的连接）
    // =================================================================
    if (local_level > 0 && hierarchical_flit_up_rx != nullptr) {
        // UP RX路径：从父节点接收数据
        r->h_flit_rx_up->bind(*hierarchical_flit_up_rx);
        r->h_req_rx_up->bind(*hierarchical_req_up_rx);
        r->h_ack_rx_up->bind(*hierarchical_ack_up_rx);
        r->h_buffer_full_status_rx_up->bind(*hierarchical_buffer_full_status_up_rx);
        
        // UP TX路径：向父节点发送数据
        r->h_flit_tx_up->bind(*hierarchical_flit_up_tx);
        r->h_req_tx_up->bind(*hierarchical_req_up_tx);
        r->h_ack_tx_up->bind(*hierarchical_ack_up_tx);
        r->h_buffer_full_status_tx_up->bind(*hierarchical_buffer_full_status_up_tx);
    }
    
    // =================================================================
    // 2. 连接DOWN方向端口（与子节点的连接）
    // =================================================================
    int fanout = hierarchical_flit_down_tx.size();
    
    for (int i = 0; i < fanout; i++) {
        // DOWN TX：Router -> 子节点
        r->h_flit_tx_down[i]->bind(*hierarchical_flit_down_tx[i]);
        r->h_req_tx_down[i]->bind(*hierarchical_req_down_tx[i]);
        r->h_ack_tx_down[i]->bind(*hierarchical_ack_down_tx[i]);
        r->h_buffer_full_status_tx_down[i]->bind(*hierarchical_buffer_full_status_down_tx[i]);
        
        // DOWN RX：子节点 -> Router
        r->h_flit_rx_down[i]->bind(*hierarchical_flit_down_rx[i]);
        r->h_req_rx_down[i]->bind(*hierarchical_req_down_rx[i]);
        r->h_ack_rx_down[i]->bind(*hierarchical_ack_down_rx[i]);
        r->h_buffer_full_status_rx_down[i]->bind(*hierarchical_buffer_full_status_down_rx[i]);
    }
}

Tile::Tile(sc_module_name nm, int id, int level): sc_module(nm) {
    local_id = id;
    local_level = level;

    // --- 1. 创建子模块 ---
    char router_name[64];
    sprintf(router_name, "Router_%d", local_id);
    r = new Router(router_name);
    r->local_id = local_id;
    r->local_level = local_level;
    r->initPorts();
    r->buildUnifiedInterface();
    // ... (r->configure) ...
    
    // pe = new MockPE("MockPE",local_id);
    pe = new ProcessingElement("ProcessingElement");
    pe->configure(local_id,GlobalParams::node_level_map[local_id],GlobalParams::hierarchical_config);

    // pe ->local_id = local_id;

    // --- 2. 连接时钟和复位 (您的这部分是正确的) ---
    r->clock(clock);
    r->reset(reset);
	
    pe->clock(clock);
    pe->reset(reset);

    // ====================================================================================
    //  [最终的、正确的修正]
    //  Tile 作为父模块，为每条内部链路都创建专用的 sc_signal 作为导线，
    //  并完成其子模块 PE 和 Router 之间的内部布线。
    // ====================================================================================

    // 内部连接常量：定义PE与Router之间的连接数量
    const int NUM_INTERNAL_CONNECTIONS = 1;
    
    // 分配二维指针数组
    sig_flit_p2r = new sc_signal<Flit>*[NUM_INTERNAL_CONNECTIONS];
    sig_req_p2r = new sc_signal<bool>*[NUM_INTERNAL_CONNECTIONS];
    sig_ack_r2p = new sc_signal<bool>*[NUM_INTERNAL_CONNECTIONS];
    sig_stat_r2p = new sc_signal<TBufferFullStatus>*[NUM_INTERNAL_CONNECTIONS];
    
    sig_flit_r2p = new sc_signal<Flit>*[NUM_INTERNAL_CONNECTIONS];
    sig_req_r2p = new sc_signal<bool>*[NUM_INTERNAL_CONNECTIONS];
    sig_ack_p2r = new sc_signal<bool>*[NUM_INTERNAL_CONNECTIONS];
    sig_stat_p2r = new sc_signal<TBufferFullStatus>*[NUM_INTERNAL_CONNECTIONS];

    // 通过循环创建信号并进行绑定
    for (int i = 0; i < NUM_INTERNAL_CONNECTIONS; i++) {
        // --- 链路 1: 数据从 PE 发往 Router (P2R) ---
        char name_buffer[64];
        sprintf(name_buffer, "sig_flit_p2r_%d", i);
        sig_flit_p2r[i] = new sc_signal<Flit>(name_buffer);
        
        sprintf(name_buffer, "sig_req_p2r_%d", i);
        sig_req_p2r[i] = new sc_signal<bool>(name_buffer);
        
        sprintf(name_buffer, "sig_ack_r2p_%d", i);
        sig_ack_r2p[i] = new sc_signal<bool>(name_buffer);
        
        sprintf(name_buffer, "sig_stat_r2p_%d", i);
        sig_stat_r2p[i] = new sc_signal<TBufferFullStatus>(name_buffer);

        // --- 链路 2: 数据从 Router 发往 PE (R2P) ---
        sprintf(name_buffer, "sig_flit_r2p_%d", i);
        sig_flit_r2p[i] = new sc_signal<Flit>(name_buffer);
        
        sprintf(name_buffer, "sig_req_r2p_%d", i);
        sig_req_r2p[i] = new sc_signal<bool>(name_buffer);
        
        sprintf(name_buffer, "sig_ack_p2r_%d", i);
        sig_ack_p2r[i] = new sc_signal<bool>(name_buffer);
        
        sprintf(name_buffer, "sig_stat_p2r_%d", i);
        sig_stat_p2r[i] = new sc_signal<TBufferFullStatus>(name_buffer);

        // 连接 PE(out) --> Router(in)
        pe->flit_tx[i].bind(*sig_flit_p2r[i]);
        pe->req_tx[i].bind(*sig_req_p2r[i]);
        r->h_flit_rx_local[i]->bind(*sig_flit_p2r[i]);
        r->h_req_rx_local[i]->bind(*sig_req_p2r[i]);

        // 连接 Router(out) --> PE(in) (反向)
        r->h_ack_rx_local[i]->bind(*sig_ack_r2p[i]);
        r->h_buffer_full_status_rx_local[i]->bind(*sig_stat_r2p[i]);
        pe->ack_tx[i].bind(*sig_ack_r2p[i]);
        pe->buffer_full_status_tx[i].bind(*sig_stat_r2p[i]);

        // 连接 Router(out) --> PE(in)
        r->h_flit_tx_local[i]->bind(*sig_flit_r2p[i]);
        r->h_req_tx_local[i]->bind(*sig_req_r2p[i]);
        pe->flit_rx[i].bind(*sig_flit_r2p[i]);
        pe->req_rx[i].bind(*sig_req_r2p[i]);

        // 连接 PE(out) --> Router(in) (反向)
        pe->ack_rx[i].bind(*sig_ack_p2r[i]);
        pe->buffer_full_status_rx[i].bind(*sig_stat_p2r[i]);
        r->h_ack_tx_local[i]->bind(*sig_ack_p2r[i]);
        r->h_buffer_full_status_tx_local[i]->bind(*sig_stat_p2r[i]);
    }

    // Hierarchical ports initialization
	initHierarchicalPorts();
    connectRouterToHierarchicalPorts();
}
