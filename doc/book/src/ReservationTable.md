# ReservationTable

类实现了以下功能：

1. **预留管理**：
    - `checkReservation(const TReservation r, const int port_out)`: 检查指定的输入端口/虚拟通道是否已经预留了指定的输出端口。
    - `reserve(const TReservation r, const int port_out)`: 为指定的输入端口/虚拟通道预留指定的输出端口。如果该输出端口已经被预留，则会触发断言。
    - `release(const TReservation r, const int port_out)`: 释放指定的输入端口/虚拟通道对指定输出端口的预留。如果该输出端口没有被预留或者预留信息不匹配，则会触发断言。
    - `isNotReserved(const int port_out)`: 检查指定的输出端口是否没有被任何输入端口/虚拟通道预留。

2. **查询**：
    - `getReservations(const int port_int)`: 获取指定输入端口预留的所有输出端口和虚拟通道的对应关系。

3. **优先级管理**：
    - `updateIndex()`: 更新预留表中每个输出端口的优先级最高的预留项的索引（具体优先级策略未在此头文件中定义）。

4. **配置**：
    - `setSize(const int n_outputs)`: 设置预留表的大小，即交换机的输出端口数量。

5. **调试**：
    - `print()`: 打印预留表的内容，用于调试和查看预留状态。

## 类成员函数

- **ReservationTable()**  
  构造函数，用于初始化预留表。

- `name() const`  
  返回预留表的名字（"ReservationTable"）。

- `checkReservation(const TReservation r, const int port_out)`

- `reserve(const TReservation r, const int port_out)`

- `release(const TReservation r, const int port_out)`

- `getReservations(const int port_int)`

- `updateIndex()`

- `isNotReserved(const int port_out)`

- `setSize(const int n_outputs)`

- `print()`

## 私有成员变量

- `TRTEntry *rtable`  
  指向 TRTEntry 类型数组的指针，用于存储预留信息。`rtable[i]` 存储了预留输出端口 `i` 的所有输入端口/虚拟通道的集合。

- `int n_outputs`  
  输出端口的数量。

## 辅助数据结构

- **TReservation**  
  结构体，表示一个预留项，包含输入端口 `input` 和虚拟通道 `vc`。

- **RTEntry**  
  结构体，表示预留表中的一个条目，包含一个 `TReservation` 类型的向量 `reservations` 和一个索引 `index`，用于指示当前优先级最高的预留项。

## `checkReservation(const TReservation r, const int port_out)`
```cpp
int ReservationTable::checkReservation(const TReservation r, const int port_out)
{
    // 1. 检查是否存在禁止的表状态：相同的输入/VC 存在于不同的输出行中
    for (int o = 0; o < n_outputs; o++)
    {
        for (vector<TReservation>::size_type i = 0; i < rtable[o].reservations.size(); i++)
        {
            // 在当前实现中，这不应该发生
            if (o != port_out && rtable[o].reservations[i] == r)
            {
                return RT_ALREADY_OTHER_OUT; // 相同的输入/VC 已经预留了另一个输出端口
            }
        }
    }

    // 2. 在给定的输出条目上，预留必须按 VC 区分
    // 动机：它们将按周期交错，因为索引会移动

    int n_reservations = rtable[port_out].reservations.size();
    for (int i = 0; i < n_reservations; i++)
    {
        // 3. 预留已经存在
        if (rtable[port_out].reservations[i] == r)
            return RT_ALREADY_SAME; // 相同的输入/VC 已经预留了该输出端口

        // 4. 同一个 VC 已经被另一个输入预留了该输出端口
        if (rtable[port_out].reservations[i].input != r.input &&
            rtable[port_out].reservations[i].vc == r.vc)
            return RT_OUTVC_BUSY; // 该输出端口的同一个 VC 已经被另一个输入端口预留
    }
    // 5. 没有冲突，该输出端口可用
    return RT_AVAILABLE; // 该输出端口可用
}
```

### `reserve(const TReservation r, const int port_out)`
```cpp
void ReservationTable::reserve(const TReservation r, const int port_out)
{
    // 1. 重要提示：当 Hub 与更多连接一起使用时存在问题
    //
    // 预留已预留/无效的端口是非法的。正确性应由 ReservationTable 用户保证
    assert(checkReservation(r, port_out) == RT_AVAILABLE); // 确保要预留的端口可用

    // 2. TODO：更好的策略可以在尽可能远离当前索引的特定位置插入
    rtable[port_out].reservations.push_back(r); // 将预留信息添加到指定输出端口的预留列表中
}
```

