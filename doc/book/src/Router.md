# Router
`Router` 类是一个 SystemC 模块，实现了网络路由器的功能。主要包含以下部分：

## 模板和依赖
```cpp
#ifndef __NOXIMROUTER_H__
#define __NOXIMROUTER_H__

#include <systemc.h>
// ... 其他头文件包含
```
定义了所需的系统头文件和依赖项。

## 基本组件
路由器类包含了以下主要组件和功能：

- I/O 端口
- 内部寄存器和状态变量  
- 路由和选择策略
- 数据缓冲区
- 统计和功率管理

## 主要方法
主要的处理流程包括：

- `process()` - 主处理循环
- `rxProcess()` - 接收处理
- `txProcess()` - 发送处理 
- `route()` - 路由功能
- `configure()` - 配置路由器参数
- `perCycleUpdate()` - 每周期更新

## 友元类
```cpp
friend class Selection_NOP;
friend class Selection_BUFFER_LEVEL;
```
声明了两个选择策略的友元类。

## 工具函数
提供了一些辅助功能：

- NoP (Neighbor-on-Path) 数据处理
- 反向查找
- 性能统计
- 状态报告

## 构造函数
在构造函数中进行：

- 方法注册
- 敏感信号设置
- 路由算法初始化
- 选择策略配置

路由器实现了复杂的网络功能，包括数据包转发、流量控制和拥塞管理。
### 友元类声明
```cpp
friend class Selection_NOP;
friend class Selection_BUFFER_LEVEL;
```
这两行代码声明了 

Selection_NOP

 和 

Selection_BUFFER_LEVEL

 为 `Router` 类的友元类，使它们能够访问 `Router` 类的私有和保护成员。

### I/O 端口
```cpp
// I/O Ports
sc_in_clk clock;		                  // 路由器的输入时钟
sc_in<bool> reset;                           // 路由器的复位信号

// 端口数量：4 个网格方向 + 本地 + 无线
sc_in<Flit> flit_rx[DIRECTIONS + 2];	  // 输入通道
sc_in<bool> req_rx[DIRECTIONS + 2];	  // 输入通道的请求信号
sc_out<bool> ack_rx[DIRECTIONS + 2];	  // 输入通道的确认信号
sc_out<TBufferFullStatus> buffer_full_status_rx[DIRECTIONS + 2];

sc_out<Flit> flit_tx[DIRECTIONS + 2];   // 输出通道
sc_out<bool> req_tx[DIRECTIONS + 2];	  // 输出通道的请求信号
sc_in<bool> ack_tx[DIRECTIONS + 2];	  // 输出通道的确认信号
sc_in<TBufferFullStatus> buffer_full_status_tx[DIRECTIONS + 2];

sc_out<int> free_slots[DIRECTIONS + 1];
sc_in<int> free_slots_neighbor[DIRECTIONS + 1];
```
这些端口用于路由器与其他模块之间的数据传输和控制信号。

DIRECTIONS + 2

 表示 4 个网格方向加上本地和无线方向。

### Neighbor-on-Path 相关 I/O
```cpp
// Neighbor-on-Path related I/O
sc_out<NoP_data> NoP_data_out[DIRECTIONS];
sc_in<NoP_data> NoP_data_in[DIRECTIONS];
```
这些端口用于处理路径上的邻居数据，帮助路由器进行路径选择和流量控制。

### 寄存器和内部变量
```cpp
// Registers
int local_id;		                // 唯一 ID
int routing_type;		                // 路由算法类型
int selection_type;
BufferBank buffer[DIRECTIONS + 2];		// 缓冲区 [方向][虚拟通道]
bool current_level_rx[DIRECTIONS + 2];	// 交替位协议 (ABP) 的当前接收电平
bool current_level_tx[DIRECTIONS + 2];	// 交替位协议 (ABP) 的当前发送电平
Stats stats;		                // 统计信息
Power power;
LocalRoutingTable routing_table;		// 路由表
ReservationTable reservation_table;		// 交换机预留表
unsigned long routed_flits;
RoutingAlgorithm *routingAlgorithm; 
SelectionStrategy *selectionStrategy;
```
这些变量和对象用于存储路由器的状态和配置信息，包括路由算法、选择策略、缓冲区状态、统计信息和功率管理等。

通过这些配置，`Router` 类能够处理来自不同方向的输入和输出数据，并与其他模块进行交互，执行复杂的路由和流量控制任务。

这段代码定义了 

Router

 类的构造函数，使用 SystemC 宏 

SC_CTOR

 来初始化路由器模块。让我们逐步解释其主要部分：

### 方法注册
```cpp
SC_METHOD(process);
sensitive << reset;
sensitive << clock.pos();
SC_METHOD(perCycleUpdate);
sensitive << reset;
sensitive << clock.pos();
```
这两行代码使用 SC_METHOD宏注册了两个方法 process和 perCycleUpdate，并将它们设置为对 reset信号和时钟上升沿 (clock，pos()) 敏感。这意味着每当 reset信号变化或时钟上升沿到来时，这两个方法将被调用。

### 路由算法初始化
```cpp
routingAlgorithm = RoutingAlgorithms::get(GlobalParams::routing_algorithm);
if (routingAlgorithm == 0)
{
    cerr << " FATAL: invalid routing -routing " << GlobalParams::routing_algorithm << ", check with noxim -help" << endl;
    exit(-1);
}
```
这段代码从 `RoutingAlgorithms` 类中获取当前配置的路由算法，并将其赋值给 routingAlgorithm成员变量。如果获取失败（即返回 `0`），则输出错误信息并终止程序。这确保了路由器在初始化时使用有效的路由算法。

### 选择策略初始化
```cpp
selectionStrategy = SelectionStrategies::get(GlobalParams::selection_strategy);
if (selectionStrategy == 0)
{
    cerr << " FATAL: invalid selection strategy -sel " << GlobalParams::selection_strategy << ", check with noxim -help" << endl;
    exit(-1);
}
```
类似地，这段代码从 `SelectionStrategies` 类中获取当前配置的选择策略，并将其赋值给 selectionStrategy成员变量。如果获取失败（即返回 `0`），则输出错误信息并终止程序。这确保了路由器在初始化时使用有效的选择策略。


## rxProcess
这段代码定义了 `Router` 类中的 rxProcess方法，用于处理路由器的接收过程。让我们逐步解释其主要部分：

### 复位处理
```cpp
if (reset.read()) {
    TBufferFullStatus bfs;
    // 清除接收协议的输出和索引
    for (int i = 0; i < DIRECTIONS + 2; i++) {
        ack_rx[i].write(0);
        current_level_rx[i] = 0;
        buffer_full_status_rx[i].write(bfs);
    }
    routed_flits = 0;
    local_drained = 0;
}
```
当复位信号被激活时，rxProcess方法会清除所有接收协议的输出和索引：
- 将所有确认信号 ack_rx置为 0。
- 将当前接收电平 current_level_rx置为 0。
- 将缓冲区状态 buffer_full_status_rx清空。
- 重置已路由的 flit 数量 routed_flits 和本地排空的 flit 数量 local_drained。

### 正常接收处理
```cpp
else 
{ 
    // 该过程仅处理接收的 flit。所有仲裁和虫洞相关问题在 txProcess() 中处理
    for (int i = 0; i < DIRECTIONS + 2; i++) {
        // 接收新 flit 的条件：
        // 1) 有一个传入请求
        // 2) 方向 i 的输入缓冲区有空闲槽
        if (req_rx[i].read() == 1 - current_level_rx[i])
        { 
            Flit received_flit = flit_rx[i].read();
            int vc = received_flit.vc_id;
            if (!buffer[i][vc].IsFull()) 
            {
                // 将接收到的 flit 存储在循环缓冲区中
                buffer[i][vc].Push(received_flit);
                LOG << " Flit " << received_flit << " collected from Input[" << i << "][" << vc <<"]" << endl;
                power.bufferRouterPush();
                // 交替位协议 (ABP) 的电平切换
                current_level_rx[i] = 1 - current_level_rx[i];
                // 如果新 flit 是从本地 PE 注入的
                if (received_flit.src_id == local_id)
                    power.networkInterface();
            }
            else  // 缓冲区满
            {
                LOG << " Flit " << received_flit << " buffer full Input[" << i << "][" << vc <<"]" << endl;
                assert(i == DIRECTION_LOCAL);
            }
        }
        ack_rx[i].write(current_level_rx[i]);
        // 更新虚拟通道的掩码以防止满缓冲区接收数据
        TBufferFullStatus bfs;
        for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
            bfs.mask[vc] = buffer[i][vc].IsFull();
        buffer_full_status_rx[i].write(bfs);
    }
}
```
在正常操作模式下，rxProcess方法处理接收的 flit：
- 检查是否有传入请求，并且当前接收电平与请求信号相反。
- 如果满足条件，读取传入的 flit 并检查对应的虚拟通道缓冲区是否有空闲槽。
- 如果缓冲区不满，将 flit 存储在缓冲区中，并切换当前接收电平。
- 如果 flit 来自本地 PE，调用 power.networkInterface方法。
- 如果缓冲区已满，记录日志并断言该 flit 来自本地方向。
- 更新确认信号 ack_rx和缓冲区状态 buffer_full_status_rx。

## txProcess
这段代码是 `Router` 类中的 `txProcess` 方法的第一阶段：预留阶段。让我们逐步解释其主要。
- 更新确认信号 ack_rx和缓冲区状态 buffer_full_statu部分：

### 预留阶段概述
在预留阶段，路由器检查每个输入端口和虚拟通道中的 flit（流控制单元），并尝试为它们预留输出端口。预留阶段确保在实际数据传输之前，输出端口已经为特定的 flit 预留好。

### 遍历输入端口和虚拟通道
```cpp
for (int j = 0; j < DIRECTIONS + 2; j++) 
{
    int i = (start_from_port + j) % (DIRECTIONS + 2);
    for (int k = 0; k < GlobalParams::n_virtual_channels; k++)
    {
        int vc = (start_from_vc[i] + k) % (GlobalParams::n_virtual_channels);
```
- 外层循环遍历所有输入端口（包括 4 个方向、本地和无线方向）。
- 内层循环遍历每个输入端口的所有虚拟通道。
- 

start_from_port和 start_from_vc用于循环遍历端口和虚拟通道，以避免饥饿问题。

### 检查缓冲区和读取 flit
```cpp
if (!buffer[i][vc].IsEmpty()) 
{
    Flit flit = buffer[i][vc].Front();
    power.bufferRouterFront();
    if (flit.flit_type == FLIT_TYPE_HEAD) 
    {
        // prepare data for routing
        RouteData route_data;
        route_data.current_id = local_id;
        route_data.src_id = flit.src_id;
        route_data.dst_id = flit.dst_id;
        route_data.dir_in = i;
        route_data.vc_id = flit.vc_id;
    }
}
```
- 检查缓冲区是否为空，如果不为空，则读取缓冲区前端的 flit。
- 如果 flit 是头 flit（FLIT_TYPE_HEAD），则准备路由数据 
route_data，包括当前 ID、源 ID、目标 ID、输入方向和虚拟通道 ID。

### 路由计算和特殊情况处理
```cpp
int o = route(route_data);
if (o >= DIRECTION_HUB_RELAY)
{
    Flit f = buffer[i][vc].Pop();
    f.hub_relay_node = o - DIRECTION_HUB_RELAY;
    buffer[i][vc].Push(f);
    o = DIRECTION_HUB;
}
```
调用 route方法计算输出方向 o。
- 如果目标 Hub 不直接连接到目的地，处理特殊情况，将 flit 的 `hub_relay_node` 设置为中继节点，并将 flit 推回缓冲区。

### 预留输出端口
```cpp
TReservation r;
r.input = i;
r.vc = vc;
LOG << " checking availability of Output[" << o << "] for Input[" << i << "][" << vc << "] flit " << flit << endl;
int rt_status = reservation_table.checkReservation(r, o);
if (rt_status == RT_AVAILABLE) 
{
    LOG << " reserving direction " << o << " for flit " << flit << endl;
    reservation_table.reserve(r, o);
}
else if (rt_status == RT_ALREADY_SAME)
{
    LOG << " RT_ALREADY_SAME reserved direction " << o << " for flit " << flit << endl;
}
else if (rt_status == RT_OUTVC_BUSY)
{
    LOG << " RT_OUTVC_BUSY reservation direction " << o << " for flit " << flit << endl;
}
else if (rt_status == RT_ALREADY_OTHER_OUT)
{
    LOG << "RT_ALREADY_OTHER_OUT: another output previously reserved for the same flit " << endl;
}
else assert(false); // no meaningful status here
```
创建预留请求 r，包括输入端口和虚拟通道。
检查输出端口 o的可用性，并根据预留状态执行相应操作：

RT_AVAILABLE：预留成功。
RT_ALREADY_SAME：已经预留相同方向。
RT_OUTVC_BUSY：输出虚拟通道忙。
RT_ALREADY_OTHER_OUT：已经预留其他输出。

### 更新起始端口和虚拟通道
```cpp
start_from_vc[i] = (start_from_vc[i] + 1) % GlobalParams::n_virtual_channels;
start_from_port = (start_from_port + 1) % (DIRECTIONS + 2);
```
- 更新起始虚拟通道和起始端口，以便在下一个周期从不同的端口和虚拟通道开始预留。

### 转发阶段
```cpp
// 2nd phase: Forwarding
for (int i = 0; i < DIRECTIONS + 2; i++) 
{ 
    vector<pair<int,int> > reservations = reservation_table.getReservations(i);
    
    if (reservations.size()!=0)
    {
        int rnd_idx = rand()%reservations.size();
        int o = reservations[rnd_idx].first;
        int vc = reservations[rnd_idx].second;
        if (!buffer[i][vc].IsEmpty())  
        {
            Flit flit = buffer[i][vc].Front();
            if ( (current_level_tx[o] == ack_tx[o].read()) &&
                 (buffer_full_status_tx[o].read().mask[vc] == false) ) 
            {
                LOG << "Input[" << i << "][" << vc << "] forwarded to Output[" << o << "], flit: " << flit << endl;
                flit_tx[o].write(flit);
                current_level_tx[o] = 1 - current_level_tx[o];
                req_tx[o].write(current_level_tx[o]);
                buffer[i][vc].Pop();
                if (flit.flit_type == FLIT_TYPE_TAIL)
                {
                    TReservation r;
                    r.input = i;
                    r.vc = vc;
                    reservation_table.release(r,o);
                }
            }
        }
    }
}
```
在转发阶段，路由器将预留的 flit 从输入端口转发到输出端口：
- 遍历所有输入端口，获取预留的输出端口和虚拟通道。
- 随机选择一个预留的输出端口和虚拟通道。
- 检查缓冲区是否为空，如果不为空，则读取缓冲区前端的 flit。
- 检查当前发送电平和确认信号是否匹配，并且输出缓冲区状态是否未满。
- 如果满足条件，将 flit 写入输出端口，切换当前发送电平，并更新请求信号。
- 如果 flit 是尾 flit（FLIT_TYPE_TAIL），释放预留的输出端口和虚拟通道。

#### Power & Stats 处理

在这段代码中，路由器处理与功耗和统计相关的操作。具体步骤如下：

##### 功耗处理

```cpp
if (o == DIRECTION_HUB) power.r2hLink();
else power.r2rLink();

power.bufferRouterPop();
power.crossBar();
```

- 如果输出方向是 `DIRECTION_HUB`，调用 `power.r2hLink()` 记录从路由器到集线器的功耗。
- 否则，调用 `power.r2rLink()` 记录从路由器到路由器的功耗。
- 调用 `power.bufferRouterPop()` 记录从缓冲区弹出数据包的功耗。
- 调用 `power.crossBar()` 记录交叉开关的功耗。

##### 网络接口和统计处理

```cpp
if (o == DIRECTION_LOCAL) 
{
    power.networkInterface();
    LOG << "Consumed flit " << flit << endl;
    stats.receivedFlit(sc_time_stamp().to_double() / GlobalParams::clock_period_ps, flit);
    if (GlobalParams::max_volume_to_be_drained) 
    {
        if (drained_volume >= GlobalParams::max_volume_to_be_drained)
            sc_stop();
        else 
        {
            drained_volume++;
            local_drained++;
        }
    }
} 
else if (i != DIRECTION_LOCAL) // not generated locally
    routed_flits++;
```

- 如果输出方向是 `DIRECTION_LOCAL`，调用 `power.networkInterface()` 记录网络接口的功耗。
- 记录日志，输出已消耗的数据包（`flit`）。
- 调用 `stats.receivedFlit()` 记录接收到的数据包，传入当前时间戳和数据包。
- 如果 `GlobalParams::max_volume_to_be_drained` 被设置，检查是否达到最大排出量：
  - 如果达到最大排出量，调用 `sc_stop()` 停止仿真。
  - 否则，增加 `drained_volume` 和 `local_drained` 的计数。
- 如果输入方向不是 `DIRECTION_LOCAL`，增加 `routed_flits` 的计数。

##### 无法转发处理

```cpp
else
{
    LOG << " Cannot forward Input[" << i << "][" << vc << "] to Output[" << o << "], flit: " << flit << endl;
    LOG << " **DEBUG buffer_full_status_tx " << buffer_full_status_tx[o].read().mask[vc] << endl;
}
```

- 如果无法转发数据包，记录日志，输出无法转发的输入和输出端口信息以及当前的数据包（`flit`）。
- 记录日志，输出 `buffer_full_status_tx` 的状态。

##### 更新预留表索引

```cpp
if ((int)(sc_time_stamp().to_double() / GlobalParams::clock_period_ps) % 2 == 0)
    reservation_table.updateIndex();
```

- 每隔两个时钟周期，调用 `reservation_table.updateIndex()` 更新预留表的索引。


## perCycleUpdate
```cpp
void Router::perCycleUpdate()
{
    if (reset.read()) {
	for (int i = 0; i < DIRECTIONS + 1; i++)
	    free_slots[i].write(buffer[i][DEFAULT_VC].GetMaxBufferSize());
    } else {
        selectionStrategy->perCycleUpdate(this);

	power.leakageRouter();
	for (int i = 0; i < DIRECTIONS + 1; i++)
	{
	    for (int vc=0;vc<GlobalParams::n_virtual_channels;vc++)
	    {
		power.leakageBufferRouter();
		power.leakageLinkRouter2Router();
	    }
	}

	power.leakageLinkRouter2Hub();
    }
}
```
这段代码定义了 `Router` 类的成员函数 `perCycleUpdate`，该函数用于在每个周期更新路由器的状态。

首先，函数检查 `reset` 信号的状态。如果 `reset.read()` 返回 `true`，表示系统处于复位状态，此时函数会将所有方向的输入缓冲区的空闲槽位数设置为其最大值。具体来说，使用一个 `for` 循环遍历所有方向（由 `DIRECTIONS + 1` 定义），对于每个方向，调用 `buffer[i][DEFAULT_VC].GetMaxBufferSize()` 获取当前虚拟通道（`DEFAULT_VC`）的最大缓冲区大小，并将其写入到 `free_slots[i]`。

如果 `reset.read()` 返回 `false`，表示系统处于正常运行状态，此时函数会执行以下操作：

1. 调用 `selectionStrategy->perCycleUpdate(this)`，更新选择策略的状态。
2. 调用 `power.leakageRouter()`，计算路由器的漏电功耗。
3. 使用一个嵌套的 `for` 循环遍历所有方向和虚拟通道（由 `DIRECTIONS + 1` 和 `GlobalParams::n_virtual_channels` 定义），对于每个方向和虚拟通道，调用 `power.leakageBufferRouter()` 和 `power.leakageLinkRouter2Router()`，分别计算缓冲区和路由器之间链路的漏电功耗。
4. 调用 `power.leakageLinkRouter2Hub()`，计算路由器到集线器链路的漏电功耗。


## Confiugre
这个代码片段定义了一个名为 `Router::configure` 的函数，用于配置路由器的各种参数。该函数接受四个参数：路由器的 ID、预热时间、最大缓冲区大小和全局路由表。函数的主要任务是初始化路由器的内部状态和缓冲区，并根据给定的全局路由表配置路由表。

```cpp
void Router::configure(const int _id,
			    const double _warm_up_time,
			    const unsigned int _max_buffer_size,
			    GlobalRoutingTable & grt)
{
    // 设置路由器的本地 ID
    local_id = _id;
    
    // 配置统计信息
    stats.configure(_id, _warm_up_time);

    // 设置起始端口为本地方向
    start_from_port = DIRECTION_LOCAL;

    // 如果全局路由表有效，则配置路由表
    if (grt.isValid())
        routing_table.configure(grt, _id);

    // 设置预定表的大小
    reservation_table.setSize(DIRECTIONS + 2);

    // 初始化每个方向和虚拟通道的缓冲区
    for (int i = 0; i < DIRECTIONS + 2; i++)
    {
        for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
        {
            // 设置缓冲区的最大大小
            buffer[i][vc].SetMaxBufferSize(_max_buffer_size);
            
            // 设置缓冲区的标签
            buffer[i][vc].setLabel(string(name()) + "->buffer[" + i_to_string(i) + "]");
        }
        // 初始化每个方向的起始虚拟通道
        start_from_vc[i] = 0;
    }

    // 如果拓扑结构是网格，则禁用边界缓冲区
    if (GlobalParams::topology == TOPOLOGY_MESH)
    {
        int row = _id / GlobalParams::mesh_dim_x;
        int col = _id % GlobalParams::mesh_dim_x;

        for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
        {
            // 禁用北边界的缓冲区
            if (row == 0)
                buffer[DIRECTION_NORTH][vc].Disable();
            
            // 禁用南边界的缓冲区
            if (row == GlobalParams::mesh_dim_y - 1)
                buffer[DIRECTION_SOUTH][vc].Disable();
            
            // 禁用西边界的缓冲区
            if (col == 0)
                buffer[DIRECTION_WEST][vc].Disable();
            
            // 禁用东边界的缓冲区
            if (col == GlobalParams::mesh_dim_x - 1)
                buffer[DIRECTION_EAST][vc].Disable();
        }
    }
}
```

### 代码解释

1. **设置路由器的本地 ID 和统计信息**：
   - `local_id = _id;`：将传入的 ID 赋值给路由器的本地 ID。
   - `stats.configure(_id, _warm_up_time);`：配置统计信息，传入 ID 和预热时间。

2. **设置起始端口和配置路由表**：
   - `start_from_port = DIRECTION_LOCAL;`：设置起始端口为本地方向。
   - `if (grt.isValid()) routing_table.configure(grt, _id);`：如果全局路由表有效，则配置路由表。

3. **设置预定表的大小和初始化缓冲区**：
   - `reservation_table.setSize(DIRECTIONS + 2);`：设置预定表的大小。
   - 循环遍历每个方向和虚拟通道，设置缓冲区的最大大小和标签，并初始化起始虚拟通道。

4. **根据拓扑结构禁用边界缓冲区**：
   - 如果拓扑结构是网格，则根据路由器的位置（行和列）禁用北、南、西、东边界的缓冲区。

通过这些步骤，路由器的配置函数完成了对路由器内部状态和缓冲区的初始化和配置。

## route
```cpp
int Router::route(const RouteData & route_data)
{
    if (route_data.dst_id == local_id)
        return DIRECTION_LOCAL;

    power.routing();
    vector < int >candidate_channels = routingFunction(route_data);

    power.selection();
    return selectionFunction(candidate_channels, route_data);
}
```
`route` 方法仅仅只做能量计算，相当于一个入口函数而实际工作则交给`routing_function` 与`selection_fucntion`来执行：

1. **目的地检查**
   - 如果数据包的目的地是当前路由器，返回 `DIRECTION_LOCAL`

2. **路由计算**
   - 调用 `power.routing()` 记录路由计算的功耗
   - 调用 `routingFunction` 获取可能的候选通道

3. **选择计算**  
   - 调用 `power.selection()` 记录选择过程的功耗
   - 调用 `selectionFunction` 从候选通道中选择最终的输出方向

## selectionFunction
```cpp
int Router::selectionFunction(const vector < int >&directions,
				   const RouteData & route_data)
{
    // not so elegant but fast escape ;)
    if (directions.size() == 1)
	return directions[0];

    return selectionStrategy->apply(this, directions, route_data);
}
```
`selectionFunction` 方法接受两个参数：候选方向列表和路由数据。如果候选方向列表只有一个元素，直接返回该方向；否则，调用选择策略的 `apply` 方法进行选择。

## `vector < int >routingFunction(const RouteData & route_data);`
这个方法实现了路由器的路由功能，包括无线和有线两种模式。让我们详细分析其实现：

### 无线路由处理
```cpp
if (GlobalParams::use_winoc) 
{
    if (hasRadioHub(local_id))
    {
        // 检查目标是否直接连接到集线器
        if (hasRadioHub(route_data.dst_id) && 
            !sameRadioHub(local_id,route_data.dst_id))
        {
            // 如果目标和当前节点连接到不同的集线器
            if (connectedHubs(it1->second,it2->second))
            {
                vector<int> dirv;
                dirv.push_back(DIRECTION_HUB);
                return dirv;
            }
        }

        // 检查路由路径上的中继节点
        if (GlobalParams::winoc_dst_hops > 0)
        {
            vector<int> nexthops = nextDeltaHops(route_data);
            
            // 检查每个可能的中继节点
            for (int i=1; i<=GlobalParams::winoc_dst_hops; i++)
            {
                int candidate_hop = nexthops[nexthops.size()-1-i];
                if (hasRadioHub(candidate_hop) && 
                    !sameRadioHub(local_id,candidate_hop))
                {
                    vector<int> dirv;
                    dirv.push_back(DIRECTION_HUB_RELAY+candidate_hop);
                    return dirv;
                }
            }
        }
    }
}
```

### 有线路由处理
```cpp
// 如果不使用无线路由或无线路由失败，使用常规路由算法
return routingAlgorithm->route(this, route_data);
```

### 主要功能
1. **无线通信选择**：
   - 检查当前节点和目标是否连接到集线器
   - 支持通过中继节点进行无线通信
   - 使用距离阈值控制中继节点选择

2. **有线通信回退**：
   - 当无线通信不可用时，使用常规路由算法
   - 通过 `routingAlgorithm` 接口支持多种路由策略

3. **路由决策输出**：
   - 返回一个方向向量，指示数据包应该转发的方向
   - 支持直接无线传输和中继无线传输

## getCurrentNoPData
`getCurrentNoPData` 方法用于获取当前路由器的 NoP (Neighbor-on-Path) 数据，这些数据用于路由决策和流量控制。

### 实现细节
```cpp
NoP_data Router::getCurrentNoPData()
{
    NoP_data NoP_data;

    for (int j = 0; j < DIRECTIONS; j++) {
        try {
            NoP_data.channel_status_neighbor[j].free_slots = free_slots_neighbor[j].read();
            NoP_data.channel_status_neighbor[j].available = (reservation_table.isNotReserved(j));
        }
        catch (int e)
        {
            if (e!=NOT_VALID) assert(false);
            // Nothing to do if an NOT_VALID direction is caught
        };
    }

    NoP_data.sender_id = local_id;

    return NoP_data;
}
```

该方法执行以下操作：
1. **创建新的 NoP 数据结构**：创建一个 `NoP_data` 对象用于存储状态信息。

2. **遍历各个方向**：对每个方向（东南西北）：
   - 读取邻居的空闲槽位数量
   - 检查该方向是否已被预留

3. **错误处理**：使用 try-catch 处理无效方向的情况。

4. **设置发送者 ID**：将当前路由器的本地 ID 存储在 NoP 数据中。

### 注意事项
- 该方法主要用于拓扑和流量控制算法
- 异常处理仅忽略 `NOT_VALID` 错误
- 返回的数据包含通道状态和可用性信息

## NoPScore
`NoPScore` 方法用于计算给定方向上的 NoP (Neighbor-on-Path) 分数，这个分数用于评估路由决策的质量。

### 实现分析
```cpp
int Router::NoPScore(const NoP_data & nop_data,
              const vector < int >&nop_channels) const
{
    int score = 0;

    for (unsigned int i = 0; i < nop_channels.size(); i++) {
        int available;

        if (nop_data.channel_status_neighbor[nop_channels[i]].available)
            available = 1;
        else
            available = 0;

        int free_slots =
            nop_data.channel_status_neighbor[nop_channels[i]].free_slots;

        score += available * free_slots;
    }

    return score;
}
```

该方法执行以下操作：

1. **分数计算**：
   - 遍历所有给定的通道
   - 对每个通道检查其可用性（0 或 1）
   - 获取通道的空闲槽位数量
   - 将可用性与空闲槽位数相乘并累加到总分

2. **评分标准**：
   - 不可用通道得分为 0
   - 可用通道得分等于其空闲槽位数
   - 最终分数是所有通道分数的总和

### 使用场景
- 用于选择最佳路由路径
- 帮助实现负载均衡
- 支持自适应路由策略

## reflexDirection
```cpp
int Router::reflexDirection(int direction) const
{
    if (direction == DIRECTION_NORTH)
        return DIRECTION_SOUTH;
    if (direction == DIRECTION_EAST)
        return DIRECTION_WEST;
    if (direction == DIRECTION_WEST)
        return DIRECTION_EAST;
    if (direction == DIRECTION_SOUTH)
        return DIRECTION_NORTH;

    // you shouldn't be here 
    assert(false);
    return NOT_VALID;
}
```

`reflexDirection` 方法用于获取给定方向的相反方向。它执行以下操作：

1. **方向映射**：
   - 北方向返回南方向
   - 东方向返回西方向 
   - 西方向返回东方向
   - 南方向返回北方向

2. **错误处理**：
   - 如果输入无效方向，触发断言
   - 返回 `NOT_VALID` 表示错误

### 使用场景
- 用于计算反向路由路径
- 在双向通信中确定返回路径
- 支持路由算法的方向计算

## getNeighborId
```cpp
int Router::getNeighborId(int _id, int direction) const
{
    assert(GlobalParams::topology == TOPOLOGY_MESH);
    Coord my_coord = id2Coord(_id); 

    switch (direction) {
    case DIRECTION_NORTH:
        if (my_coord.y == 0) return NOT_VALID;
        my_coord.y--;
        break;
    case DIRECTION_SOUTH:
        if (my_coord.y == GlobalParams::mesh_dim_y - 1) return NOT_VALID;
        my_coord.y++;
        break;
    case DIRECTION_EAST:
        if (my_coord.x == GlobalParams::mesh_dim_x - 1) return NOT_VALID;
        my_coord.x++;
        break;
    case DIRECTION_WEST:
        if (my_coord.x == 0) return NOT_VALID;
        my_coord.x--;
        break;
    default:
        LOG << "Direction not valid : " << direction;
        assert(false);
    }

    return coord2Id(my_coord);
}
```

此方法用于计算网格拓扑中给定节点在指定方向上的邻居 ID。主要功能：

1. **拓扑检查**：
   - 确保当前拓扑是网格型
   - 将节点 ID 转换为坐标

2. **边界检查**：
   - 北向：检查是否在顶部边界
   - 南向：检查是否在底部边界
   - 东向：检查是否在右侧边界
   - 西向：检查是否在左侧边界

3. **坐标计算**：
   - 根据方向更新坐标
   - 将新坐标转换回节点 ID

### 使用场景
- 路由算法中确定下一跳
- 构建网络拓扑
- 计算邻居关系

## nextDeltaHops
```cpp
vector<int> Router::nextDeltaHops(RouteData rd)
```
该方法用于计算 Delta 网络（如 Omega、Butterfly、Baseline）中从源节点到目标节点的所有跳转节点序列。

### 主要流程

1. **拓扑检查**
```cpp
if (GlobalParams::topology == TOPOLOGY_MESH) {
    cout << "Mesh topologies are not supported for nextDeltaHops() ";
    assert(false);
}
```
确保不是网格拓扑，因为该方法仅支持 Delta 网络。

2. **初始化**
```cpp
int src = rd.src_id;
int dst = rd.dst_id;
int current_node = src;
vector<int> direction;
vector<int> next_hops;
```
设置初始参数和结果向量。

3. **第一阶段路由**
```cpp
int sw = GlobalParams::n_delta_tiles/2;
int stg = log2(GlobalParams::n_delta_tiles);
```
计算每阶段的交换机数量和总阶段数。

根据不同拓扑计算第一跳：
- Omega 网络：根据源节点位置计算
- Butterfly/Baseline：使用右移操作计算

4. **阶段间路由**
```cpp
while (current_stage < stg-1) {
    // 获取当前位置
    // 计算路由方向
    // 根据位检查结果更新坐标
    // 添加下一跳
}
```
- 在每个阶段计算下一个节点
- 使用位操作确定路由方向
- 维护节点序列

### 返回值
返回包含从源到目的地的所有中间节点 ID 的向量，包括源和目的节点。

### 使用场景
- 在 Delta 网络中规划路由路径
- 支持多跳无线通信
- 辅助路由决策

## inCongestion
```cpp
bool Router::inCongestion()
{
    for (int i = 0; i < DIRECTIONS; i++) {
        if (free_slots_neighbor[i]==NOT_VALID) continue;

        int flits = GlobalParams::buffer_depth - free_slots_neighbor[i];
        if (flits > (int) (GlobalParams::buffer_depth * GlobalParams::dyad_threshold))
            return true;
    }

    return false;
}
```

`inCongestion` 方法检查路由器是否处于拥塞状态。具体执行以下操作：

1. **遍历所有方向**：
   - 检查每个方向的邻居节点
   - 跳过无效方向

2. **拥塞判断**：
   - 计算已使用的缓冲区槽位数量
   - 将已用槽位与阈值比较
   - 阈值由缓冲区深度和动态阈值参数决定

3. **返回值**：
   - 任一方向超过阈值返回 true
   - 所有方向都在阈值内返回 false

此方法用于：
- 动态路由决策
- 负载均衡
- 流量控制

## ShowBuffersStats
```cpp
void Router::ShowBuffersStats(std::ostream & out)
{
    for (int i=0; i<DIRECTIONS+2; i++)
            for (int vc=0; vc<GlobalParams::n_virtual_channels;vc++)
                buffer[i][vc].ShowStats(out);
}
```

`ShowBuffersStats` 方法用于输出路由器所有缓冲区的统计信息：

1. **遍历缓冲区**：
     - 外层循环遍历所有方向（DIRECTIONS+2表示4个方向加本地和无线）
     - 内层循环遍历每个方向的所有虚拟通道

2. **统计输出**：
     - 调用每个缓冲区的ShowStats方法
     - 将统计信息写入指定的输出流

使用场景：
- 性能分析和调试
- 监控缓冲区使用情况
- 生成统计报告

## connectedHubs
```cpp
bool Router::connectedHubs(int src_hub, int dst_hub)
```

该方法检查两个集线器是否通过共同的通道相连。具体实现如下：

1. **获取通道列表**：
    - 获取源集线器的发送通道列表
    - 获取目标集线器的接收通道列表

2. **查找共同通道**：
    - 遍历源集线器的发送通道
    - 对每个发送通道，检查是否存在于目标集线器的接收通道中
    - 将找到的共同通道保存到交集列表中

3. **连接判断**：
    - 如果交集列表为空，返回 false（不相连）
    - 如果交集列表不为空，返回 true（相连）

使用场景：
- 验证无线通信可行性
- 路由决策时的连接性检查
- 网络拓扑分析