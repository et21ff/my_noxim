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
    
    pe = new MockPE("MockPE",local_id);
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

    int local_port_index = 0;

    // --- 链路 1: 数据从 PE 发往 Router (P2R) ---
     sig_flit_p2r = new sc_signal<Flit>("sig_flit_p2r");
     sig_req_p2r  = new sc_signal<bool>("sig_req_p2r");
    // 反向信号: Router -> PE
     sig_ack_r2p  = new sc_signal<bool>("sig_ack_r2p");
     sig_stat_r2p = new sc_signal<TBufferFullStatus>("sig_stat_r2p");

    // 连接 PE(out) --> Router(in)
    pe->flit_tx.bind(*sig_flit_p2r);
    pe->req_tx.bind(*sig_req_p2r);
    r->h_flit_rx_local[local_port_index]->bind(*sig_flit_p2r);
    r->h_req_rx_local[local_port_index]->bind(*sig_req_p2r);

    // 连接 Router(out) --> PE(in) (反向)
    r->h_ack_rx_local[local_port_index]->bind(*sig_ack_r2p);
    r->h_buffer_full_status_rx_local[local_port_index]->bind(*sig_stat_r2p);
    pe->ack_tx.bind(*sig_ack_r2p);
    pe->buffer_full_status_tx.bind(*sig_stat_r2p);

    // --- 链路 2: 数据从 Router 发往 PE (R2P) ---
    sig_flit_r2p = new sc_signal<Flit>("sig_flit_r2p");
    sig_req_r2p  = new sc_signal<bool>("sig_req_r2p");
        // 反向信号: PE -> Router
    sig_ack_p2r  = new sc_signal<bool>("sig_ack_p2r");
    sig_stat_p2r = new sc_signal<TBufferFullStatus>("sig_stat_p2r");
    
    // 连接 Router(out) --> PE(in)
    r->h_flit_tx_local[local_port_index]->bind(*sig_flit_r2p);
    r->h_req_tx_local[local_port_index]->bind(*sig_req_r2p);
    pe->flit_rx.bind(*sig_flit_r2p);
    pe->req_rx.bind(*sig_req_r2p);

    // 连接 PE(out) --> Router(in) (反向)
    pe->ack_rx.bind(*sig_ack_p2r);
    pe->buffer_full_status_rx.bind(*sig_stat_p2r);
    r->h_ack_tx_local[local_port_index]->bind(*sig_ack_p2r);
    r->h_buffer_full_status_tx_local[local_port_index]->bind(*sig_stat_p2r);

    // Hierarchical ports initialization
	initHierarchicalPorts();
}
