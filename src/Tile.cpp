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