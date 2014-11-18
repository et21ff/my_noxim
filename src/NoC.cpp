/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2010 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the Network-on-Chip
 */

#include "NoC.h"

void NoC::buildMesh(char const * cfg_fname)
{
    YAML::Node config = YAML::LoadFile(cfg_fname);
  
    char channel_name[15];
    // TODO: replace with dynamic code based on configuration file
    channel = (Channel **) malloc (config["Channels"].size() * sizeof(Channel*));
    for (size_t i = 0; i < config["Channels"].size() ; i++) {
        sprintf(channel_name, "Channel_%lu", i);
        channel[i] = new Channel(channel_name);
    }

    char hub_name[15];
    // TODO: replace with dynamic code based on configuration file
    h = (Hub **) malloc (config["Hubs"].size() * sizeof(Hub*));
    //for (int i=0;i<MAX_HUBS;i++)
    for (size_t i = 0; i < config["Hubs"].size() ; i++) {
        sprintf(hub_name, "HUB_%lu", i);
        cout << "Creating HUB " << i << endl;
        h[i] = new Hub(hub_name);
        h[i]->local_id = i;
        h[i]->clock(clock);
        h[i]->reset(reset);
    }

    // Check for routing table availability
    if (GlobalParams::routing_algorithm == ROUTING_TABLE_BASED)
	assert(grtable.load(GlobalParams::routing_table_filename));

    // Check for traffic table availability
    if (GlobalParams::traffic_distribution == TRAFFIC_TABLE_BASED)
	assert(gttable.load(GlobalParams::traffic_table_filename));

    // Determine, from configuration file, which Hub is connected to which Tile
    int **hub_for_tile;

    hub_for_tile = (int **) malloc(GlobalParams::mesh_dim_y * sizeof(int **));
    assert(hub_for_tile != NULL);

    for(int i = 0; i < GlobalParams::mesh_dim_y; i++) {
        hub_for_tile[i] = (int *) malloc(GlobalParams::mesh_dim_x * sizeof(int *));
        assert(hub_for_tile[i] != NULL);
    }

    for(YAML::const_iterator hubs_it = config["Hubs"].begin(); 
            hubs_it != config["Hubs"].end();
            ++hubs_it) {

        int hub_id = hubs_it->first.as<int>();
        YAML::Node hub = hubs_it->second;

        for (size_t i = 0; i < hub["attachedNodes"].size(); i++){
            int tile_id = hub["attachedNodes"][i].as<int>();
            hub_for_tile[tile_id % GlobalParams::mesh_dim_x][tile_id / GlobalParams::mesh_dim_x] = hub_id;
        }
    } 
   
    // DEBUG Print Tile / Hub connections 
    for (int i = 0; i < GlobalParams::mesh_dim_x; i++) {
	    for (int j = 0; j < GlobalParams::mesh_dim_y; j++) {
            cout << "t[" << i << "][" << j << "] => " << hub_for_tile[i][j] << endl;
        }
    }

    // Var to track Hub connected ports
    int * hub_connected_ports = (int *) calloc(config["Hubs"].size(), sizeof(int));


    // Create the mesh as a matrix of tiles
    for (int i = 0; i < GlobalParams::mesh_dim_x; i++) {
	for (int j = 0; j < GlobalParams::mesh_dim_y; j++) {
	    // Create the single Tile with a proper name
	    char tile_name[20];
	    sprintf(tile_name, "Tile[%02d][%02d]", i, j);
	    t[i][j] = new Tile(tile_name);

	    cout << "> Setting " << tile_name << endl;

	    // Tell to the router its coordinates
	    t[i][j]->r->configure(j * GlobalParams::mesh_dim_x + i,
				  GlobalParams::stats_warm_up_time,
				  GlobalParams::buffer_depth,
				  grtable);

	    // Tell to the PE its coordinates
	    t[i][j]->pe->local_id = j * GlobalParams::mesh_dim_x + i;
	    t[i][j]->pe->traffic_table = &gttable;	// Needed to choose destination
	    t[i][j]->pe->never_transmit = (gttable.occurrencesAsSource(t[i][j]->pe->local_id) == 0);

	    // Map clock and reset
	    t[i][j]->clock(clock);
	    t[i][j]->reset(reset);

	    // Map Rx signals
	    t[i][j]->req_rx[DIRECTION_NORTH] (req_to_south[i][j]);
	    t[i][j]->flit_rx[DIRECTION_NORTH] (flit_to_south[i][j]);
	    t[i][j]->ack_rx[DIRECTION_NORTH] (ack_to_north[i][j]);

	    t[i][j]->req_rx[DIRECTION_EAST] (req_to_west[i + 1][j]);
	    t[i][j]->flit_rx[DIRECTION_EAST] (flit_to_west[i + 1][j]);
	    t[i][j]->ack_rx[DIRECTION_EAST] (ack_to_east[i + 1][j]);

	    t[i][j]->req_rx[DIRECTION_SOUTH] (req_to_north[i][j + 1]);
	    t[i][j]->flit_rx[DIRECTION_SOUTH] (flit_to_north[i][j + 1]);
	    t[i][j]->ack_rx[DIRECTION_SOUTH] (ack_to_south[i][j + 1]);

	    t[i][j]->req_rx[DIRECTION_WEST] (req_to_east[i][j]);
	    t[i][j]->flit_rx[DIRECTION_WEST] (flit_to_east[i][j]);
	    t[i][j]->ack_rx[DIRECTION_WEST] (ack_to_west[i][j]);

	    // Map Tx signals
	    t[i][j]->req_tx[DIRECTION_NORTH] (req_to_north[i][j]);
	    t[i][j]->flit_tx[DIRECTION_NORTH] (flit_to_north[i][j]);
	    t[i][j]->ack_tx[DIRECTION_NORTH] (ack_to_south[i][j]);

	    t[i][j]->req_tx[DIRECTION_EAST] (req_to_east[i + 1][j]);
	    t[i][j]->flit_tx[DIRECTION_EAST] (flit_to_east[i + 1][j]);
	    t[i][j]->ack_tx[DIRECTION_EAST] (ack_to_west[i + 1][j]);

	    t[i][j]->req_tx[DIRECTION_SOUTH] (req_to_south[i][j + 1]);
	    t[i][j]->flit_tx[DIRECTION_SOUTH] (flit_to_south[i][j + 1]);
	    t[i][j]->ack_tx[DIRECTION_SOUTH] (ack_to_north[i][j + 1]);

	    t[i][j]->req_tx[DIRECTION_WEST] (req_to_west[i][j]);
	    t[i][j]->flit_tx[DIRECTION_WEST] (flit_to_west[i][j]);
	    t[i][j]->ack_tx[DIRECTION_WEST] (ack_to_east[i][j]);

	    // TODO: check if hub signal is always required
	    // signals/port when tile receives(rx) from hub
	    t[i][j]->hub_req_rx(req_from_hub[i][j]);
	    t[i][j]->hub_flit_rx(flit_from_hub[i][j]);
	    t[i][j]->hub_ack_rx(ack_to_hub[i][j]);

	    // signals/port when tile transmits(tx) to hub
	    t[i][j]->hub_req_tx(req_to_hub[i][j]); // 7, sc_out
	    t[i][j]->hub_flit_tx(flit_to_hub[i][j]);
	    t[i][j]->hub_ack_tx(ack_from_hub[i][j]);
/*
	    // TODO: wireless hub test - replace with dynamic code
	    // using configuration file
	    // node 0,0 connected to Hub 0
	    // node 3,3 connected to Hub 1

	    // TODO: where pointers should be allocated ?
	    if (i==0 && j==0)
	    {
		h[0]->init[0]->socket.bind(channel->targ_socket );
		channel->init_socket.bind(h[0]->target[0]->socket);

		// hub receives
		h[0]->req_rx[0](req_to_hub[i][j]);
		h[0]->flit_rx[0](flit_to_hub[i][j]);
		h[0]->ack_rx[0](ack_from_hub[i][j]);

		// hub transmits
		h[0]->req_tx[0](req_from_hub[i][j]);
		h[0]->flit_tx[0](flit_from_hub[i][j]);
		h[0]->ack_tx[0](ack_to_hub[i][j]);
	    }
	    else
	    if (i==3 && j==3)
	    {
		h[1]->init[0]->socket.bind(channel->targ_socket );
		channel->init_socket.bind(h[1]->target[0]->socket);

		h[1]->req_rx[0](req_to_hub[i][j]);
		h[1]->flit_rx[0](flit_to_hub[i][j]);
		h[1]->ack_rx[0](ack_from_hub[i][j]);

		h[1]->flit_tx[0](flit_from_hub[i][j]);
		h[1]->req_tx[0](req_from_hub[i][j]);
		h[1]->ack_tx[0](ack_to_hub[i][j]);

	    }
	    else
	    {
	    }
*/
        // TODO: Review port index. Connect each Hub to all its Channels 
        int hub_id = hub_for_tile[i][j];
	// The next time that the same HUB is considered, the next
	// port will be connected
        int port = hub_connected_ports[hub_id]++;


        //if (port == 0) {
            cout << "HUB ID " << hub_id << "Connected port " << port << endl;
            h[hub_id]->init[0]->socket.bind(channel[0]->targ_socket);
            channel[0]->init_socket.bind(h[hub_id]->target[0]->socket);

            h[hub_id]->req_rx[port](req_to_hub[i][j]);
            h[hub_id]->flit_rx[port](flit_to_hub[i][j]);
            h[hub_id]->ack_rx[port](ack_from_hub[i][j]);

            h[hub_id]->flit_tx[port](flit_from_hub[i][j]);
            h[hub_id]->req_tx[port](req_from_hub[i][j]);
            h[hub_id]->ack_tx[port](ack_to_hub[i][j]);
        //}

	    // Map buffer level signals (analogy with req_tx/rx port mapping)
	    t[i][j]->free_slots[DIRECTION_NORTH] (free_slots_to_north[i][j]);
	    t[i][j]->free_slots[DIRECTION_EAST] (free_slots_to_east[i + 1][j]);
	    t[i][j]->free_slots[DIRECTION_SOUTH] (free_slots_to_south[i][j + 1]);
	    t[i][j]->free_slots[DIRECTION_WEST] (free_slots_to_west[i][j]);

	    t[i][j]->free_slots_neighbor[DIRECTION_NORTH] (free_slots_to_south[i][j]);
	    t[i][j]->free_slots_neighbor[DIRECTION_EAST] (free_slots_to_west[i + 1][j]);
	    t[i][j]->free_slots_neighbor[DIRECTION_SOUTH] (free_slots_to_north[i][j + 1]);
	    t[i][j]->free_slots_neighbor[DIRECTION_WEST] (free_slots_to_east[i][j]);

	    // NoP 
	    t[i][j]->NoP_data_out[DIRECTION_NORTH] (NoP_data_to_north[i][j]);
	    t[i][j]->NoP_data_out[DIRECTION_EAST] (NoP_data_to_east[i + 1][j]);
	    t[i][j]->NoP_data_out[DIRECTION_SOUTH] (NoP_data_to_south[i][j + 1]);
	    t[i][j]->NoP_data_out[DIRECTION_WEST] (NoP_data_to_west[i][j]);

	    t[i][j]->NoP_data_in[DIRECTION_NORTH] (NoP_data_to_south[i][j]);
	    t[i][j]->NoP_data_in[DIRECTION_EAST] (NoP_data_to_west[i + 1][j]);
	    t[i][j]->NoP_data_in[DIRECTION_SOUTH] (NoP_data_to_north[i][j + 1]);
	    t[i][j]->NoP_data_in[DIRECTION_WEST] (NoP_data_to_east[i][j]);
	}
    }

    // dummy NoP_data structure
    NoP_data tmp_NoP;

    tmp_NoP.sender_id = NOT_VALID;

    for (int i = 0; i < DIRECTIONS; i++) {
	tmp_NoP.channel_status_neighbor[i].free_slots = NOT_VALID;
	tmp_NoP.channel_status_neighbor[i].available = false;
    }

    // Clear signals for borderline nodes
    for (int i = 0; i <= GlobalParams::mesh_dim_x; i++) {
	req_to_south[i][0] = 0;
	ack_to_north[i][0] = 0;
	req_to_north[i][GlobalParams::mesh_dim_y] = 0;
	ack_to_south[i][GlobalParams::mesh_dim_y] = 0;

	free_slots_to_south[i][0].write(NOT_VALID);
	free_slots_to_north[i][GlobalParams::mesh_dim_y].write(NOT_VALID);

	NoP_data_to_south[i][0].write(tmp_NoP);
	NoP_data_to_north[i][GlobalParams::mesh_dim_y].write(tmp_NoP);

    }

    for (int j = 0; j <= GlobalParams::mesh_dim_y; j++) {
	req_to_east[0][j] = 0;
	ack_to_west[0][j] = 0;
	req_to_west[GlobalParams::mesh_dim_x][j] = 0;
	ack_to_east[GlobalParams::mesh_dim_x][j] = 0;

	free_slots_to_east[0][j].write(NOT_VALID);
	free_slots_to_west[GlobalParams::mesh_dim_x][j].write(NOT_VALID);

	NoP_data_to_east[0][j].write(tmp_NoP);
	NoP_data_to_west[GlobalParams::mesh_dim_x][j].write(tmp_NoP);

    }

    // invalidate reservation table entries for non-exhistent channels
    for (int i = 0; i < GlobalParams::mesh_dim_x; i++) {
	t[i][0]->r->reservation_table.invalidate(DIRECTION_NORTH);
	t[i][GlobalParams::mesh_dim_y - 1]->r->reservation_table.invalidate(DIRECTION_SOUTH);
    }
    for (int j = 0; j < GlobalParams::mesh_dim_y; j++) {
	t[0][j]->r->reservation_table.invalidate(DIRECTION_WEST);
	t[GlobalParams::mesh_dim_x - 1][j]->r->reservation_table.invalidate(DIRECTION_EAST);
    }
}

Tile *NoC::searchNode(const int id) const
{
    for (int i = 0; i < GlobalParams::mesh_dim_x; i++)
	for (int j = 0; j < GlobalParams::mesh_dim_y; j++)
	    if (t[i][j]->r->local_id == id)
		return t[i][j];

    return NULL;
}