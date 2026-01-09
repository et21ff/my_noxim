/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementaton of the global statistics
 */

#include "GlobalStats.h"
#include <unordered_map>
using namespace std;

GlobalStats::GlobalStats(const NoC *_noc) {
  noc = _noc;

#ifdef TESTING
  drained_total = 0;
#endif
}

double GlobalStats::getAverageDelay() {
  unsigned int total_packets = 0;
  double avg_delay = 0.0;

  if (GlobalParams::topology == TOPOLOGY_MESH) {
    for (int y = 0; y < GlobalParams::mesh_dim_y; y++)
      for (int x = 0; x < GlobalParams::mesh_dim_x; x++) {
        int node_id = y * GlobalParams::mesh_dim_x + x;
        unsigned int received_packets =
            noc->t[node_id]->r->stats.getReceivedPackets();

        if (received_packets) {
          avg_delay +=
              received_packets * noc->t[node_id]->r->stats.getAverageDelay();
          total_packets += received_packets;
        }
      }
  } else if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++) {
      unsigned int received_packets = noc->t[i]->r->stats.getReceivedPackets();

      if (received_packets) {
        avg_delay += received_packets * noc->t[i]->r->stats.getAverageDelay();
        total_packets += received_packets;
      }
    }
  } else // other delta topologies
  {
    for (int y = 0; y < GlobalParams::n_delta_tiles; y++) {
      unsigned int received_packets =
          noc->core[y]->r->stats.getReceivedPackets();

      if (received_packets) {
        avg_delay +=
            received_packets * noc->core[y]->r->stats.getAverageDelay();
        total_packets += received_packets;
      }
    }
  }

  avg_delay /= (double)total_packets;

  return avg_delay;
}

double GlobalStats::getAverageDelay(const int src_id, const int dst_id) {
  Tile *tile = noc->searchNode(dst_id);

  assert(tile != NULL);

  return tile->r->stats.getAverageDelay(src_id);
}

double GlobalStats::getMaxDelay() {
  double maxd = -1.0;

  if (GlobalParams::topology == TOPOLOGY_MESH) {
    for (int y = 0; y < GlobalParams::mesh_dim_y; y++)
      for (int x = 0; x < GlobalParams::mesh_dim_x; x++) {
        Coord coord;
        coord.x = x;
        coord.y = y;
        int node_id = coord2Id(coord);
        double d = getMaxDelay(node_id);
        if (d > maxd)
          maxd = d;
      }

  } else // other delta topologies
  {
    for (int y = 0; y < GlobalParams::n_delta_tiles; y++) {
      double d = getMaxDelay(y);
      if (d > maxd)
        maxd = d;
    }
  }

  return maxd;
}

double GlobalStats::getMaxDelay(const int node_id) {
  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {

    unsigned int received_packets =
        noc->t[node_id]->r->stats.getReceivedPackets();

    if (received_packets)
      return noc->t[node_id]->r->stats.getMaxDelay();
    else
      return -1.0;
  } else // other delta topologies
  {
    unsigned int received_packets =
        noc->core[node_id]->r->stats.getReceivedPackets();
    if (received_packets)
      return noc->core[node_id]->r->stats.getMaxDelay();
    else
      return -1.0;
  }
}

double GlobalStats::getMaxDelay(const int src_id, const int dst_id) {
  Tile *tile = noc->searchNode(dst_id);

  assert(tile != NULL);

  return tile->r->stats.getMaxDelay(src_id);
}

vector<vector<double>> GlobalStats::getMaxDelayMtx() {
  vector<vector<double>> mtx;

  assert(GlobalParams::topology == TOPOLOGY_MESH);

  mtx.resize(GlobalParams::mesh_dim_y);
  for (int y = 0; y < GlobalParams::mesh_dim_y; y++)
    mtx[y].resize(GlobalParams::mesh_dim_x);

  for (int y = 0; y < GlobalParams::mesh_dim_y; y++)
    for (int x = 0; x < GlobalParams::mesh_dim_x; x++) {
      Coord coord;
      coord.x = x;
      coord.y = y;
      int id = coord2Id(coord);
      mtx[y][x] = getMaxDelay(id);
    }

  return mtx;
}

double GlobalStats::getAverageThroughput(const int src_id, const int dst_id) {
  Tile *tile = noc->searchNode(dst_id);

  assert(tile != NULL);

  return tile->r->stats.getAverageThroughput(src_id);
}

/*
double GlobalStats::getAverageThroughput()
{
    unsigned int total_comms = 0;
    double avg_throughput = 0.0;

    for (int y = 0; y < GlobalParams::mesh_dim_y; y++)
        for (int x = 0; x < GlobalParams::mesh_dim_x; x++) {
            unsigned int ncomms =
                noc->t[x][y]->r->stats.getTotalCommunications();

            if (ncomms) {
                avg_throughput +=
                    ncomms * noc->t[x][y]->r->stats.getAverageThroughput();
                total_comms += ncomms;
            }
        }

    avg_throughput /= (double) total_comms;

    return avg_throughput;
}
*/

double GlobalStats::getAggregatedThroughput() {
  int total_cycles =
      GlobalParams::simulation_time - GlobalParams::stats_warm_up_time;

  return (double)getReceivedFlits() / (double)(total_cycles);
}

unsigned int GlobalStats::getReceivedPackets() {
  unsigned int n = 0;

  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++)
      n += noc->t[i]->r->stats.getReceivedPackets();
  } else // other delta topologies
  {
    for (int y = 0; y < GlobalParams::n_delta_tiles; y++)
      n += noc->core[y]->r->stats.getReceivedPackets();
  }

  return n;
}

unsigned int GlobalStats::getReceivedFlits() {
  unsigned int n = 0;
  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++)
      n += noc->t[i]->r->stats.getReceivedFlits();
#ifdef TESTING
    drained_total += noc->t[i].r->local_drained;
#endif
  } else // other delta topologies
  {
    for (int y = 0; y < GlobalParams::n_delta_tiles; y++) {
      n += noc->core[y]->r->stats.getReceivedFlits();
#ifdef TESTING
      drained_total += noc->core[y]->r->local_drained;
#endif
    }
  }

  return n;
}

double GlobalStats::getThroughput() {
  if (GlobalParams::topology == TOPOLOGY_MESH) {
    int number_of_ip = GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y;
    return (double)getAggregatedThroughput() / (double)(number_of_ip);
  } else // other delta topologies
  {
    int number_of_ip = GlobalParams::n_delta_tiles;
    return (double)getAggregatedThroughput() / (double)(number_of_ip);
  }
}

// Only accounting IP that received at least one flit
double GlobalStats::getActiveThroughput() {
  int total_cycles =
      GlobalParams::simulation_time - GlobalParams::stats_warm_up_time;
  unsigned int n = 0;
  unsigned int trf = 0;
  unsigned int rf;
  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++) {
      rf = noc->t[i]->r->stats.getReceivedFlits();

      if (rf != 0)
        n++;

      trf += rf;
    }
  } else // other delta topologies
  {
  }

  return (double)trf / (double)(total_cycles * n);
}

vector<unsigned long> GlobalStats::getRoutedFlitsMtx() {

  vector<unsigned long> mtx;
  assert(GlobalParams::topology == TOPOLOGY_HIERARCHICAL);

  mtx.resize(GlobalParams::num_nodes);
  for (int i = 0; i < GlobalParams::num_nodes; i++)
    mtx[i] = noc->t[i]->r->getRoutedFlits();

  return mtx;
}

unsigned int GlobalStats::getWirelessPackets() {
  unsigned int packets = 0;

  // Wireless noc
  for (map<int, HubConfig>::iterator it =
           GlobalParams::hub_configuration.begin();
       it != GlobalParams::hub_configuration.end(); ++it) {
    int hub_id = it->first;

    map<int, Hub *>::const_iterator i = noc->hub.find(hub_id);
    Hub *h = i->second;

    packets += h->wireless_communications_counter;
  }
  return packets;
}

double GlobalStats::getDynamicPower() {
  double power = 0.0;

  // Electric noc
  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++)
      power += noc->t[i]->r->power.getDynamicPower();
  } else // other delta topologies
  {
  }

  // Wireless noc
  for (map<int, HubConfig>::iterator it =
           GlobalParams::hub_configuration.begin();
       it != GlobalParams::hub_configuration.end(); ++it) {
    int hub_id = it->first;

    map<int, Hub *>::const_iterator i = noc->hub.find(hub_id);
    Hub *h = i->second;

    power += h->power.getDynamicPower();
  }
  return power;
}

double GlobalStats::getStaticPower() {
  double power = 0.0;

  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++)
      power += noc->t[i]->r->power.getStaticPower();
  } else // other delta topologies
  {
  }

  // Wireless noc
  for (map<int, HubConfig>::iterator it =
           GlobalParams::hub_configuration.begin();
       it != GlobalParams::hub_configuration.end(); ++it) {
    int hub_id = it->first;

    map<int, Hub *>::const_iterator i = noc->hub.find(hub_id);
    Hub *h = i->second;

    power += h->power.getStaticPower();
  }
  return power;
}

void GlobalStats::showStats(std::ostream &out, bool detailed) {
  if (detailed) {
    assert(GlobalParams::topology == TOPOLOGY_MESH);
    out << endl << "detailed = [" << endl;

    for (int i = 0; i < GlobalParams::num_nodes; i++)
      noc->t[i]->r->stats.showStats(i, out, true);
    out << "];" << endl;

    // show MaxDelay matrix
    vector<vector<double>> md_mtx = getMaxDelayMtx();

    out << endl << "max_delay = [" << endl;
    for (unsigned int y = 0; y < md_mtx.size(); y++) {
      out << "   ";
      for (unsigned int x = 0; x < md_mtx[y].size(); x++)
        out << setw(6) << md_mtx[y][x];
      out << endl;
    }
    out << "];" << endl;

    // show RoutedFlits matrix
    vector<unsigned long> rf_mtx = getRoutedFlitsMtx();

    out << endl << "routed_flits = [" << endl;
    for (unsigned int i = 0; i < rf_mtx.size(); i++) {
      out << "   " << setw(10) << rf_mtx[i] << endl;
    }
    out << "];" << endl;

    showPowerBreakDown(out);
    showPowerManagerStats(out);
  }

#ifdef DEBUG

  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++)
      out << "PE" << i << ": " << noc->t[i]->pe->getQueueSize() << ",";
  } else // other delta topologies
  {
  }

  out << endl;
#endif

  // int total_cycles = GlobalParams::simulation_time -
  // GlobalParams::stats_warm_up_time;
  out << "% Total received packets: " << getReceivedPackets() << endl;
  out << "% Total received flits: " << getReceivedFlits() << endl;
  out << "% Received/Ideal flits Ratio: " << getReceivedIdealFlitRatio()
      << endl;
  out << "% Average wireless utilization: "
      << getWirelessPackets() / (double)getReceivedPackets() << endl;
  out << "% Global average delay (cycles): " << getAverageDelay() << endl;
  out << "% Max delay (cycles): " << getMaxDelay() << endl;
  out << "% Network throughput (flits/cycle): " << getAggregatedThroughput()
      << endl;
  out << "% Average IP throughput (flits/cycle/IP): " << getThroughput()
      << endl;
  out << "% Total energy (J): " << getTotalPower() << endl;
  out << "% \tDynamic energy (J): " << getDynamicPower() << endl;
  out << "% \tStatic energy (J): " << getStaticPower() << endl;

  if (GlobalParams::show_buffer_stats)
    showBufferStats(out);

  // PE数据等待统计（按层聚合）
  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    // 显示层级统计
    showLayerStats(out);

    out << "% PE Data Wait Statistics (by layer):" << endl;

    // 按层聚合统计
    std::vector<std::unordered_map<DataType, size_t>> layer_wait_stats(
        GlobalParams::num_levels);
    std::vector<size_t> layer_total_wait(GlobalParams::num_levels, 0);
    std::vector<int> layer_node_count(GlobalParams::num_levels, 0);

    // 收集每层的统计数据
    for (int i = 0; i < GlobalParams::num_nodes; i++) {
      int level = GlobalParams::node_level_map[i];
      const auto &wait_stats = noc->t[i]->pe->getDataWaitStats();
      size_t total_wait = noc->t[i]->pe->getTotalWaitCycles();

      layer_total_wait[level] += total_wait;
      layer_node_count[level]++;

      for (const auto &entry : wait_stats) {
        layer_wait_stats[level][entry.first] += entry.second;
      }
    }

    // 输出每层统计
    for (int level = 0; level < GlobalParams::num_levels; level++) {
      if (layer_total_wait[level] > 0 && layer_node_count[level] > 0) {
        double avg_wait =
            (double)layer_total_wait[level] / layer_node_count[level];
        out << "% Level " << level << " Average wait cycles: " << avg_wait
            << endl;
        out << "%   Wait breakdown:" << endl;

        for (const auto &entry : layer_wait_stats[level]) {
          double percentage = 100.0 * entry.second / layer_total_wait[level];
          double avg_wait_by_type =
              (double)entry.second / layer_node_count[level];
          out << "%     " << DataType_to_str(entry.first) << ": "
              << avg_wait_by_type << " cycles (" << percentage << "%)" << endl;
        }
      }
    }
  }
}

void GlobalStats::updatePowerBreakDown(map<string, double> &dst,
                                       PowerBreakdown *src) {
  for (int i = 0; i != src->size; i++) {
    dst[src->breakdown[i].label] += src->breakdown[i].value;
  }
}

void GlobalStats::showPowerManagerStats(std::ostream &out) {
  std::streamsize p = out.precision();
  int total_cycles =
      sc_time_stamp().to_double() / GlobalParams::clock_period_ps -
      GlobalParams::reset_time;

  out.precision(4);

  out << "powermanager_stats_tx = [" << endl;
  out << "%\tFraction of: TX Transceiver off (TTXoff), AntennaBufferTX off "
         "(ABTXoff) "
      << endl;
  out << "%\tHUB\tTTXoff\tABTXoff\t" << endl;

  for (map<int, HubConfig>::iterator it =
           GlobalParams::hub_configuration.begin();
       it != GlobalParams::hub_configuration.end(); ++it) {
    int hub_id = it->first;

    map<int, Hub *>::const_iterator i = noc->hub.find(hub_id);
    Hub *h = i->second;

    out << "\t" << hub_id << "\t" << std::fixed
        << (double)h->total_ttxoff_cycles / total_cycles << "\t";

    int s = 0;
    for (map<int, int>::iterator i = h->abtxoff_cycles.begin();
         i != h->abtxoff_cycles.end(); i++)
      s += i->second;

    out << (double)s / h->abtxoff_cycles.size() / total_cycles << endl;
  }

  out << "];" << endl;

  out << "powermanager_stats_rx = [" << endl;
  out << "%\tFraction of: RX Transceiver off (TRXoff), AntennaBufferRX off "
         "(ABRXoff), BufferToTile off (BTToff) "
      << endl;
  out << "%\tHUB\tTRXoff\tABRXoff\tBTToff\t" << endl;

  for (map<int, HubConfig>::iterator it =
           GlobalParams::hub_configuration.begin();
       it != GlobalParams::hub_configuration.end(); ++it) {
    string bttoff_str;

    out.precision(4);

    int hub_id = it->first;

    map<int, Hub *>::const_iterator i = noc->hub.find(hub_id);
    Hub *h = i->second;

    out << "\t" << hub_id << "\t" << std::fixed
        << (double)h->total_sleep_cycles / total_cycles << "\t";

    int s = 0;
    for (map<int, int>::iterator i = h->buffer_rx_sleep_cycles.begin();
         i != h->buffer_rx_sleep_cycles.end(); i++)
      s += i->second;

    out << (double)s / h->buffer_rx_sleep_cycles.size() / total_cycles << "\t";

    s = 0;
    for (map<int, int>::iterator i = h->buffer_to_tile_poweroff_cycles.begin();
         i != h->buffer_to_tile_poweroff_cycles.end(); i++) {
      double bttoff_fraction = i->second / (double)total_cycles;
      s += i->second;
      if (bttoff_fraction < 0.25)
        bttoff_str += " ";
      else if (bttoff_fraction < 0.5)
        bttoff_str += ".";
      else if (bttoff_fraction < 0.75)
        bttoff_str += "o";
      else if (bttoff_fraction < 0.90)
        bttoff_str += "O";
      else
        bttoff_str += "0";
    }
    out << (double)s / h->buffer_to_tile_poweroff_cycles.size() / total_cycles
        << "\t" << bttoff_str << endl;
  }

  out << "];" << endl;

  out.unsetf(std::ios::fixed);

  out.precision(p);
}

void GlobalStats::showPowerBreakDown(std::ostream &out) {
  map<string, double> power_dynamic;
  map<string, double> power_static;

  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++) {
      updatePowerBreakDown(power_dynamic,
                           noc->t[i]->r->power.getDynamicPowerBreakDown());
      updatePowerBreakDown(power_static,
                           noc->t[i]->r->power.getStaticPowerBreakDown());
    }
  } else // other delta topologies
  {
  }

  for (map<int, HubConfig>::iterator it =
           GlobalParams::hub_configuration.begin();
       it != GlobalParams::hub_configuration.end(); ++it) {
    int hub_id = it->first;

    map<int, Hub *>::const_iterator i = noc->hub.find(hub_id);
    Hub *h = i->second;

    updatePowerBreakDown(power_dynamic, h->power.getDynamicPowerBreakDown());

    updatePowerBreakDown(power_static, h->power.getStaticPowerBreakDown());
  }

  printMap("power_dynamic", power_dynamic, out);
  printMap("power_static", power_static, out);
}

void GlobalStats::showBufferStats(std::ostream &out) {
  out << "Router id\tBuffer N\t\tBuffer E\t\tBuffer S\t\tBuffer W\t\tBuffer L"
      << endl;
  out << "         \tMean\tMax\tMean\tMax\tMean\tMax\tMean\tMax\tMean\tMax"
      << endl;

  if (GlobalParams::topology == TOPOLOGY_HIERARCHICAL) {
    for (int i = 0; i < GlobalParams::num_nodes; i++) {
      out << noc->t[i]->r->local_id;
      noc->t[i]->r->ShowBuffersStats(out);
      out << endl;
    }
  } else // other delta topologies
  {
  }
}

double GlobalStats::getReceivedIdealFlitRatio() {
  int total_cycles;
  total_cycles =
      GlobalParams::simulation_time - GlobalParams::stats_warm_up_time;
  double ratio;
  if (GlobalParams::topology == TOPOLOGY_MESH) {
    ratio =
        getReceivedFlits() /
        (GlobalParams::packet_injection_rate *
         (GlobalParams::min_packet_size + GlobalParams::max_packet_size) / 2 *
         total_cycles * GlobalParams::mesh_dim_y * GlobalParams::mesh_dim_x);
  } else // other delta topologies
  {
    ratio = getReceivedFlits() /
            (GlobalParams::packet_injection_rate *
             (GlobalParams::min_packet_size + GlobalParams::max_packet_size) /
             2 * total_cycles * GlobalParams::n_delta_tiles);
  }
  return ratio;
}

void GlobalStats::showLayerStats(std::ostream &out) {
  if (GlobalParams::topology != TOPOLOGY_HIERARCHICAL) {
    out << "% Layer statistics only available for hierarchical topology"
        << endl;
    return;
  }

  out << "% === Layer-wise Statistics ===" << endl;

  // 计算每层的平均延迟和吞吐量
  std::vector<double> layer_avg_delay = getLayerAverageDelay();
  std::vector<double> layer_avg_throughput = getLayerAverageThroughput();

  for (int level = 0; level < GlobalParams::num_levels; level++) {
    out << "% Level " << level << " Statistics:" << endl;
    out << "%   Average delay: " << layer_avg_delay[level] << " cycles" << endl;
    out << "%   Average throughput: " << layer_avg_throughput[level]
        << " flits/cycle" << endl;
    out << "%   Node count: " << GlobalParams::fanouts_per_level[level] << endl;
  }
}

std::vector<double> GlobalStats::getLayerAverageDelay() {
  std::vector<double> layer_avg_delay(GlobalParams::num_levels, 0.0);
  std::vector<int> layer_node_count(GlobalParams::num_levels, 0);

  for (int i = 0; i < GlobalParams::num_nodes; i++) {
    int level = GlobalParams::node_level_map[i];
    unsigned int received_packets = noc->t[i]->r->stats.getReceivedPackets();

    if (received_packets > 0) {
      layer_avg_delay[level] +=
          noc->t[i]->r->stats.getAverageDelay() * received_packets;
      layer_node_count[level] += received_packets;
    }
  }

  // 计算每层的加权平均
  for (int level = 0; level < GlobalParams::num_levels; level++) {
    if (layer_node_count[level] > 0) {
      layer_avg_delay[level] /= layer_node_count[level];
    }
  }

  return layer_avg_delay;
}

std::vector<double> GlobalStats::getLayerAverageThroughput() {
  std::vector<double> layer_throughput(GlobalParams::num_levels, 0.0);
  std::vector<int> layer_node_count(GlobalParams::num_levels, 0);

  int total_cycles =
      GlobalParams::simulation_time - GlobalParams::stats_warm_up_time;

  for (int i = 0; i < GlobalParams::num_nodes; i++) {
    int level = GlobalParams::node_level_map[i];
    unsigned int received_flits = noc->t[i]->r->stats.getReceivedFlits();

    if (received_flits > 0) {
      layer_throughput[level] += received_flits;
      layer_node_count[level]++;
    }
  }

  // 计算每层的平均吞吐量
  for (int level = 0; level < GlobalParams::num_levels; level++) {
    if (layer_node_count[level] > 0) {
      layer_throughput[level] =
          layer_throughput[level] / (total_cycles * layer_node_count[level]);
    }
  }

  return layer_throughput;
}
