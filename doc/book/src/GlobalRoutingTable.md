# GlobalRoutingTable
# GlobalRoutingTable

本文档详细介绍了 GlobalRoutingTable.h 头文件的内容，其核心在于管理 NoC（片上网络）路由表的基本数据结构和相关函数。

**概述**  
该文件提供了用于管理网络路由表的抽象方法，包括链路（LinkId）、允许的输出集合（AdmissibleOutputs）以及整个路由表的定义。

**数据结构**  
- **LinkId**  
    - 一对整数，表示源节点和目标节点，作为链路的唯一标识。  
- **AdmissibleOutputs**  
    - 一个包含 LinkId 的集合，定义了在路由决策中允许的输出链路。  
- **RoutingTableLink**  
    - 一个映射，将目标节点（int）与其对应的可接受输出链路集合（AdmissibleOutputs）关联。  
- **RoutingTableNode**  
    - 一个映射，将输入链路（LinkId）与其所属的路由表（RoutingTableLink）关联。  
- **RoutingTableNoC**  
    - 一个映射，将网络中每个节点（int）与其路由表（RoutingTableNode）对应，体现了整个 NoC 的路由配置。

**工具函数**  
- **direction2ILinkId(int node_id, int dir)**  
    - 将输入方向转换为对应的链路（LinkId），映射节点输入方向。  
- **oLinkId2Direction(const LinkId & out_link)**  
    - 将输出链路（LinkId）转换回方向值。  
- **admissibleOutputsSet2Vector(const AdmissibleOutputs & ao)**  
    - 将一组可接受的输出链路转换为整数向量，表示具体的方向。

**GlobalRoutingTable 类**  
该类封装了整个网络的全局路由表，并提供对路由信息的存储与查询功能。

**类成员**  
- **rt_noc**  
    - 类型为 RoutingTableNoC，存储整个网络的路由表数据。  
- **valid**  
    - 布尔值，指示路由表是否已成功加载并且有效。

**成员函数**  
- **GlobalRoutingTable()**  
    - 构造函数，用于初始化路由表。  
- **bool load(const char *fname)**  
    - 从指定文件加载路由表数据，加载成功返回 true，否则返回 false。  
- **RoutingTableNode getNodeRoutingTable(const int node_id)**  
    - 获取给定节点的路由表。  
- **bool isValid()**  
    - 检查路由表加载后是否处于有效状态。

