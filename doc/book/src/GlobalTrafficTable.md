# GlobalTrafficTable
## 全局流量表说明

这段代码定义了一个全局流量表，用于管理和存储网络中各节点之间的通信数据，为网络仿真提供流量模型支持。

### 数据结构定义
- **Communication 结构体**  
    用于存储单个通信链路的详细信息，包括：
    - `src`：源节点ID
    - `dst`：目标节点ID
    - `pir`：数据包注入率
    - `por`：重传概率
    - `t_on`：活动开始时间
    - `t_off`：活动结束时间
    - `t_period`：活动周期

### 类定义
- **GlobalTrafficTable 类**  
    负责管理全局通信信息，主要包括：
    - 构造函数 `GlobalTrafficTable()`：初始化流量表
    - 函数 `load(const char *fname)`：从指定文件加载流量表数据
    - 函数 `getCumulativePirPor(...)`：计算特定周期内源节点的累积数据包注入率和重传概率，并返回目标节点及其累积概率向量
    - 函数 `occurrencesAsSource(const int src_id)`：统计指定源节点在流量表中的出现次数

### 数据存储
- **traffic_table**  
    私有成员变量，通常为一个存储所有 `Communication` 信息的向量。

该模块为网络通信管理提供了结构化的数据模型和相关操作接口，支持复杂的流量查询与统计功能。

## `double getCumulativePirPor(const int src_id,
			       const int ccycle,
			       const bool pir_not_por,
			       vector < pair < int, double > > &dst_prob);`
```cpp
double GlobalTrafficTable::getCumulativePirPor(const int src_id, const int ccycle,const bool pir_not_por,vector < pair < int, double > > &dst_prob)
{
  double cpirnpor = 0.0; // 累积的 PIR 或 POR 值

  dst_prob.clear(); // 清空目标节点概率向量

  for (unsigned int i = 0; i < traffic_table.size(); i++) { // 遍历流量表
    Communication comm = traffic_table[i]; // 获取当前通信信息
    if (comm.src == src_id) { // 如果当前通信的源节点与指定的源节点 ID 匹配
      int r_ccycle = ccycle % comm.t_period; // 计算当前周期在通信周期内的相对位置
      if (r_ccycle > comm.t_on && r_ccycle < comm.t_off) { // 如果当前周期在通信的活动时间内
    cpirnpor += pir_not_por ? comm.pir : comm.por; // 根据 pir_not_por 标志累加 PIR 或 POR
    pair < int, double >dp(comm.dst, cpirnpor); // 创建一个目标节点和累积概率的键值对
    dst_prob.push_back(dp); // 将键值对添加到目标节点概率向量中
      }
    }
  }

  return cpirnpor; // 返回累积的 PIR 或 POR 值
}
```
## `int occurrencesAsSource(const int src_id)`
```cpp
int GlobalTrafficTable::occurrencesAsSource(const int src_id)
{
  int count = 0; // 初始化计数器

  for (unsigned int i = 0; i < traffic_table.size(); i++) // 遍历流量表
    if (traffic_table[i].src == src_id) // 如果当前通信的源节点 ID 与指定的源节点 ID 匹配
      count++; // 计数器加 1

  return count; // 返回源节点 ID 出现的次数
}
```

