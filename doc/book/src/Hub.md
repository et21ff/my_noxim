# Hub 类详细分析

这是一个用于网络芯片(NoC)中集线器(Hub)功能实现的类定义。让我们分析其主要组成部分：

## 基本结构和接口

### 系统接口
```cpp
SC_MODULE(Hub) {
    sc_in_clk clock;      // 时钟输入
    sc_in<bool> reset;    // 复位信号
    int local_id;         // 集线器唯一标识
}
```

### 通信端口
```cpp
sc_in<Flit>* flit_rx;    // 数据包接收端口
sc_out<Flit>* flit_tx;   // 数据包发送端口
sc_in<bool>* req_rx;     // 请求接收信号
sc_out<bool>* req_tx;    // 请求发送信号
```

## 主要功能组件

### 缓冲区管理
```cpp
BufferBank* buffer_from_tile;   // 从瓦片接收数据的缓冲区
BufferBank* buffer_to_tile;     // 向瓦片发送数据的缓冲区
```

### 令牌环控制
```cpp
map<int, sc_in<int>* > current_token_holder;      // 当前令牌持有者
map<int, sc_in<int>* > current_token_expiration;  // 令牌过期时间
map<int, sc_inout<int>* > flag;                   // 状态标志
```

## 构造函数实现

构造函数主要完成以下初始化工作：

1. 注册处理方法：
```cpp
if (GlobalParams::use_winoc) {
    SC_METHOD(tileToAntennaProcess);
    SC_METHOD(antennaToTileProcess);
}
```

2. 配置基本参数：
```cpp
local_id = id;
num_ports = GlobalParams::hub_configuration[local_id].attachedNodes.size();
```

3. 初始化通信通道：
```cpp
flit_rx = new sc_in<Flit>[num_ports];
flit_tx = new sc_out<Flit>[num_ports];
```
## 函数
### `int Hub::route(Flit& f)`
这段代码实现了 Hub 类中的路由决策方法，用于确定数据包（Flit）的下一跳方向。
```cpp
int Hub::route(Flit& f)
{
	// 本地连接到 Hub 的节点 ID
	for (vector<int>::size_type i=0; i< GlobalParams::hub_configuration[local_id].attachedNodes.size();i++)
	{
		// ...to a destination which is connected to the Hub
		if (GlobalParams::hub_configuration[local_id].attachedNodes[i]==f.dst_id)
		{
			return tile2Port(f.dst_id);
		}
		//到连接到目的节点并与hub相连的一个节点
		if (GlobalParams::hub_configuration[local_id].attachedNodes[i]==f.hub_relay_node)
		{
			assert(GlobalParams::winoc_dst_hops>0);
			return tile2Port(f.hub_relay_node);
		}

	}
	return DIRECTION_WIRELESS;
//两者条件都不满足，表示需要无线路由
}
```

### `Hub::rxPowerManager`
这是 Hub 类中的接收功率管理方法，用于控制和统计接收相关的功耗。
```cpp
void Hub::rxPowerManager()
{
    // Check wheter accounting or not buffer to tile leakage
    // For each port, two poweroff condition should be checked:
    // - the buffer to tile is empty
    // - it has not been reserved
    // currently only supported without VC
    assert(GlobalParams::n_virtual_channels==1);
    for (int port=0;port<num_ports;port++)
    {
        if (!buffer_to_tile[port][DEFAULT_VC].IsEmpty() ||
            antenna2tile_reservation_table.isNotReserved(port))//BUFFER非空或者端口未被预留
            power.leakageBufferToTile();
        else
            buffer_to_tile_poweroff_cycles[port]++; //端口休眠cycle++
    }
    for (unsigned int i=0;i<rxChannels.size();i++)
    {
        int ch_id = rxChannels[i];
        if (!target[ch_id]->buffer_rx.IsEmpty()) //检查天线接收缓冲区是否为空
        {
            power.leakageAntennaBuffer();
        }
        else
            buffer_rx_sleep_cycles[ch_id]++;
    } 
    // Check wheter accounting antenna RX buffer
    // check if there is at least one not empty antenna RX buffer
    // To be only applied if the current hub is in RADIO_EVENT_SLEEP_ON mode
    if (power.isSleeping())
        total_sleep_cycles++;
    else // not sleeping
    {
        power.wirelessSnooping();
        power.leakageTransceiverRx();
        power.biasingRx();
    }
}
```
 ### `void Hub::updateRxPower()`
 ```cpp
 void Hub::updateRxPower()
{
    // 1. 检查是否使用功率管理器
    if (GlobalParams::use_powermanager)
        // 2. 如果使用，调用 rxPowerManager 进行详细的功率管理
        rxPowerManager();
    else
    {
        // 3. 如果不使用，执行基本的功率计算
        power.wirelessSnooping();       // 无线监听功耗
        power.leakageTransceiverRx();   // 接收器泄漏功耗
        power.biasingRx();              // 接收器偏置功耗

        // 4. 遍历所有接收通道和虚拟通道，计算天线缓冲区泄漏功耗
        for (unsigned int i=0; i<rxChannels.size(); i++)
            for (int vc=0; vc<GlobalParams::n_virtual_channels; vc++)
                power.leakageAntennaBuffer();

        // 5. 遍历所有端口和虚拟通道，计算 Tile 缓冲区泄漏功耗
        for (int i = 0; i < num_ports; i++)
            for (int vc=0; vc<GlobalParams::n_virtual_channels; vc++)
                power.leakageBufferToTile();
    }
}
 ```
### `void Hub::txPowerManager()`
```cpp
void Hub::txPowerManager()
{
    // 1. 遍历所有发送通道
    for (unsigned int i = 0; i < txChannels.size(); i++)
    {
        // 2. 检查通道是否为空闲或未被预留
        if (!init[i]->buffer_tx.IsEmpty() ||
            tile2antenna_reservation_table.isNotReserved(i))
        {
            // 3. 如果通道活动，则计算天线缓冲区泄漏功耗
            power.leakageAntennaBuffer();

            // 4. 检查 Hub 是否处于休眠状态
            if (power.isSleeping())
            {
                // 5. 如果处于休眠状态，则增加模拟发送关闭周期计数
                analogtxoff_cycles[i]++;
            }
            else
            {
                // 6. 如果未处于休眠状态，则计算发送器泄漏功耗和偏置功耗
                power.leakageTransceiverTx();
                power.biasingTx();
            }
        }
        else
        {
            // 7. 如果通道空闲且未被预留，则增加各种关闭周期计数
            abtxoff_cycles[i]++;        // 天线缓冲区关闭周期
            analogtxoff_cycles[i]++;    // 模拟发送关闭周期
            total_ttxoff_cycles++;       // 总发送关闭周期
        }
    }
}
```

### `void Hub::updateTxPower()`
```cpp
void Hub::updateTxPower()
{
    // 1. 检查是否使用功率管理器
    if (GlobalParams::use_powermanager)
        // 2. 如果使用，调用 txPowerManager 进行详细的功率管理
        txPowerManager();
    else
    {
        // 3. 如果不使用，则执行基本的功率计算
        for (unsigned int i=0; i<txChannels.size(); i++)
            for (int vc=0; vc<GlobalParams::n_virtual_channels; vc++)
                power.leakageAntennaBuffer();  // 计算天线缓冲区泄漏功耗

        power.leakageTransceiverTx();      // 计算发送器泄漏功耗
        power.biasingTx();                 // 计算发送器偏置功耗
    }

    // 4. 强制执行的功率计算
    power.leakageLinkRouter2Hub();         // 计算 Router 到 Hub 链路的泄漏功耗
    for (int i = 0; i < num_ports; i++)
        for (int vc=0; vc<GlobalParams::n_virtual_channels; vc++)
            power.leakageBufferFromTile();   // 计算 Tile 到 Hub 缓冲区的泄漏功耗
}
```
### Hub::txRadioProcessTokenPacket 

这是 Hub 类中处理令牌包传输的方法。让我们详细分析其实现和功能。

#### 核心功能

#### 1. 状态检查
```cpp
int current_holder = current_token_holder[channel]->read();
int current_channel_flag = flag[channel]->read();
```
- 获取当前令牌持有者
- 获取当前通道标志状态

#### 2. 处理逻辑
当本地节点持有令牌且通道未释放时，进行以下处理：

##### 发送缓冲区非空时：
```cpp
if (!init[channel]->buffer_tx.IsEmpty()) {
    Flit flit = init[channel]->buffer_tx.Front();
    init[channel]->start_request_event.notify();
}
```
- 获取待发送的数据包
- 触发传输请求事件

##### 发送缓冲区为空时：
- 如果没有正在进行的传输，释放令牌
- 如果有传输正在进行，保持令牌

```cpp
void Hub::txRadioProcessTokenPacket(int channel)
{
    int current_holder = current_token_holder[channel]->read();//获取当前令牌持有者
    int current_channel_flag =flag[channel]->read();//获取当前通道标志状态
    if ( current_holder == local_id && current_channel_flag !=RELEASE_CHANNEL) //本地节点持有令牌且通道未释放
    {
        if (!init[channel]->buffer_tx.IsEmpty())//发送缓冲区非空时
        {
            Flit flit = init[channel]->buffer_tx.Front();//获取待发送的数据包
            // TODO: check whether it would make sense to use transmission_in_progress to
            // avoid multiple notify()
            LOG << "*** [Ch"<<channel<<"] Requesting transmission event of flit " << flit << endl;
            init[channel]->start_request_event.notify(); //触发传输请求事件
        }
        else
        {
            if (!transmission_in_progress.at(channel)) //没有传输正在进行
            {
                LOG << "*** [Ch"<<channel<<"] Buffer_tx empty and no trasmission in progress, releasing token" << endl;
                flag[channel]->write(RELEASE_CHANNEL); //标记通道为释放状态
            }
            else
                LOG << "*** [Ch"<<channel<<"] Buffer_tx empty, but trasmission in progress, holding token" << endl;
        }
    }
}
```
### `void Hub::txRadioProcessTokenHold`
```cpp
void Hub::txRadioProcessTokenHold(int channel)
{
    // 1. 如果通道被释放，则重新持有
    if (flag[channel]->read() == RELEASE_CHANNEL)
        flag[channel]->write(HOLD_CHANNEL);

    // 2. 检查当前 Hub 是否为令牌持有者
    if (current_token_holder[channel]->read() == local_id)
    {
        // 3. 检查发送缓冲区是否为空
        if (!init[channel]->buffer_tx.IsEmpty())
        {
            // 4. 检查令牌过期时间是否足够发送数据包
            if (current_token_expiration[channel]->read() < flit_transmission_cycles[channel])
            {
                // 5. 如果令牌时间不足，则不发送数据包
                //LOG << "TOKEN_HOLD policy: Not enough token expiration time for sending channel " << channel << endl;
            }
            else
            {
                // 6. 如果令牌时间足够，则持有通道并触发传输事件
                flag[channel]->write(HOLD_CHANNEL);
                LOG << "*** [Ch" << channel << "] Starting transmission event" << endl;
                init[channel]->start_request_event.notify();
            }
        }
        else
        {
            // 7. 如果发送缓冲区为空，则继续持有令牌
            //LOG << "TOKEN_HOLD policy: nothing to transmit, holding token for channel " << channel << endl;
        }
    }
}
```

### `void Hub::txRadioProcessTokenMaxHold`
```cpp
void Hub::txRadioProcessTokenMaxHold(int channel)
{
    // 1. 如果通道被释放，则重新持有
    if (flag[channel]->read() == RELEASE_CHANNEL)
        flag[channel]->write(HOLD_CHANNEL);

    // 2. 检查当前 Hub 是否为令牌持有者
    if (current_token_holder[channel]->read() == local_id)
    {
        // 3. 检查发送缓冲区是否为空
        if (!init[channel]->buffer_tx.IsEmpty())
        {
            // 4. 检查令牌过期时间是否足够发送数据包
            if (current_token_expiration[channel]->read() < flit_transmission_cycles[channel])
            {
                // 5. 如果令牌时间不足，则释放令牌
                //LOG << "TOKEN_MAX_HOLD: Not enough token expiration time, releasing token for channel " << channel << endl;
                flag[channel]->write(RELEASE_CHANNEL);
            }
            else
            {
                // 6. 如果令牌时间足够，则持有通道并触发传输事件
                flag[channel]->write(HOLD_CHANNEL);
                LOG << "Starting transmission on channel " << channel << endl;
                init[channel]->start_request_event.notify();
            }
        }
        else
        {
            // 7. 如果发送缓冲区为空，则释放令牌
            //LOG << "TOKEN_MAX_HOLD: Buffer_tx empty, releasing token for channel " << channel << endl;
            flag[channel]->write(RELEASE_CHANNEL);
        }
    }
}
```

```cpp
void Hub::antennaToTileProcess()
{
    // 1. 复位处理: 如果复位信号有效，则初始化输出端口
    if (reset.read())
    {
        for (int i = 0; i < num_ports; i++)
        {
            req_tx[i]->write(0);		// 关闭请求信号
            current_level_tx[i] = 0;	// 重置当前发送电平
        }
        return;
    }
    // 重要: 请勿移动此行!
    // rxPowerManager 必须在从缓冲区移除 Flit 之前执行其检查
    updateRxPower();

    /***********************************************************************************
      数据从天线流向 Tile 包含 3 个不同的步骤:

      1) 从无线信道接收的数据存储到特定的 buffer_rx (如果可能)
      2) 在 buffer_rx 中找到的数据移动到 buffer_to_tile
      3) 在 buffer_to_tile 中找到的数据移动到 signal_tx

      从实现的角度来看, 它们以 3-2-1 的顺序执行, 以模拟一种流水线序列
     ***********************************************************************************/

    //////////////////////////////////////////////////////////////////////////////////////
    // 将 Flit 从 buffer_to_tile 移动到相应的 signal_tx
    // 无需路由: 每个端口都与一个预定义的 Tile 相关联
    for (int i = 0; i < num_ports; i++)
    {
        // TODO: 检查阻塞通道 (例如阻塞单个信号?)
        for (int k = 0; k < GlobalParams::n_virtual_channels; k++)
        {
            int vc = (start_from_vc[i] + k) % (GlobalParams::n_virtual_channels);

            if (!buffer_to_tile[i][vc].IsEmpty())
            {
                Flit flit = buffer_to_tile[i][vc].Front();

                LOG << "Flit " << flit << " found on buffer_to_tile[" << i << "][" << vc << "] " << endl;
                if (current_level_tx[i] == ack_tx[i].read() &&
                    buffer_full_status_tx[i].read().mask[vc] == false)
                {
                    LOG << "Flit " << flit << " moved from buffer_to_tile[" << i << "][" << vc << "] to signal flit_tx[" << i << "] " << endl;

                    flit_tx[i].write(flit);					// 将 Flit 写入输出端口
                    current_level_tx[i] = 1 - current_level_tx[i];	// 翻转当前电平
                    req_tx[i].write(current_level_tx[i]);		// 写入请求信号

                    buffer_to_tile[i][vc].Pop();			// 从 buffer_to_tile 移除 Flit
                    power.bufferToTilePop();				// 更新 bufferToTile 弹出操作的功耗统计
                    power.r2hLink();					// 更新 Router 到 Hub 链路的功耗统计
                    break; // 端口 Flit 已发送, 跳过剩余 VC
                }
                else
                {
                    LOG << "Flit " << flit << " cannot move from buffer_to_tile[" << i << "][" << vc << "] to signal flit_tx[" << i << "] " << endl;
                }
            } // if buffer not empty
        }
        start_from_vc[i] = (start_from_vc[i] + 1) % GlobalParams::n_virtual_channels;
    }

    /////////////////////////////////////////////////////////////////////////////////
    // 将 Flit 从天线 buffer_rx 移动到相应的 buffer_to_tile
    //
    // 两个不同的阶段:
    // 1) 存储有关传入 Flit 的路由决策 (例如, 到哪个输出端口)
    // 2) 移动 Flit 并从天线 buffer_rx 中移除

    for (unsigned int i = 0; i < rxChannels.size(); i++)
    {
        int channel = rxChannels[i];

        if (!(target[channel]->buffer_rx.IsEmpty()))
        {
            Flit received_flit = target[channel]->buffer_rx.Front();
            power.antennaBufferFront();

            // 检查天线 buffer_rx 并进行适当的预留
            if (received_flit.flit_type == FLIT_TYPE_HEAD)
            {
                int dst_port;

                if (received_flit.hub_relay_node != NOT_VALID)
                    dst_port = tile2Port(received_flit.hub_relay_node);
                else
                    dst_port = tile2Port(received_flit.dst_id);

                TReservation r;
                r.input = channel;
                r.vc = received_flit.vc_id;

                LOG << " Checking reservation availability of output port " << dst_port << " by channel " << channel << " for flit " << received_flit << endl;

                int rt_status = antenna2tile_reservation_table.checkReservation(r, dst_port);

                if (rt_status == RT_AVAILABLE)
                {
                    LOG << "Reserving output port " << dst_port << " by channel " << channel << " for flit " << received_flit << endl;
                    antenna2tile_reservation_table.reserve(r, dst_port);

                    // 使用无线网络进行的通信次数, 也包括部分有线路径
                    wireless_communications_counter++;
                }
                else if (rt_status == RT_ALREADY_SAME)
                {
                    LOG << " RT_ALREADY_SAME reserved direction " << dst_port << " for flit " << received_flit << endl;
                }
                else if (rt_status == RT_OUTVC_BUSY)
                {
                    LOG << " RT_OUTVC_BUSY reservation direction " << dst_port << " for flit " << received_flit << endl;
                }
                else assert(false); // 此处没有有意义的状态
            }
        }
    }
    // 转发
    for (unsigned int i = 0; i < rxChannels.size(); i++)
    {
        int channel = rxChannels[i];
        vector<pair<int, int> > reservations = antenna2tile_reservation_table.getReservations(channel);

        if (reservations.size() != 0)
        {
            int rnd_idx = rand() % reservations.size();

            int port = reservations[rnd_idx].first;
            int vc = reservations[rnd_idx].second;

            if (!(target[channel]->buffer_rx.IsEmpty()))
            {
                Flit received_flit = target[channel]->buffer_rx.Front();
                power.antennaBufferFront();

                if (!buffer_to_tile[port][vc].IsFull())
                {
                    target[channel]->buffer_rx.Pop();		// 从天线 buffer_rx 移除 Flit
                    power.antennaBufferPop();			// 更新天线缓冲区弹出操作的功耗统计
                    LOG << "*** [Ch" << channel << "] Moving flit  " << received_flit << " from buffer_rx to buffer_to_tile[" << port << "][" << vc << "]" << endl;

                    buffer_to_tile[port][vc].Push(received_flit);	// 将 Flit 推送到 buffer_to_tile
                    power.bufferToTilePush();			// 更新 bufferToTile 推送操作的功耗统计

                    if (received_flit.flit_type == FLIT_TYPE_TAIL)
                    {
                        LOG << "Releasing reservation for output port " << port << ", flit " << received_flit << endl;
                        TReservation r;
                        r.input = channel;
                        r.vc = vc;
                        antenna2tile_reservation_table.release(r, port);
                    }
                }
                else
                    LOG << "Full buffer_to_tile[" << port << "][" << vc << "], cannot store " << received_flit << endl;
            }
            else
            {
                // 应该没问题
                /*
                LOG << "WARNING: empty target["<<channel<<"] buffer_rx, but reservation still present, if correct, remove assertion below " << endl;
                assert(false);
                */
            }
        }
    }
}
```

### `void Hub::tileToAntennaProcess()`
```cpp
void Hub::tileToAntennaProcess()
{
    // double cycle = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    // if (cycle > 0 && cycle < 58428)
    // {
    //     if (local_id == 1)
    //     {
    //         cout << "CYCLES " << cycle << endl;
    //         for (int j = 0; j < num_ports; j++)
    //     	buffer_from_tile[j].Print();;
    //         init[0]->buffer_tx.Print();
    //         cout << endl;
    //     }
    // }

    // 1. 复位处理：如果复位信号有效，则初始化发送通道
    if (reset.read())
    {
        for (unsigned int i = 0; i < txChannels.size(); i++)
        {
            int channel = txChannels[i];
            flag[channel]->write(HOLD_CHANNEL); // 设置通道为 HOLD 状态
        }

        TBufferFullStatus bfs;
        for (int i = 0; i < num_ports; i++)
        {
            ack_rx[i]->write(0);				   // 关闭确认信号
            buffer_full_status_rx[i].write(bfs);   // 初始化缓冲区满状态
            current_level_rx[i] = 0;			   // 重置当前接收电平
        }
        return;
    }

    // 2. 令牌环 MAC 策略处理：根据配置的 MAC 策略处理每个发送通道
    for (unsigned int i = 0; i < txChannels.size(); i++)
    {
        int channel = txChannels[i];

        string macPolicy = token_ring->getPolicy(channel).first;

        if (macPolicy == TOKEN_PACKET)
            txRadioProcessTokenPacket(channel); // TOKEN_PACKET 策略
        else if (macPolicy == TOKEN_HOLD)
            txRadioProcessTokenHold(channel);   // TOKEN_HOLD 策略
        else if (macPolicy == TOKEN_MAX_HOLD)
            txRadioProcessTokenMaxHold(channel); // TOKEN_MAX_HOLD 策略
        else
            assert(false); // 未知的 MAC 策略
    }

    int last_reserved = NOT_VALID;

    // 用于存储路由决策
    int *r_from_tile[num_ports];
    for (int i = 0; i < num_ports; i++)
        r_from_tile[i] = new int[GlobalParams::n_virtual_channels];

    // 3. 预留阶段：为从 Tile 到天线的传输进行通道预留
    for (int j = 0; j < num_ports; j++)
    {
        int i = (start_from_port + j) % (num_ports); // 循环遍历端口

        for (int k = 0; k < GlobalParams::n_virtual_channels; k++)
        {
            int vc = (start_from_vc[i] + k) % (GlobalParams::n_virtual_channels);

            // 4. 检查 buffer_from_tile 是否为空
            if (!buffer_from_tile[i][vc].IsEmpty())
            {
                LOG << "Reservation: buffer_from_tile[" << i << "][" << vc << "] not empty " << endl;

                Flit flit = buffer_from_tile[i][vc].Front();

                assert(flit.vc_id == vc);

                power.bufferFromTileFront();		// 更新 bufferFromTile 前端操作的功耗统计
                r_from_tile[i][vc] = route(flit); // 进行路由决策

                // 5. 如果是 HEAD Flit，则进行通道预留
                if (flit.flit_type == FLIT_TYPE_HEAD)
                {
                    TReservation r;
                    r.input = i;
                    r.vc = vc;

                    assert(r_from_tile[i][vc] == DIRECTION_WIRELESS);
                    int channel;

                    // 确定目标通道
                    if (flit.hub_relay_node == NOT_VALID)
                        channel = selectChannel(local_id, tile2Hub(flit.dst_id));
                    else
                        channel = selectChannel(local_id, tile2Hub(flit.hub_relay_node));

                    assert(channel != NOT_VALID && "hubs are not connected by any channel");

                    LOG << "Checking reservation availability of Channel " << channel << " by Hub port[" << i << "][" << vc << "] for flit " << flit << endl;

                    int rt_status = tile2antenna_reservation_table.checkReservation(r, channel);

                    if (rt_status == RT_AVAILABLE)
                    {
                        LOG << "Reservation of channel " << channel << " from Hub port[" << i << "][" << vc << "] by flit " << flit << endl;
                        tile2antenna_reservation_table.reserve(r, channel); // 预留通道
                    }
                    else if (rt_status == RT_ALREADY_SAME)
                    {
                        LOG << "RT_ALREADY_SAME reserved channel " << channel << " for flit " << flit << endl;
                    }
                    else if (rt_status == RT_OUTVC_BUSY)
                    {
                        LOG << "RT_OUTVC_BUSY reservation for channel " << channel << " for flit " << flit << endl;
                    }
                    else if (rt_status == RT_ALREADY_OTHER_OUT)
                    {
                        LOG << "RT_ALREADY_OTHER_OUT a channel different from " << channel << " already reserved by Hub port[" << i << "][" << vc << "]" << endl;
                    }
                    else
                        assert(false); // 此处没有有意义的状态
                }
            }
        }
        start_from_vc[i] = (start_from_vc[i] + 1) % GlobalParams::n_virtual_channels;
    } // for num_ports

    if (last_reserved != NOT_VALID)
        start_from_port = (last_reserved + 1) % num_ports;

    // 6. 转发阶段：将 Flit 从 buffer_from_tile 转发到天线 buffer_tx
    for (int i = 0; i < num_ports; i++)
    {
        vector<pair<int, int> > reservations = tile2antenna_reservation_table.getReservations(i);

        if (reservations.size() != 0)
        {
            int rnd_idx = rand() % reservations.size();

            int o = reservations[rnd_idx].first;
            int vc = reservations[rnd_idx].second;

            // 7. 检查 buffer_from_tile 是否为空
            if (!buffer_from_tile[i][vc].IsEmpty())
            {
                Flit flit = buffer_from_tile[i][vc].Front();
                // powerFront already accounted in 1st phase

                assert(r_from_tile[i][vc] == DIRECTION_WIRELESS);

                int channel = o;

                if (channel != NOT_RESERVED)
                {
                    // 8. 检查天线 buffer_tx 是否已满
                    if (!(init[channel]->buffer_tx.IsFull()))
                    {
                        buffer_from_tile[i][vc].Pop();	  // 从 buffer_from_tile 移除 Flit
                        power.bufferFromTilePop();		  // 更新 bufferFromTile 弹出操作的功耗统计
                        init[channel]->buffer_tx.Push(flit); // 将 Flit 推送到天线 buffer_tx
                        power.antennaBufferPush();		  // 更新天线缓冲区推送操作的功耗统计
                        if (flit.flit_type == FLIT_TYPE_TAIL)
                        {
                            TReservation r;
                            r.input = i;
                            r.vc = vc;
                            tile2antenna_reservation_table.release(r, channel); // 释放通道预留
                        }

                        LOG << "Flit " << flit << " moved from buffer_from_tile[" << i << "][" << vc << "]  to buffer_tx[" << channel << "] " << endl;
                    }
                    else
                    {
                        LOG << "Buffer Full: Cannot move flit " << flit << " from buffer_from_tile[" << i << "] to buffer_tx[" << channel << "] " << endl;
                        //init[channel]->buffer_tx.Print();
                    }
                }
                else
                {
                    LOG << "Forwarding: No channel reserved for input port [" << i << "][" << vc << "], flit " << flit << endl;
                }
            }

        } // for all the ports
    }

    // 9. 从 flit_rx 信号读取 Flit 并存储到 buffer_from_tile
    for (int i = 0; i < num_ports; i++)
    {
        // 10. 检查是否有新的 Flit 到达
        if (req_rx[i]->read() == 1 - current_level_rx[i])
        {
            Flit received_flit = flit_rx[i]->read();
            int vc = received_flit.vc_id;
            LOG << "Reading " << received_flit << " from signal flit_rx[" << i << "]" << endl;

            /*
            if (!buffer_from_tile[i][vc].deadlockFree())
            {
            LOG << " deadlock on buffer " << i << endl;
            buffer_from_tile[i][vc].Print();
            }
            */

            // 11. 检查 buffer_from_tile 是否已满
            if (!buffer_from_tile[i][vc].IsFull())
            {
                LOG << "Storing " << received_flit << " on buffer_from_tile[" << i << "][" << vc << "]" << endl;

                buffer_from_tile[i][vc].Push(received_flit); // 将 Flit 推送到 buffer_from_tile
                power.bufferFromTilePush();				   // 更新 bufferFromTile 推送操作的功耗统计

                current_level_rx[i] = 1 - current_level_rx[i]; // 翻转当前接收电平
            }
            else
            {
                LOG << "Buffer Full: Cannot store " << received_flit << " on buffer_from_tile[" << i << "][" << vc << "]" << endl;
                //buffer_from_tile[i][TODO_VC].Print();
            }
        }
        ack_rx[i]->write(current_level_rx[i]); // 写入确认信号

        // 12. 更新 VC 的掩码以防止数据进入已满的缓冲区
        TBufferFullStatus bfs;
        for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
            bfs.mask[vc] = buffer_from_tile[i][vc].IsFull();
        buffer_full_status_rx[i].write(bfs);
    }

    // 重要: 请勿移动此行!
    // txPowerManager 假设所有 Flit 缓冲区写入操作已完成
    updateTxPower();
}
```
**关键步骤：**

1.  **复位处理**：初始化发送通道状态。
2.  **MAC 策略处理**：根据配置的 MAC 策略处理每个发送通道（TOKEN\_PACKET, TOKEN\_HOLD, TOKEN\_MAX\_HOLD）。
3.  **预留阶段**：为从 Tile 到天线的传输进行通道预留。
4.  **检查 buffer\_from\_tile**：判断缓冲区是否为空。
5.  **HEAD Flit 处理**：如果是 HEAD Flit，则进行通道预留。
6.  **转发阶段**：将 Flit 从 buffer\_from\_tile 转发到天线 buffer\_tx。
7.  **检查 buffer\_from\_tile**：判断缓冲区是否为空。
8.  **检查天线 buffer\_tx**：判断天线缓冲区是否已满。
9.  **从 flit\_rx 读取 Flit**：从 flit\_rx 信号读取 Flit 并存储到 buffer\_from\_tile。
10. **检查是否有新 Flit**：判断是否有新的 Flit 到达。
11. **检查 buffer\_from\_tile**：判断缓冲区是否已满。
12. **更新 VC 掩码**：更新 VC 的掩码以防止数据进入已满的缓冲区。
13. **更新发送功率**：更新发送功率统计。


### `Hub::selectChannel()`
```cpp
int Hub::selectChannel(int src_hub, int dst_hub) const
{
    // 1. 获取源 Hub 的发送通道和目标 Hub 的接收通道
    vector<int> & first = GlobalParams::hub_configuration[src_hub].txChannels;
    vector<int> & second = GlobalParams::hub_configuration[dst_hub].rxChannels;

    // 2. 找到两个 Hub 之间共有的通道（交集）
    vector<int> intersection;

    for (unsigned int i = 0; i < first.size(); i++)
    {
        for (unsigned int j = 0; j < second.size(); j++)
        {
            if (first[i] == second[j])
                intersection.push_back(first[i]); // 将共有通道添加到交集向量
        }
    }

    // 3. 如果没有找到共有通道，则返回 NOT_VALID
    if (intersection.size() == 0)
        return NOT_VALID;

    // 4. 根据全局参数选择通道
    if (GlobalParams::channel_selection == CHSEL_RANDOM)
        // 5. 如果选择随机通道，则从交集中随机选择一个通道
        return intersection[rand() % intersection.size()];
    else if (GlobalParams::channel_selection == CHSEL_FIRST_FREE)
    {
        // 6. 如果选择第一个空闲通道，则从交集中查找第一个空闲通道
        int start_channel = rand() % intersection.size(); // 随机选择起始通道
        int k;

        for (vector<int>::size_type i = 0; i < intersection.size(); i++)
        {
            k = (start_channel + i) % intersection.size(); // 循环遍历交集

            // 7. 检查通道是否空闲
            if (!transmission_in_progress.at(intersection[k]))
            {
                cout << "Found free channel " << intersection[k] << " on (src,dest) (" << src_hub << "," << dst_hub << ") " << endl;
                return intersection[k]; // 找到空闲通道，返回
            }
        }
        // 8. 如果所有通道都忙，则应用随机选择
        cout << "All channel busy, applying random selection " << endl;
        return intersection[rand() % intersection.size()];
    }

    return NOT_VALID; // 未知的通道选择策略
}
```