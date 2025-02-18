# TokenRing 模块分析

本章将分析 TokenRing 模块的实现，这是一个用于令牌环网络控制的 SystemC 模块。

## 基本结构

TokenRing 模块的核心声明如下：

```cpp
SC_MODULE(TokenRing)
{
    SC_HAS_PROCESS(TokenRing);
}
```

## 接口定义

### 时钟和复位端口

模块包含基本的同步接口：

```cpp
sc_in_clk clock;    // 时钟输入端口
sc_in<bool> reset;  // 复位信号输入端口
```

### 令牌控制信号

模块使用三种映射来管理令牌状态：

1. **令牌持有者信号**：
```cpp
map<int, sc_out<int>* > current_token_holder;
```

2. **令牌过期信号**：
```cpp
map<int, sc_out<int>* > current_token_expiration;
```

3. **标志信号**：
```cpp
map<int, map<int,sc_inout<int>* > > flag;
```

## 内部实现

### 内部信号

模块维护三组内部信号用于状态管理：

```cpp
map<int, sc_signal<int>* > token_holder_signals;
map<int, sc_signal<int>* > token_expiration_signals;
map<int, map<int, sc_signal<int>* > > flag_signals;
```

## 函数
### `void TokenRing::updateTokenPacket(int channel)`
这段代码实现了令牌环网络中令牌传递的更新逻辑。让我们详细分析其功能和实现。
```cpp
void TokenRing::updateTokenPacket(int channel)
{
    int token_pos = token_position[channel]; \\获取令牌所在的位置
    int token_holder = rings_mapping[channel][token_pos]; \\获取当前令牌持有者
    // TEST HOLD BUG
	//if (flag[channel][token_pos]->read() == RELEASE_CHANNEL)

    if (flag[channel][token_holder]->read() == RELEASE_CHANNEL) \\如果当前令牌可以被释放
	{
	    // number of hubs of the ring
	    int num_hubs = rings_mapping[channel].size();

	    token_position[channel] = (token_position[channel]+1)%num_hubs; //更新令牌位置

	    int new_token_holder = rings_mapping[channel][token_position[channel]]; //获取新的令牌持有者
        LOG << "*** Token of channel " << channel << " has been assigned to Hub_" <<  new_token_holder << endl; //记录日志
	    current_token_holder[channel]->write(new_token_holder); //更新令牌持有者
	    // TEST HOLD BUG
	    //flag[channel][token_position[channel]]->write(HOLD_CHANNEL);
        flag[channel][new_token_holder]->write(HOLD_CHANNEL); //更新令牌为持有状态
	}
}
```


### `void TokenRing::updateTokenMaxHold(int channel)`
```cpp
void TokenRing::updateTokenMaxHold(int channel)
{
	if (--token_hold_count[channel] == 0 ||
		flag[channel][token_position[channel]]->read() == RELEASE_CHANNEL) \\如果令牌的持有时间已经达到最大或者令牌可以被释放
	{
	    token_hold_count[channel] = atoi(GlobalParams::channel_configuration[channel].macPolicy[1].c_str()); //重置令牌的持有时间
	    // number of hubs of the ring
	    int num_hubs = rings_mapping[channel].size(); //获取环网中的Hub数量

	    token_position[channel] = (token_position[channel]+1)%num_hubs; //更新令牌位置
	    LOG << "*** Token of channel " << channel << " has been assigned to Hub_" <<  rings_mapping[channel][token_position[channel]] << endl; //记录日志

	    current_token_holder[channel]->write(rings_mapping[channel][token_position[channel]]); //更新令牌持有者
	}

	current_token_expiration[channel]->write(token_hold_count[channel]); //更新令牌过期时间
}
```

### `void TokenRing::updateTokenHold(int channel)`
```cpp
void TokenRing::updateTokenHold(int channel) {
    // 1. 检查令牌持有时间是否到期
    if (--token_hold_count[channel] == 0) {
        // 2. 如果到期，则重置令牌持有时间
        token_hold_count[channel] = atoi(GlobalParams::channel_configuration[channel].macPolicy[1].c_str());

        // 3. 获取环中 Hub 的数量
        int num_hubs = rings_mapping[channel].size();

        // 4. 更新令牌在环中的位置
        token_position[channel] = (token_position[channel] + 1) % num_hubs;

        // 5. 记录令牌分配信息
        LOG << "*** Token of channel " << channel << " has been assigned to Hub_"
            << rings_mapping[channel][token_position[channel]] << endl;

        // 6. 更新令牌持有者
        current_token_holder[channel]->write(rings_mapping[channel][token_position[channel]]);
    }

    // 7. 更新令牌过期时间
    current_token_expiration[channel]->write(token_hold_count[channel]);
}
```

### `void TokenRing::updateTokens()`
这是令牌环网络中用于更新令牌状态的主要方法。让我们分析其实现和功能。

```cpp
void TokenRing::updateTokens()
{
    if (reset.read()) {//复位信号
        for (map<int,ChannelConfig>::iterator i = GlobalParams::channel_configuration.begin();
             i!=GlobalParams::channel_configuration.end();
             i++)
            current_token_holder[i->first]->write(rings_mapping[i->first][0]);
    }
    else //非复位信号
    {
        for (map<int,ChannelConfig>::iterator i = GlobalParams::channel_configuration.begin(); i!=GlobalParams::channel_configuration.end(); i++) //遍历所有通道
        {
            int channel = i->first;
            //int channel_holder;
            //channel_holder = current_token_holder[channel]->read();
            string macPolicy = getPolicy(channel).first; //获取当前通道的MAC策略
            if (macPolicy == TOKEN_PACKET) 
                updateTokenPacket(channel);
            else if (macPolicy == TOKEN_HOLD)
                updateTokenHold(channel);
            else if (macPolicy == TOKEN_MAX_HOLD)
                updateTokenMaxHold(channel);
            else
                assert(false);
        }
    }
}
```
### `void TokenRing::attachHub()`
```cpp
void TokenRing::attachHub(int channel, int hub, sc_in<int>* hub_token_holder_port, sc_in<int>* hub_token_expiration_port, sc_inout<int>* hub_flag_port) {
    // 1. 检查是否已存在该通道的端口
    if (!current_token_holder[channel]) {
        // 2. 如果不存在，则初始化该通道的令牌环相关数据结构
        token_position[channel] = 0;  // 初始化令牌位置

        // 3. 创建并分配端口和信号
        current_token_holder[channel] = new sc_out<int>();       // 当前令牌持有者端口
        current_token_expiration[channel] = new sc_out<int>();   // 令牌过期时间端口
        token_holder_signals[channel] = new sc_signal<int>();    // 令牌持有者信号
        token_expiration_signals[channel] = new sc_signal<int>();// 令牌过期时间信号

        // 4. 绑定端口和信号
        current_token_holder[channel]->bind(*(token_holder_signals[channel]));
        current_token_expiration[channel]->bind(*(token_expiration_signals[channel]));

        // 5. 初始化令牌持有时间
        token_hold_count[channel] = 0;

        // 6. 根据 MAC 策略设置令牌持有时间
        if (GlobalParams::channel_configuration[channel].macPolicy[0] != TOKEN_PACKET) {
            // 计算延迟周期数
            double delay_ps = 1000 * GlobalParams::flit_size / GlobalParams::channel_configuration[channel].dataRate;
            int cycles = ceil(delay_ps / GlobalParams::clock_period_ps);

            // 获取最大持有周期数
            int max_hold_cycles = atoi(GlobalParams::channel_configuration[channel].macPolicy[1].c_str());

            // 断言周期数小于最大持有周期数
            assert(cycles < max_hold_cycles);

            // 设置令牌持有时间
            token_hold_count[channel] = atoi(GlobalParams::channel_configuration[channel].macPolicy[1].c_str());
        }
    }

    // 7. 创建并绑定 Hub 的标志端口和信号
    flag[channel][hub] = new sc_inout<int>();
    flag_signals[channel][hub] = new sc_signal<int>();
    flag[channel][hub]->bind(*(flag_signals[channel][hub]));
    hub_flag_port->bind(*(flag_signals[channel][hub]));

    // 8. 连接 TokenRing 到 Hub
    hub_token_holder_port->bind(*(token_holder_signals[channel]));
    hub_token_expiration_port->bind(*(token_expiration_signals[channel]));

    // 9. 将 Hub 添加到环的映射中
    rings_mapping[channel].push_back(hub);

    // 10. 设置初始令牌持有者
    int starting_hub = rings_mapping[channel][0];
    current_token_holder[channel]->write(starting_hub);
}
```