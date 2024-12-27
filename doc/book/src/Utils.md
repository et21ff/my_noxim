# Utils.h 头文件文档

本头文件是 Noxim 片上网络 (NoC) 模拟器的一部分,包含全局参数声明和实用工具函数的定义。

## 概述
- 文件位置: `utils.h`
- 版权所有: © 2005-2018 卡塔尼亚大学

## 核心组件

### 调试日志系统
- 基于 DEBUG 宏的条件日志系统
- 非调试模式下的空流实现
- 调试模式下支持时间戳和函数名跟踪

### 输出流运算符
为以下数据结构重载了输出运算符:
- Flit (数据包)  
- ChannelStatus (通道状态)
- NoP_data (网络包数据)
- TBufferFullStatus (缓冲区状态)
- Coord (坐标)

### SystemC 跟踪功能
用于仿真数据跟踪的函数:
- Flit 跟踪
- NoP_data 跟踪  
- TBufferFullStatus 跟踪
- ChannelStatus 跟踪

### 实用工具函数
常用辅助函数:
- `id2Coord()`: 节点 ID 转换为坐标
- `coord2Id()`: 坐标转换为节点 ID
- `sameRadioHub `和 `hasRadioHub`：检查节点是否连接到相同的无线集线器。
- `tile2Hub`：获取节点连接的集线器 ID。
- `printMap`：打印映射表。
- `i_to_string`：将整数转换为字符串。
- `YouAreSwitch`：检查节点是否为交换机。

这个函数是 `ostream` 操作符 `<<` 的重载，用于输出 `Flit` 对象的信息。`Flit` 是网络芯片模拟器 Noxim 中的数据包单元。该函数根据全局参数 `GlobalParams::verbose_mode` 的值，以不同的详细程度输出 `Flit` 对象的信息。


这个函数 `id2Coord` 用于将节点 ID 转换为坐标 `Coord`。根据不同的网络拓扑结构（如网格拓扑或其他 Delta 拓扑），函数计算节点的 `x` 和 `y` 坐标，并返回一个 `Coord` 对象。

## id2Coord

```cpp
inline Coord id2Coord(int id)
```
- **功能**：将节点 ID 转换为坐标 `Coord`。
- **参数**：节点 ID。
- **返回值**：对应的坐标 `Coord` 对象。

### 坐标对象初始化
```cpp
Coord coord;
```
- **初始化**：创建一个 `Coord` 对象 `coord`，用于存储计算得到的坐标。

### 网格拓扑处理
```cpp
if (GlobalParams::topology == TOPOLOGY_MESH)
{
    coord.x = id % GlobalParams::mesh_dim_x;
    coord.y = id / GlobalParams::mesh_dim_x;

    assert(coord.x < GlobalParams::mesh_dim_x);
    assert(coord.y < GlobalParams::mesh_dim_y);
}
```
- **网格拓扑**：如果全局参数 `GlobalParams::topology` 为 `TOPOLOGY_MESH`，表示当前网络拓扑为网格结构。
  - 计算 `x` 坐标：`coord.x = id % GlobalParams::mesh_dim_x`，即节点 ID 对网格宽度取模。
  - 计算 `y` 坐标：`coord.y = id / GlobalParams::mesh_dim_x`，即节点 ID 除以网格宽度。
  - 断言检查：确保计算得到的 `x` 和 `y` 坐标在网格范围内。

### 其他 Delta 拓扑处理
```cpp
else // other delta topologies
{
    id = id - GlobalParams::n_delta_tiles;
    coord.x = id / (int)(GlobalParams::n_delta_tiles/2);
    coord.y = id % (int)(GlobalParams::n_delta_tiles/2);

    assert(coord.x < log2(GlobalParams::n_delta_tiles));
    assert(coord.y < (GlobalParams::n_delta_tiles/2));
}
```
- **Delta 拓扑**：如果当前网络拓扑不是网格结构，则处理其他 Delta 拓扑。
  - 调整 ID：`id = id - GlobalParams::n_delta_tiles`，将节点 ID 减去 Delta 瓦片数量。
  - 计算 `x` 坐标：`coord.x = id / (int)(GlobalParams::n_delta_tiles/2)`，即调整后的节点 ID 除以 Delta 瓦片数量的一半。
  - 计算 `y` 坐标：`coord.y = id % (int)(GlobalParams::n_delta_tiles/2)`，即调整后的节点 ID 对 Delta 瓦片数量的一半取模。
  - 断言检查：确保计算得到的 `x` 和 `y` 坐标在 Delta 拓扑范围内。

### 返回坐标
```cpp
return coord;
```
- **返回值**：返回计算得到的坐标 `Coord` 对象。

## coord2Id

```cpp
inline int coord2Id(const Coord & coord)
```
- **功能**：将坐标 `Coord` 转换为节点 ID。
- **参数**：坐标 `Coord` 对象。
- **返回值**：对应的节点 ID。

### 网格拓扑处理
```cpp
if (GlobalParams::topology == TOPOLOGY_MESH)
{
    id = (coord.y * GlobalParams::mesh_dim_x) + coord.x;
    assert(id < GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y);
}
```
- **网格拓扑**：如果全局参数 `GlobalParams::topology` 为 `TOPOLOGY_MESH`，表示当前网络拓扑为网格结构。
  - 计算节点 ID：`id = (coord.y * GlobalParams::mesh_dim_x) + coord.x`，即坐标 `y` 乘以网格宽度加上坐标 `x`。
  - 断言检查：确保计算得到的节点 ID 在网格范围内。

### 其他 Delta 拓扑处理
```cpp
else
{
    id = (coord.x * (GlobalParams::n_delta_tiles/2)) + coord.y + GlobalParams::n_delta_tiles;
    assert(id > (GlobalParams::n_delta_tiles-1));
}
```
- **Delta 拓扑**：如果当前网络拓扑不是网格结构，则处理其他 Delta 拓扑。
  - 计算节点 ID：`id = (coord.x * (GlobalParams::n_delta_tiles/2)) + coord.y + GlobalParams::n_delta_tiles`，即坐标 `x` 乘以 Delta 瓦片数量的一半加上坐标 `y`，再加上 Delta 瓦片数量。
  - 断言检查：确保计算得到的节点 ID 在 Delta 拓扑范围内。

### 返回节点 ID
```cpp
return id;
```
- **返回值**：返回计算得到的节点 ID。

## sameRadioHub

```cpp
inline bool sameRadioHub(int id1, int id2)
```
- **功能**：检查两个节点是否连接到相同的无线集线器。
- **参数**：两个节点 ID。
- **返回值**：如果两个节点连接到相同的无线集线器，返回 `true`，否则返回 `false`。

### 查找集线器
```cpp
map<int, int>::iterator it1 = GlobalParams::hub_for_tile.find(id1); 
map<int, int>::iterator it2 = GlobalParams::hub_for_tile.find(id2); 
```
- **查找**：在全局参数 `GlobalParams::hub_for_tile` 中查找节点 ID 对应的集线器。

### 断言检查
```cpp
assert( (it1 != GlobalParams::hub_for_tile.end()) && "Specified Tile is not connected to any Hub");
assert( (it2 != GlobalParams::hub_for_tile.end()) && "Specified Tile is not connected to any Hub");
```
- **检查**：确保两个节点都连接到某个集线器。

### 返回结果
```cpp
return (it1->second == it2->second);
```
- **返回值**：返回两个节点是否连接到相同的集线器。

## hasRadioHub

```cpp
inline bool hasRadioHub(int id)
```
- **功能**：检查节点是否连接到无线集线器。
- **参数**：节点 ID。
- **返回值**：如果节点连接到无线集线器，返回 `true`，否则返回 `false`。

### 查找集线器
```cpp
map<int, int>::iterator it = GlobalParams::hub_for_tile.find(id);
```
- **查找**：在全局参数 `GlobalParams::hub_for_tile` 中查找节点 ID 对应的集线器。

### 返回结果
```cpp
return (it != GlobalParams::hub_for_tile.end());
```
- **返回值**：返回节点是否连接到无线集线器。

## tile2Hub

```cpp
inline int tile2Hub(int id)
```
- **功能**：获取节点连接的集线器 ID。
- **参数**：节点 ID。
- **返回值**：对应的集线器 ID。

### 查找集线器
```cpp
map<int, int>::iterator it = GlobalParams::hub_for_tile.find(id); 
```
- **查找**：在全局参数 `GlobalParams::hub_for_tile` 中查找节点 ID 对应的集线器。

### 断言检查
```cpp
assert( (it != GlobalParams::hub_for_tile.end()) && "Specified Tile is not connected to any Hub");
```
- **检查**：确保节点连接到某个集线器。

### 返回结果
```cpp
return it->second;
```
- **返回值**：返回节点连接的集线器 ID。

## printMap

```cpp
inline void printMap(string label, const map<string,double> & m, std::ostream & out)
```
- **功能**：打印映射表。
- **参数**：
    - `label`：映射表的标签。
    - `m`：映射表对象。
    - `out`：输出流对象。
- **返回值**：无。

### 打印映射表
```cpp
out << label << " = [" << endl;
for (map<string,double>::const_iterator i = m.begin(); i != m.end(); i++)
        out << "\t" << std::scientific << i->second << "\t % " << i->first << endl;
out << "];" << endl;
```
- **打印**：以科学计数法格式打印映射表的内容。

## i_to_string

```cpp
template<typename T> std::string i_to_string(const T& t)
```
- **功能**：将整数转换为字符串。
- **参数**：整数 `t`。
- **返回值**：对应的字符串。

### 转换为字符串
```cpp
std::stringstream s;
s << t;
return s.str();
```
- **转换**：使用 `stringstream` 将整数转换为字符串。

## YouAreSwitch

```cpp
inline bool YouAreSwitch(int id)
```
- **功能**：检查节点是否为交换机。
- **参数**：节点 ID。
- **返回值**：如果节点为交换机，返回 `true`，否则返回 `false`。

### 检查节点
```cpp
if (id < (GlobalParams::n_delta_tiles / 2) * log2(GlobalParams::n_delta_tiles))
        return true;
else
        return false;
```
- **检查**：根据节点 ID 和 Delta 瓦片数量判断节点是否为交换机。