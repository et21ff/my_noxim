#Main
## void signalHandler( int signum )
```cpp
void signalHandler( int signum )
{
    cout << "\b\b  " << endl;
    cout << endl;
    cout << "Current Statistics:" << endl;
    cout << "(" << sc_time_stamp().to_double() / GlobalParams::clock_period_ps << " sim cycles executed)" << endl;
    GlobalStats gs(n);
    gs.showStats(std::cout, GlobalParams::detailed);
}
```
### 函数概述

`signalHandler`

 是一个信号处理函数，用于响应系统信号（如 SIGQUIT）。当接收到信号时，它会输出当前仿真的统计信息。

### 关键组件分析

1. **输出格式控制**
```cpp
cout << "\b\b  " << endl;
cout << endl;
```
- `\b\b` 使用退格字符清除前面的字符
- 输出两个换行符创建空白行

2. **统计信息标题**
```cpp
cout << "Current Statistics:" << endl;
```
打印统计信息的标题行

3. **仿真周期计算**
```cpp
cout << "(" << sc_time_stamp().to_double() / GlobalParams::clock_period_ps << " sim cycles executed)" << endl;
```
- 

sc_time_stamp()

 获取当前 SystemC 仿真时间
- 将时间戳除以时钟周期得到执行的仿真周期数

4. **详细统计信息**
```cpp
GlobalStats gs(n);
gs.showStats(std::cout, GlobalParams::detailed);
```
- 创建 GlobalStats对象
- 调用 `showStats` 输出详细统计信息

这是 Noxim NoC 模拟器的主函数实现。让我详细解释其关键部分：

### 初始化阶段
```cpp
signal(SIGQUIT, signalHandler);  // 设置信号处理
drained_volume = 0;              // 初始化排空数据量
configure(arg_num, arg_vet);     // 处理命令行参数
```

### SystemC 核心组件设置
```cpp
sc_clock clock("clock", GlobalParams::clock_period_ps, SC_PS);  // 创建系统时钟
sc_signal<bool> reset;                                          // 创建复位信号
n = new NoC("NoC");                                            // 创建 NoC 实例
n->clock(clock);                                               // 连接时钟
n->reset(reset);                                               // 连接复位信号
```

### 信号追踪设置
如果启用了追踪模式：
- 创建 VCD 追踪文件
- 追踪复位和时钟信号
- 对网格中每个节点的东西南北方向追踪：
  - 请求信号 (req)
  - 确认信号 (ack)

### 仿真执行
1. 复位阶段：
```cpp
reset.write(1);                                                // 激活复位
sc_start(GlobalParams::reset_time * GlobalParams::clock_period_ps, SC_PS);
reset.write(0);                                                // 释放复位
```

2. 主仿真阶段：
```cpp
sc_start(GlobalParams::simulation_time * GlobalParams::clock_period_ps, SC_PS);
```

### 结果处理
- 关闭追踪文件
- 显示仿真完成信息和统计数据
- 检查是否达到预期的数据排空量

值得注意的是：
- 时间单位使用皮秒（PS）而不是纳秒（NS）
- 使用 `GlobalParams` 来配置各种参数
- 包含详细的信号追踪功能
- 具有完整的统计信息收集和显示机制