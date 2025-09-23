/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the declaration of the router
 */

#ifndef __NOXIMROUTER_H__
#define __NOXIMROUTER_H__
#define NUM_LOCAL_PORTS 1

#include <systemc.h>
#include "DataStructs.h"
#include "Buffer.h"
#include "Stats.h"
#include "GlobalRoutingTable.h"
#include "LocalRoutingTable.h"
#include "ReservationTable.h"
#include "Utils.h"
#include "routingAlgorithms/RoutingAlgorithm.h"
#include "routingAlgorithms/RoutingAlgorithms.h"

using namespace std;

extern unsigned int drained_volume;

SC_MODULE(Router)
{


    // I/O Ports
    sc_in_clk clock;		                  // The input clock for the router
    sc_in <bool> reset;                           // The reset signal for the router

    // Hierarchical Dynamic Ports
    // UP ports (parent connection)
    sc_in<Flit>* h_flit_rx_up;
    sc_in<bool>* h_req_rx_up;
    sc_out<bool>* h_ack_rx_up;
    sc_out<TBufferFullStatus>* h_buffer_full_status_rx_up;

    sc_out<Flit>* h_flit_tx_up;
    sc_out<bool>* h_req_tx_up;
    sc_in<bool>* h_ack_tx_up;
    sc_in<TBufferFullStatus>* h_buffer_full_status_tx_up;

    // DOWN ports (child connections)
    vector<sc_out<Flit>*> h_flit_tx_down;
    vector<sc_out<bool>*> h_req_tx_down;
    vector<sc_in<bool>*> h_ack_tx_down;
    vector<sc_in<TBufferFullStatus>*> h_buffer_full_status_tx_down;

    vector<sc_in<Flit>*> h_flit_rx_down;
    vector<sc_in<bool>*> h_req_rx_down;
    vector<sc_out<bool>*> h_ack_rx_down;
    vector<sc_out<TBufferFullStatus>*> h_buffer_full_status_rx_down;

    // LOCAL ports (PE connection)
    sc_in<Flit>* h_flit_rx_local[NUM_LOCAL_PORTS];
    sc_in<bool>* h_req_rx_local[NUM_LOCAL_PORTS];
    sc_out<bool>* h_ack_rx_local[NUM_LOCAL_PORTS];
    sc_out<TBufferFullStatus>* h_buffer_full_status_rx_local[NUM_LOCAL_PORTS];

    sc_out<Flit>* h_flit_tx_local[NUM_LOCAL_PORTS];
    sc_out<bool>* h_req_tx_local[NUM_LOCAL_PORTS];
    sc_in<bool>* h_ack_tx_local[NUM_LOCAL_PORTS];
    sc_in<TBufferFullStatus>* h_buffer_full_status_tx_local[NUM_LOCAL_PORTS];

    // Logical port type enumeration
    enum LogicalPortType { PORT_UP, PORT_LOCAL, PORT_DOWN };

    struct PortInfo {
        LogicalPortType type;
        int instance_index; 
        std::string name;
    };

    // Unified Interface Adapter
    vector<sc_in<Flit>*> all_flit_rx;
    vector<sc_in<bool>*> all_req_rx;
    vector<sc_out<bool>*> all_ack_rx;
    vector<sc_out<TBufferFullStatus>*> all_buffer_full_status_rx;

    vector<sc_out<Flit>*> all_flit_tx;
    vector<sc_out<bool>*> all_req_tx;
    vector<sc_in<bool>*> all_ack_tx;
    vector<sc_in<TBufferFullStatus>*> all_buffer_full_status_tx;

    vector<BufferBank*> buffers;
    vector<PortInfo> port_info_map;
    vector<bool> current_level_rx;
    vector<bool> current_level_tx;
    vector<int> start_from_vc;

    // Registers

    int local_id;		                // Unique ID
    int local_level;		              // Hierarchical level
    int routing_type;		                // Type of routing algorithm
    int selection_type;
    Stats stats;		                // Statistics
    Power power;
    LocalRoutingTable routing_table;		// Routing table
    ReservationTable reservation_table;		// Switch reservation table
    unsigned long routed_flits;
    RoutingAlgorithm * routingAlgorithm;
    
    // Functions

    void process();
    void rxProcess();		// The receiving process
    void txProcess();		// The transmitting process
    void perCycleUpdate();
    void configure(const int _id, const int _level, const double _warm_up_time,
		   const unsigned int _max_buffer_size,
		   GlobalRoutingTable & grt);

    unsigned long getRoutedFlits();	// Returns the number of routed flits 
    SC_HAS_PROCESS(Router);
    Router(sc_module_name nm); 
        void initPorts();
    void buildUnifiedInterface();
    ~Router();

    vector<int> routeMulticast(const MulticastRouteData & route_data);
    vector<int> getMulticastChildren(const vector<int>& dst_ids);
    bool isMulticastToLocalOnly(const vector<int>& dst_ids);

  private:

    // Dynamic port management

    void cleanupPorts();
    

    public:
    // performs actual routing + selection
    int route(const RouteData & route_data);
private:

    // wrappers
    int selectionFunction(const vector <int> &directions,
			  const RouteData & route_data);
    vector < int >routingFunction(const RouteData & route_data);

    NoP_data getCurrentNoPData();
    void NoP_report() const;
    int NoPScore(const NoP_data & nop_data, const vector <int> & nop_channels) const;
    int reflexDirection(int direction) const;
    int getNeighborId(int _id, int direction) const;

    vector<int> getNextHops(int src, int dst);
    int start_from_port;	     // Port from which to start the reservation cycle
  public:
    unsigned int local_drained;

    bool inCongestion();
    void ShowBuffersStats(std::ostream & out);

    bool connectedHubs(int src_hub, int dst_hub);
    bool isDescendant(int dst_id) const;
    int getNextHopNode(int dst_id) const;
    int getLogicalPortIndex(LogicalPortType type, int down_index = -1) const;
};

#endif
