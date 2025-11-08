/*
 * Noxim - the NoC Simulator
 * 
 * 层次化拓扑管理器 - 负责构建和管理层次化网络拓扑
 * 将拓扑构建逻辑从NoC类中解耦，提供独立的拓扑管理功能
 */
 
#ifndef __HIERARCHICAL_TOPOLOGY_MANAGER_H__
#define __HIERARCHICAL_TOPOLOGY_MANAGER_H__
 
#include <vector>
#include <map>
#include <iostream>
#include "DataTypes.h"
#include "GlobalParams.h"
 
using namespace std;
 
class HierarchicalTopologyManager {
private:
    // 拓扑基本信息
    int num_levels;
    int* nodes_per_level;
    int total_nodes;
    
    // 映射关系数组
    int* node_level_map;        // 节点ID -> 层级
    int* parent_map;            // 节点ID -> 父节点ID
    int** child_map;            // 节点ID -> 子节点ID数组
    
    // 私有辅助方法
    bool validateGlobalParams();
    void calculateNodesPerLevel();
    void buildHierarchicalMappings();
    void buildRoleMappings();
    void findComputeNodes(int node_id, int target_level, vector<int>& result);
    void writeToGlobalParams();
    void cleanup();
 
public:
    // 构造函数和析构函数
    HierarchicalTopologyManager();
    ~HierarchicalTopologyManager();
    
    // 主要公共接口
    bool initialize();                          // 初始化拓扑管理器
    void buildTopology();                       // 构建完整的层次化拓扑
    void printTopologyInfo();                   // 打印拓扑信息
    
    // 查询接口
    int getTotalNodes() const { return total_nodes; }
    int getNumLevels() const { return num_levels; }
    int getNodesInLevel(int level) const;
    int getNodeLevel(int node_id) const;
    int getParentNode(int node_id) const;
    vector<int> getChildNodes(int node_id) const;
    
    // 角色映射查询
    vector<int> getComputeNodesForStorage(int storage_node_id) const;
    int getStorageNodeForCompute(int compute_node_id) const;
    
    // 调试和验证
    bool validateTopology() const;
    void dumpTopologyToFile(const string& filename) const;
};
 
#endif // __HIERARCHICAL_TOPOLOGY_MANAGER_H__