# Datastructs
## DataStructs.h 数据结构介绍

该文件定义了 Noxim 模拟器中使用的一些关键数据结构，用于表示网络中的各种元素和信息。

**1. `Coord`**

*   表示 Tile 在 Mesh 网络中的 XY 坐标。
*   `x`：X 坐标。
*   `y`：Y 坐标。
*   重载了 `==` 运算符，用于比较两个 `Coord` 对象是否相等。

**2. `FlitType`**

*   枚举类型，表示 Flit 的类型。
*   `FLIT_TYPE_HEAD`：Head Flit，表示数据包的头部。
*   `FLIT_TYPE_BODY`：Body Flit，表示数据包的主体。
*   `FLIT_TYPE_TAIL`：Tail Flit，表示数据包的尾部。

**3. `Payload`**

*   表示 Flit 的有效载荷。
*   `data`：`sc_uint<32>` 类型，用于存储要交换的数据。
*   重载了 `==` 运算符，用于比较两个 `Payload` 对象是否相等。

**4. `Packet`**

*   表示一个数据包。
*   `src_id`：源节点 ID。
*   `dst_id`：目标节点 ID。
*   `vc_id`：虚拟通道 ID。
*   `timestamp`：数据包生成的时间戳。
*   `size`：数据包的大小（Flit 数量）。
*   `flit_left`：数据包中剩余的 Flit 数量。
*   `use_low_voltage_path`：是否使用低电压路径。
*   `make()`：构造函数，用于初始化 `Packet` 对象。

**5. `RouteData`**

*   表示路由所需的数据。
*   `current_id`：当前节点 ID。
*   `src_id`：源节点 ID。
*   `dst_id`：目标节点 ID。
*   `dir_in`：数据包进入路由器的方向。
*   `vc_id`：虚拟通道 ID。

**6. `ChannelStatus`**

*   表示通道的状态。
*   `free_slots`：空闲的缓冲区槽位数。
*   `available`：通道是否可用。
*   重载了 `==` 运算符，用于比较两个 `ChannelStatus` 对象是否相等。

**7. `NoP_data`**

*   表示 NoP（No Operation）数据，用于流量控制。
*   `sender_id`：发送者 ID。
*   `channel_status_neighbor`：相邻通道的状态数组。
*   重载了 `==` 运算符，用于比较两个 `NoP_data` 对象是否相等。

**8. `TBufferFullStatus`**

*   表示缓冲区满状态。
*   `mask`：布尔数组，用于指示每个虚拟通道是否已满。
*   重载了 `==` 运算符，用于比较两个 `TBufferFullStatus` 对象是否相等。

**9. `Flit`**

*   表示一个 Flit（Flow control unit）。
*   `src_id`：源节点 ID。
*   `dst_id`：目标节点 ID。
*   `vc_id`：虚拟通道 ID。
*   `flit_type`：Flit 类型（`FLIT_TYPE_HEAD`、`FLIT_TYPE_BODY`、`FLIT_TYPE_TAIL`）。
*   `sequence_no`：Flit 在数据包中的序列号。
*   `sequence_length`：数据包的总长度。
*   `payload`：有效载荷（`Payload` 类型）。
*   `timestamp`：数据包生成的时间戳。
*   `hop_no`：从源到目标的跳数。
*   `use_low_voltage_path`：是否使用低电压路径。
*   `hub_relay_node`：Hub 中继节点。
*   重载了 `==` 运算符，用于比较两个 `Flit` 对象是否相等。

**10. `PowerBreakdownEntry`**

*   表示功耗分解条目。
*   `label`：标签，描述功耗类型。
*   `value`：功耗值。

**11. 枚举类型**

*   定义了多个枚举类型，用于表示不同的功耗分解条目。

**12. `PowerBreakdown`**

*   表示功耗分解信息。
*   `size`：分解条目的数量。
*   `breakdown`：`PowerBreakdownEntry` 数组，存储功耗分解条目。

这些数据结构在 Noxim 模拟器中被广泛使用，用于表示网络拓扑、数据包信息、路由信息、通道状态以及功耗信息等。