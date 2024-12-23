# Router
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

