/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the top-level of Noxim
 */

#include "ConfigurationManager.h"
#include "DataStructs.h"
#include "GlobalParams.h"
#include "GlobalStats.h"
#include "NoC.h"

#include <csignal>

using namespace std;

// need to be globally visible to allow "-volume" simulation stop
unsigned int drained_volume;
NoC *n;

const std::string test_yaml_content = R"(
workload:
  working_set:
    - role: "ROLE_DRAM"
      data:
        - { data_space: "Weights", size: 96, reuse_strategy: "resident" }
        - { data_space: "Inputs", size: 18, reuse_strategy: "resident" }
        - { data_space: "Outputs", size: 512, reuse_strategy: "resident" }
    - role: "ROLE_GLB"
      data:
        - { data_space: "Weights", size: 96, reuse_strategy: "resident" }
        - { data_space: "Inputs", size: 18, reuse_strategy: "resident" }
        - { data_space: "Outputs", size: 512, reuse_strategy: "resident" }
    - role: "ROLE_BUFFER"
      data:
        - { data_space: "Weights", size: 6, reuse_strategy: "temporal" }
        - { data_space: "Inputs",  size: 3, reuse_strategy: "temporal" }
        - { data_space: "Outputs", size: 2, reuse_strategy: "temporal" }

  data_flow_specs:

    - role: "ROLE_DRAM"
      schedule_template:
        total_timesteps: 1
        delta_events:
          - trigger: { on_timestep: "default" }
            name: "INITIAL_LOAD_TO_GLB"
            target_group: "1,"
            delta:
              - { data_space: "Weights", size: 96 }
              - { data_space: "Inputs",  size: 18 }
              - { data_space: "Outputs", size: 512 }

    # -----------------------------------------------------------
    # 规格 B: 针对 ROLE_GLB
    # -----------------------------------------------------------
    - role: "ROLE_GLB"
      schedule_template:
        total_timesteps: 256
        delta_events:
          - trigger: { on_timestep_modulo: [16, 0] }
            name: "FILL_TO_PES"
            target_group: "2,"
            
            delta:
              - { data_space: "Weights", size: 6 }
              - { data_space: "Inputs",  size: 3 }
              - { data_space: "Outputs", size: 2 }

          - trigger: { on_timestep: "default" }
            name: "DELTA_TO_PES"
            target_group: "2,"
            
            delta:
              - { data_space: "Inputs",  size: 1 }
              - { data_space: "Outputs", size: 2 }

      command_definitions:
        - command_id: 0
          name: "EVICT_AFTER_INIT_LOAD"
          evict_payload: { Weights: 96, Inputs: 18, Outputs: 512 }

    # -----------------------------------------------------------
    # 规格 C: 针对 ROLE_COMPUTE
    # -----------------------------------------------------------
    - role: "ROLE_BUFFER"
      properties:
        compute_latency: 6
        
      command_definitions:
        - command_id: 0
          name: "EVICT_DELTA"

          evict_payload: { Weights: 0, Inputs: 1, Outputs: 2 }
          
        - command_id: 1
          name: "EVICT_FULL_CONTEXT"
          evict_payload: { Weights: 6, Inputs: 3, Outputs: 2 } 
)";

void signalHandler(int signum) {
  cout << "\b\b  " << endl;
  cout << endl;
  cout << "Current Statistics:" << endl;
  cout << "(" << sc_time_stamp().to_double() / GlobalParams::clock_period_ps
       << " sim cycles executed)" << endl;
  GlobalStats gs(n);
  gs.showStats(std::cout, GlobalParams::detailed);
}

int sc_main(int arg_num, char *arg_vet[]) {
  signal(SIGQUIT, signalHandler);

  // TEMP
  drained_volume = 0;

  // Handle command-line arguments
  cout << "\t--------------------------------------------" << endl;
  cout << "\t\tNoxim - the NoC Simulator" << endl;
  cout << "\t\t(C) University of Catania" << endl;
  cout << "\t--------------------------------------------" << endl;

  cout << "Catania V., Mineo A., Monteleone S., Palesi M., and Patti D. (2016) "
          "Cycle-Accurate Network on Chip Simulation with Noxim. ACM Trans. "
          "Model. Comput. Simul. 27, 1, Article 4 (August 2016), 25 pages. "
          "DOI: https://doi.org/10.1145/2953878"
       << endl;
  cout << endl;
  cout << endl;

  configure(arg_num, arg_vet);
  // GlobalParams::workload = loadWorkloadConfigFromString(test_yaml_content);

  // GlobalParams::CapabilityMap[ROLE_GLB].main_channel_caps = {0,18,96};
  // GlobalParams::CapabilityMap[ROLE_GLB].output_channel_caps = {0,512};
  // GlobalParams::CapabilityMap[ROLE_BUFFER].main_channel_caps = {0,1,3,6};
  // GlobalParams::CapabilityMap[ROLE_BUFFER].output_channel_caps = {0,2};

  // Signals
  sc_clock clock("clock", GlobalParams::clock_period_ps, SC_PS);
  sc_signal<bool> reset;

  // NoC instance
  n = new NoC("NoC");

  n->clock(clock);
  n->reset(reset);

  reset.write(1);
  cout << "Reset for " << (int)(GlobalParams::reset_time) << " cycles... ";
  srand(GlobalParams::rnd_generator_seed);

  // fix clock periods different from 1ns
  // sc_start(GlobalParams::reset_time, SC_NS);
  sc_start(GlobalParams::reset_time * GlobalParams::clock_period_ps, SC_PS);

  reset.write(0);
  cout << " done! " << endl;
  cout << " Now running for " << GlobalParams::simulation_time << " cycles..."
       << endl;
  // fix clock periods different from 1ns
  // sc_start(GlobalParams::simulation_time, SC_NS);
  sc_start(GlobalParams::simulation_time * GlobalParams::clock_period_ps,
           SC_PS);

  // Close the simulation
  // if (GlobalParams::trace_mode) sc_close_vcd_trace_file(tf);
  // cout << "Noxim simulation completed.";
  // cout << " (" << sc_time_stamp().to_double() / GlobalParams::clock_period_ps
  // << " cycles executed)" << endl; cout << endl;
  // assert(false);
  //  Show statistics
  cout << "=== Configuration Sources ===" << endl;
  cout << "Main config file: " << GlobalParams::config_filename << endl;
  cout << endl;
  GlobalStats gs(n);
  gs.showStats(std::cout, GlobalParams::detailed);
  n->showHierarchicalIdleStats(std::cout);

  if ((GlobalParams::max_volume_to_be_drained > 0) &&
      (sc_time_stamp().to_double() / GlobalParams::clock_period_ps -
           GlobalParams::reset_time >=
       GlobalParams::simulation_time)) {
    cout << endl
         << "WARNING! the number of flits specified with -volume option" << endl
         << "has not been reached. ( " << drained_volume << " instead of "
         << GlobalParams::max_volume_to_be_drained << " )" << endl
         << "You might want to try an higher value of simulation cycles" << endl
         << "using -sim option." << endl;

#ifdef TESTING
    cout << endl
         << " Sum of local drained flits: " << gs.drained_total << endl
         << endl
         << " Effective drained volume: " << drained_volume;
#endif
  }

#ifdef DEADLOCK_AVOIDANCE
  cout << "***** WARNING: DEADLOCK_AVOIDANCE ENABLED!" << endl;
#endif
  return 0;
}
