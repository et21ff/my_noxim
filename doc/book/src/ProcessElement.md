# 处理单元 (Processing Element, PE)

# 处理单元 (Processing Element, PE)

处理单元是片上网络(NoC)中的一个基本组件,负责数据包的生成、传输和接收。它被实现为一个具有特定网络通信功能的SystemC模块。

## 结构

PE模块包含以下部分:

### 输入/输出端口
- `clock`: 输入时钟信号
- `reset`: 复位信号
- 输入通道信号:
    - `flit_rx`: 输入数据片
    - `req_rx`: 请求信号
    - `ack_rx`: 确认信号
    - `buffer_full_status_rx`: 缓冲区状态
- 输出通道信号:
    - `flit_tx`: 输出数据片
    - `req_tx`: 请求信号
    - `ack_tx`: 确认信号
    - `buffer_full_status_tx`: 缓冲区状态

### 内部组件
- 本地ID,用于唯一标识
- ABP(交替位协议)级别,用于传输控制
- 数据包队列,用于存储消息
- 流量管理功能

## 流量模式

PE支持多种流量分布模式:
- 随机分布
- 转置(1 & 2)
- 位反转
- 洗牌
- 蝶形
- 局部/统一局部(具有局部性)

## 主要进程

两个主要进程处理PE的操作:
1. `rxProcess`: 处理输入数据片
2. `txProcess`: 管理输出传输

## 实现代码说明

### txProcess
```cpp
// 传输进程
void ProcessingElement::txProcess() 
{
    if (reset.read()) {
        req_tx.write(0);                    // 将请求信号置为0
        current_level_tx = 0;               // 重置当前传输级别
        transmittedAtPreviousCycle = false; // 标记上一个周期没有传输
    } else {
        Packet packet;                      // 创建一个新的数据包对象

        // 检查是否可以生成新的数据包
        if (canShot(packet)) {
            packet_queue.push(packet);      // 将数据包推入队列
            transmittedAtPreviousCycle = true;
        } else {
            transmittedAtPreviousCycle = false;
        }

        // 检查确认信号是否与当前传输级别匹配
        if (ack_tx.read() == current_level_tx) {
            if (!packet_queue.empty()) {
        } else {
            transmittedAtPreviousCycle = false; // 标记上一个周期没有传输
        }

        // 检查确认信号是否与当前传输级别匹配
        if (ack_tx.read() == current_level_tx) {
            // 如果数据包队列不为空
            if (!packet_queue.empty()) {
                Flit flit = nextFlit(); // 生成一个新的flit
                flit_tx->write(flit); // 发送生成的flit
                current_level_tx = 1 - current_level_tx; // 切换当前传输级别（用于交替位协议）
                req_tx.write(current_level_tx); // 更新请求信号
            }
        }
    }
}
```


### 代码解释

1. **复位处理**：
    - 如果 `reset` 信号被激活，重置 `req_tx` 信号、`current_level_tx` 和 `transmittedAtPreviousCycle` 变量。

2. **数据包生成**：
    - 创建一个新的 `Packet` 对象。但是数据包的具体生成逻辑在 `canShot(packet)` 函数中实现。
    - 使用 `canShot(packet)` 函数检查是否可以生成新的数据包。如果可以，将数据包推入 `packet_queue` 队列，并标记 `transmittedAtPreviousCycle` 为 `true`。否则，标记为 `false`。

3. **传输处理**：
    - 检查 `ack_tx` 信号是否与 `current_level_tx` 匹配。
    - 如果匹配并且 `packet_queue` 不为空，生成一个新的 `Flit` 对象，并通过 `flit_tx` 信号发送。然后切换 `current_level_tx` 的值（用于交替位协议），并更新 `req_tx` 信号。

这样，代码实现了一个简单的传输处理逻辑，结合了复位处理、数据包生成和传输处理。

## canShot 函数

```cpp
bool ProcessingElement::canShot(Packet & packet)
{
    if (never_transmit) return false;

    #ifdef DEADLOCK_AVOIDANCE
    if (local_id % 2 == 0)
        return false;
    #endif

    bool shot;
    double threshold;
    double now = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;

    if (GlobalParams::traffic_distribution != TRAFFIC_TABLE_BASED) {
        if (!transmittedAtPreviousCycle)
            threshold = GlobalParams::packet_injection_rate;
        else
            threshold = GlobalParams::probability_of_retransmission;

        shot = (((double) rand()) / RAND_MAX < threshold);
        if (shot) {
            switch (GlobalParams::traffic_distribution) {
                case TRAFFIC_RANDOM:
                    packet = trafficRandom();
                    break;
                case TRAFFIC_TRANSPOSE1:
                    packet = trafficTranspose1();
                    break;
                case TRAFFIC_TRANSPOSE2:
                    packet = trafficTranspose2();
                    break;
                case TRAFFIC_BIT_REVERSAL:
                    packet = trafficBitReversal();
                    break;
                case TRAFFIC_SHUFFLE:
                    packet = trafficShuffle();
                    break;
                case TRAFFIC_BUTTERFLY:
                    packet = trafficButterfly();
                    break;
                case TRAFFIC_LOCAL:
                    packet = trafficLocal();
                    break;
                case TRAFFIC_ULOCAL:
                    packet = trafficULocal();
                    break;
                default:
                    cout << "Invalid traffic distribution: " << GlobalParams::traffic_distribution << endl;
                    exit(-1);
            }
        }
    } else {
        if (never_transmit)
            return false;

        bool use_pir = (transmittedAtPreviousCycle == false);
        vector<pair<int, double>> dst_prob;
        double threshold = traffic_table->getCumulativePirPor(local_id, (int) now, use_pir, dst_prob);

        double prob = (double) rand() / RAND_MAX;
        shot = (prob < threshold);
        if (shot) {
            for (unsigned int i = 0; i < dst_prob.size(); i++) {
                if (prob < dst_prob[i].second) {
                    int vc = randInt(0, GlobalParams::n_virtual_channels - 1);
                    packet.make(local_id, dst_prob[i].first, vc, now, getRandomSize());
                    break;
                }
            }
        }
    }

    return shot;
}
```

### 代码解释

1. **基本检查**：
    - 如果 `never_transmit` 为真，函数直接返回 `false`。
    - 如果定义了 `DEADLOCK_AVOIDANCE` 且 `local_id` 为偶数，函数返回 `false`。

2. **流量分布处理**：
    - 如果 `GlobalParams::traffic_distribution` 不是基于表的流量分布：
        - 根据 `transmittedAtPreviousCycle` 设置 `threshold`。
        - 生成一个随机数并与 `threshold` 比较，决定是否生成数据包。
        - 根据不同的流量分布模式生成相应的数据包。
    - 如果是基于表的流量分布：
        - 获取当前时间和 `threshold`。
        - 生成一个随机数并与 `threshold` 比较，决定是否生成数据包。
        - 根据概率选择目标，并生成数据包。

3. **返回值**：
    - 返回 `shot`，表示是否生成了数据包。

这样，`canShot` 函数实现了根据不同的流量分布模式和条件生成数据包的逻辑。

## trafficLocal 函数

```cpp
```cpp
Packet ProcessingElement::trafficLocal()
{
    Packet p;
    p.src_id = local_id;                    // 设置数据包的源ID为本地ID
    double rnd = rand() / (double) RAND_MAX; // 生成一个随机概率值

    vector<int> dst_set;                     // 创建目标ID集合

    // 计算网格中的最大节点ID
    int max_id = (GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y);

    // 遍历所有可能的目标ID
    for (int i = 0; i < max_id; i++)
    {
        if (rnd <= GlobalParams::locality)   // 如果随机值小于等于局部性参数
        {
            // 如果目标不是自己且在同一无线电集线器范围内，加入目标集合
            if (local_id != i && sameRadioHub(local_id, i))
                dst_set.push_back(i);
        }
        else                                 // 如果随机值大于局部性参数
        {
            // 如果不在同一无线电集线器范围内，加入目标集合
            if (!sameRadioHub(local_id, i))
                dst_set.push_back(i);
        }
    }

    // 从目标集合中随机选择一个目标ID
    int i_rnd = rand() % dst_set.size();

    p.dst_id = dst_set[i_rnd];              // 设置数据包的目标ID
    // 设置数据包的时间戳（以时钟周期为单位）
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize(); // 设置数据包的大小和剩余flit数
    // 随机选择一个虚拟通道ID
    p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);

    return p;                               // 返回生成的数据包
}
```

### 代码解释

1. **初始化**：
    - 创建一个 `Packet` 对象 `p` 并设置其源ID为 `local_id`。
    - 生成一个随机数 `rnd`。

2. **目标集合生成**：
    - 遍历所有可能的目标ID。
    - 根据 `rnd` 和 `GlobalParams::locality`，决定是否将目标ID加入 `dst_set`。
    - 如果 `rnd` 小于等于 `GlobalParams::locality`，则将与 `local_id` 在同一无线电集线器的目标ID加入集合。
    - 否则，将不在同一无线电集线器的目标ID加入集合。

3. **选择目标**：
    - 从 `dst_set` 中随机选择一个目标ID，并设置为数据包的目标ID `dst_id`。

4. **设置数据包属性**：
    - 设置数据包的时间戳、大小和虚拟通道ID。

5. **返回数据包**：
    - 返回生成的 `Packet` 对象 `p`。

这样，`trafficLocal` 函数实现了根据局部性生成目标ID并创建数据包的逻辑。

## findRandomDestination 函数

```cpp
int ProcessingElement::findRandomDestination(int id, int hops)
{
    assert(GlobalParams::topology == TOPOLOGY_MESH);

    int inc_y = rand()%2?-1:1;  // 随机选择Y方向增量(+1或-1)
    int inc_x = rand()%2?-1:1;  // 随机选择X方向增量(+1或-1)
    
    Coord current = id2Coord(id); // 将ID转换为网格坐标
    
    // 在网格中随机行走hops步
    for (int h = 0; h<hops; h++) {
        // 边界检查和调整
        if (current.x==0 && inc_x<0) inc_x=0;
        if (current.x==GlobalParams::mesh_dim_x-1 && inc_x>0) inc_x=0;
        if (current.y==0 && inc_y<0) inc_y=0;
        if (current.y==GlobalParams::mesh_dim_y-1 && inc_y>0) inc_y=0;

        // 随机选择X或Y方向移动
        if (rand()%2)
            current.x += inc_x;
        else
            current.y += inc_y;
    }

    return coord2Id(current); // 将最终坐标转回ID
}
```

### 代码解释

1. **初始化**：
   - 确保拓扑类型是网格(MESH)
   - 随机生成X和Y方向的移动增量(+1或-1)
   - 将输入的ID转换为网格坐标

2. **随机行走**：
   - 在网格中进行指定步数(hops)的随机行走
   - 检查并处理网格边界情况
   - 随机选择X或Y方向进行移动

3. **返回结果**：
   - 将最终坐标转换回ID并返回

## roulette 函数

```cpp
int ProcessingElement::roulette()
{
    // 计算轮盘赌的切片数（网格的X维度加Y维度减2）
    int slices = GlobalParams::mesh_dim_x + GlobalParams::mesh_dim_y -2;

    // 生成0到1之间的随机数
    double r = rand()/(double)RAND_MAX;

    // 根据概率分布选择跳数
    for (int i=1;i<=slices;i++)
    {
        if (r< (1-1/double(2<<i)))
        {
            return i;
        }
    }
    
    assert(false);  // 不应该到达这里
    return 1;
}
```

### 代码解释

1. **切片计算**：
   - 根据网格维度计算轮盘赌的切片数

2. **概率生成**：
   - 生成一个0到1之间的随机数

3. **概率选择**：
   - 使用指数递减的概率分布选择跳数
   - 返回选中的跳数值

## trafficULocal 函数

```cpp
Packet ProcessingElement::trafficULocal()
{
    Packet p;
    p.src_id = local_id;                    // 设置数据包的源ID为本地ID

    int target_hops = roulette();           // 使用轮盘赌算法决定目标跳数

    // 找到指定跳数范围内的随机目标
    p.dst_id = findRandomDestination(local_id, target_hops);

    // 设置数据包属性
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize(); // 设置数据包大小
    p.vc_id = randInt(0, GlobalParams::n_virtual_channels-1); // 设置虚拟通道ID

    return p;                               // 返回生成的数据包
}
```

### 代码解释

1. **初始化**：
   - 创建数据包并设置源ID
   - 使用轮盘赌算法决定目标距离

2. **目标选择**：
   - 使用findRandomDestination函数在指定跳数范围内找到随机目标

3. **数据包配置**：
   - 设置时间戳、大小和虚拟通道
   - 返回配置完成的数据包

## trafficRandom 函数

```cpp
Packet ProcessingElement::trafficRandom()
{
    Packet p;
    p.src_id = local_id;                    // 设置数据包的源ID
    double rnd = rand() / (double) RAND_MAX; // 生成随机数
    double range_start = 0.0;
    int max_id;                             // 最大目标ID

    // 根据网络拓扑设置最大ID
    if (GlobalParams::topology == TOPOLOGY_MESH)
        max_id = (GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y) - 1;
    else    // 其他Delta拓扑
        max_id = GlobalParams::n_delta_tiles - 1;

    // 生成随机目标ID
    do {
        p.dst_id = randInt(0, max_id);      // 随机选择目标ID

        // 检查热点目标
        for (size_t i = 0; i < GlobalParams::hotspots.size(); i++) {
            if (rnd >= range_start && rnd < range_start + GlobalParams::hotspots[i].second) {
                if (local_id != GlobalParams::hotspots[i].first) {
                    p.dst_id = GlobalParams::hotspots[i].first;
                }
                break;
            }
            range_start += GlobalParams::hotspots[i].second;
        }

        #ifdef DEADLOCK_AVOIDANCE
        // 死锁避免处理
        if (p.dst_id % 2 != 0) {
            p.dst_id = (p.dst_id + 1) % 256;
        }
        #endif
    } while (p.dst_id == p.src_id);         // 确保目标ID不等于源ID

    // 设置数据包属性
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize(); // 设置数据包大小
    p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);

    return p;                               // 返回生成的数据包
}
```

### 代码解释

1. **初始化**：
   - 创建数据包并设置源ID
   - 生成随机数和设置网络最大ID

2. **目标选择**：
   - 随机选择目标ID
   - 处理热点目标分布
   - 实现死锁避免机制

3. **数据包配置**：
   - 设置时间戳、大小和虚拟通道
   - 确保目标不是源节点

该函数实现了随机流量模式的数据包生成，支持热点流量和死锁避免。