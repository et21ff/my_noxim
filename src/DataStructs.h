/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the declaration of the top-level of Noxim
 */

#ifndef _DATASTRUCS_H__
#define _DATASTRUCS_H__

#include "DataTypes.h"
#include "GlobalParams.h"
#include <systemc.h>
#include <vector>

// Coord -- XY coordinates type of the Tile inside the Mesh
class Coord
{
public:
  int x; // X coordinate
  int y; // Y coordinate

  inline bool operator==(const Coord &coord) const
  {
    return (coord.x == x && coord.y == y);
  }
};

// FlitType -- Flit type enumeration
enum FlitType
{
  FLIT_TYPE_HEAD,
  FLIT_TYPE_BODY,
  FLIT_TYPE_TAIL
};

// Payload -- Payload definition
struct Payload
{
  sc_uint<32> data; // Bus for the data to be exchanged

  inline bool operator==(const Payload &payload) const
  {
    return (payload.data == data);
  }
};

// Packet -- Packet definition
struct Packet
{
  int src_id;
  int dst_id;
  int vc_id;
  int logical_timestamp; // SC timestamp at packet generation
  int size;
  int flit_left; // Number of remaining flits inside the packet
  bool use_low_voltage_path;

  int payload_data_size; // 用来表示整个Packet的真实数据大小（以字节为单位）
  int payload_sizes[3];  // 索引0: INPUT, 1: WEIGHT, 2: OUTPUT
  DataType data_type;    // Packet的数据类型（FILL或DELTA）
  int command;           // 用于存储来自txprocess的命令 (timestamp,command)
  PE_Role target_role;

  Packet(const Packet &other) = default;
  Packet &operator=(const Packet &other) = default;
  // Constructors
  Packet() {}

  Packet(const int s, const int d, const int vc, const double ts,
         const int sz)
  {
    make(s, d, vc, ts, sz);
  }

  void make(const int s, const int d, const int vc, const int ts,
            const int sz)
  {
    src_id = s;
    dst_id = d;
    vc_id = vc;
    logical_timestamp = ts;
    size = sz;
    flit_left = sz;
    use_low_voltage_path = false;
  }

  int total_size()
  {
    return payload_sizes[0] + payload_sizes[1] + payload_sizes[2];
  }
};

// RouteData -- data required to perform routing
struct RouteData
{
  int current_id;
  int src_id;
  int dst_id;
  int dir_in; // direction from which the packet comes from
  int vc_id;
  bool is_output;
  DataType data_type; // true if the packet is an output packet
  PE_Role target_role;
  int command; // 用于存储来自txprocess的命令 (timestamp,command)
};

struct ChannelStatus
{
  int free_slots; // occupied buffer slots
  bool available; //
  inline bool operator==(const ChannelStatus &bs) const
  {
    return (free_slots == bs.free_slots && available == bs.available);
  };
};

// NoP_data -- NoP Data definition
struct NoP_data
{
  int sender_id;
  ChannelStatus channel_status_neighbor[DIRECTIONS];

  inline bool operator==(const NoP_data &nop_data) const
  {
    return (sender_id == nop_data.sender_id &&
            nop_data.channel_status_neighbor[0] == channel_status_neighbor[0] &&
            nop_data.channel_status_neighbor[1] == channel_status_neighbor[1] &&
            nop_data.channel_status_neighbor[2] == channel_status_neighbor[2] &&
            nop_data.channel_status_neighbor[3] == channel_status_neighbor[3]);
  };
};

struct TBufferFullStatus
{
  TBufferFullStatus()
  {
    for (int i = 0; i < MAX_VIRTUAL_CHANNELS; i++)
      mask[i] = false;
  };
  inline bool operator==(const TBufferFullStatus &bfs) const
  {
    for (int i = 0; i < MAX_VIRTUAL_CHANNELS; i++)
      if (mask[i] != bfs.mask[i])
        return false;
    return true;
  };

  bool mask[MAX_VIRTUAL_CHANNELS];
};

inline std::ostream &operator<<(std::ostream &os,
                                const TBufferFullStatus &bfs)
{
  os << "[ ";
  for (int i = 0; i < MAX_VIRTUAL_CHANNELS; ++i)
  {
    os << (bfs.mask[i] ? "T" : "F") << " ";
  }
  os << "]";
  return os;
}

inline void sc_trace(sc_core::sc_trace_file *tf, const TBufferFullStatus &bfs,
                     const std::string &name)
{
  for (int i = 0; i < MAX_VIRTUAL_CHANNELS; ++i)
  {
    // Creamos un nombre de señal único para cada booleano en el VCD, ej.
    // "mi_señal_mask_0"
    sc_trace(tf, bfs.mask[i], name + ".mask[" + std::to_string(i) + "]");
  }
}

struct Flit
{
  int payload_data_size = 0; // <--- 初始化

  // 数组初始化：全部置为 0
  int payload_sizes[3] = {0, 0, 0};

  int src_id = -2; // <--- 建议初始化为 -1，表示无效ID
  int dst_id = -2;
  int vc_id = -2;

  // 给枚举类型一个默认值，或者 FLIT_TYPE_HEAD
  FlitType flit_type;

  int sequence_no = -1;
  int sequence_length = -1;

  Payload payload; // Payload 是个结构体，如果它里面也没有构造函数，也需要去 Payload 定义里加初始化！

  int hop_no = -1;
  bool use_low_voltage_path = false;
  bool is_output = false;

  int logical_timestamp = -1;

  // 枚举类型默认值
  DataType data_type = DataType::UNKNOWN;

  // *** 导致 Valgrind 报错的关键变量 ***
  int command = -2; // 之前是垃圾值，导致 if (command == -1) 失效
  PE_Role target_role = PE_Role::ROLE_UNUSED;
  int forward_count = 0;   // 之前是垃圾值，导致逻辑判断错误
  int current_forward = 0; // 之前是垃圾值
  // **********************************

  int hub_relay_node = -1;

  // 比较运算符保持不变
  inline bool operator==(const Flit &flit) const
  {
    return (
        flit.src_id == src_id && flit.flit_type == flit_type &&
        flit.vc_id == vc_id && flit.sequence_no == sequence_no &&
        flit.sequence_length == sequence_length && flit.payload == payload &&
        flit.logical_timestamp == logical_timestamp && flit.hop_no == hop_no &&
        flit.use_low_voltage_path == use_low_voltage_path);
  }
};

inline std::ostream &operator<<(std::ostream &os, const std::vector<int> &vec)
{
  os << "[";
  for (size_t i = 0; i < vec.size(); ++i)
  {
    if (i > 0)
      os << ", ";
    os << vec[i];
  }
  os << "]";
  return os;
}

enum PayloadIndex
{
  INPUT_IDX = 0,
  WEIGHT_IDX = 1,
  OUTPUT_IDX = 2
};

typedef struct
{
  string label;
  double value;
} PowerBreakdownEntry;

enum
{
  BUFFER_PUSH_PWR_D,
  BUFFER_POP_PWR_D,
  BUFFER_FRONT_PWR_D,
  BUFFER_TO_TILE_PUSH_PWR_D,
  BUFFER_TO_TILE_POP_PWR_D,
  BUFFER_TO_TILE_FRONT_PWR_D,
  BUFFER_FROM_TILE_PUSH_PWR_D,
  BUFFER_FROM_TILE_POP_PWR_D,
  BUFFER_FROM_TILE_FRONT_PWR_D,
  ANTENNA_BUFFER_PUSH_PWR_D,
  ANTENNA_BUFFER_POP_PWR_D,
  ANTENNA_BUFFER_FRONT_PWR_D,
  ROUTING_PWR_D,
  SELECTION_PWR_D,
  CROSSBAR_PWR_D,
  LINK_R2R_PWR_D,
  LINK_R2H_PWR_D,
  NI_PWR_D,
  WIRELESS_TX,
  WIRELESS_DYNAMIC_RX_PWR,
  WIRELESS_SNOOPING,
  NO_BREAKDOWN_ENTRIES_D
};

enum
{
  TRANSCEIVER_RX_PWR_BIASING,
  TRANSCEIVER_TX_PWR_BIASING,
  BUFFER_ROUTER_PWR_S,
  BUFFER_TO_TILE_PWR_S,
  BUFFER_FROM_TILE_PWR_S,
  ANTENNA_BUFFER_PWR_S,
  LINK_R2H_PWR_S,
  ROUTING_PWR_S,
  SELECTION_PWR_S,
  CROSSBAR_PWR_S,
  NI_PWR_S,
  TRANSCEIVER_RX_PWR_S,
  TRANSCEIVER_TX_PWR_S,
  NO_BREAKDOWN_ENTRIES_S
};

typedef struct
{
  int size;
  PowerBreakdownEntry
      breakdown[NO_BREAKDOWN_ENTRIES_D + NO_BREAKDOWN_ENTRIES_S];
} PowerBreakdown;

#endif
