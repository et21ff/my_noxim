/*
 * Noxim - the NoC Simulator
 * 
 * 层次化拓扑管理器实现
 */
 
#include "HierarchicalTopologyManager.h"
#include <iostream>
#include <fstream>
#include <cassert>
 
HierarchicalTopologyManager::HierarchicalTopologyManager() 
    : num_levels(0), nodes_per_level(nullptr), total_nodes(0),
      node_level_map(nullptr), parent_map(nullptr), child_map(nullptr) {
}
 
HierarchicalTopologyManager::~HierarchicalTopologyManager() {
    cleanup();
}
 
bool HierarchicalTopologyManager::validateGlobalParams() {
    if (GlobalParams::num_levels <= 0 || GlobalParams::fanouts_per_level == nullptr) {
        std::cerr << "错误: 全局配置 (GlobalParams) 无效或未初始化。" << std::endl;
        std::cerr << "  - GlobalParams::num_levels: " << GlobalParams::num_levels << std::endl;
        std::cerr << "  - GlobalParams::fanouts_per_level is " 
                  << (GlobalParams::fanouts_per_level == nullptr ? "nullptr" : "not null") << std::endl;
        return false;
    }
    return true;
}
 
bool HierarchicalTopologyManager::initialize() {
    if (!validateGlobalParams()) {
        return false;
    }
    
    num_levels = GlobalParams::num_levels;
    calculateNodesPerLevel();
    
    cout << "层次化结构: " << num_levels << "层" << endl;
    cout << "节点分布: ";
    for (int i = 0; i < num_levels; i++) {
        cout << "L" << i << "(" << nodes_per_level[i] << ") ";
    }
    cout << "= 总计 " << total_nodes << " 个节点" << endl;
    
    return true;
}
 
void HierarchicalTopologyManager::calculateNodesPerLevel() {
    nodes_per_level = new int[num_levels];
    
    for (int i = 0; i < num_levels; i++) {
        if (i == 0) {
            nodes_per_level[i] = 1; // 根节点
        } else {
            nodes_per_level[i] = nodes_per_level[i-1] * GlobalParams::fanouts_per_level[i-1];
        }
        
        #ifdef DEBUG
        cout << "Level " << i << ": fanout=" << (i > 0 ? GlobalParams::fanouts_per_level[i-1] : 0) 
             << ", nodes=" << nodes_per_level[i] << endl;
        #endif
    }
    
    // 计算总节点数
    total_nodes = 0;
    for (int i = 0; i < num_levels; i++) {
        total_nodes += nodes_per_level[i];
    }
}
 
void HierarchicalTopologyManager::buildTopology() {
    buildHierarchicalMappings();
    buildRoleMappings();
    writeToGlobalParams();
    
    cout << "层次化拓扑构建完成" << endl;
}
 
void HierarchicalTopologyManager::buildHierarchicalMappings() {
    cout << "建立层次化映射关系..." << endl;
    
    // 分配映射数组
    node_level_map = new int[total_nodes];
    parent_map = new int[total_nodes];
    child_map = new int*[total_nodes];
    
    // 初始化
    for (int i = 0; i < total_nodes; i++) {
        node_level_map[i] = -1;
        parent_map[i] = -1;
        child_map[i] = nullptr;
    }
    
    // 构建层次化映射
    int node_id = 0;
    
    for (int level = 0; level < num_levels; level++) {
        int level_start = node_id;
        int level_end = node_id + nodes_per_level[level];
        
        // 设置节点层级
        for (int i = level_start; i < level_end; i++) {
            node_level_map[i] = level;
        }
        
        // 非根节点设置父节点关系
        if (level > 0) {
            int parent_level_start = 0;
            for (int p = 0; p < level - 1; p++) {
                parent_level_start += nodes_per_level[p];
            }
            
            int fanout = GlobalParams::fanouts_per_level[level - 1];
            
            for (int i = 0; i < nodes_per_level[level]; i++) {
                int current_node = level_start + i;
                int parent_id = parent_level_start + (i / fanout);
                parent_map[current_node] = parent_id;
                
                // 为父节点分配子节点数组（如果还未分配）
                if (child_map[parent_id] == nullptr) {
                    child_map[parent_id] = new int[fanout];
                    for (int j = 0; j < fanout; j++) {
                        child_map[parent_id][j] = -1;
                    }
                }
                
                // 添加子节点映射
                int child_index = i % fanout;
                child_map[parent_id][child_index] = current_node;
            }
        }
        
        node_id += nodes_per_level[level];
    }
    
    // 根节点无父节点
    if (total_nodes > 0) {
        parent_map[0] = -1;
    }
    
    cout << "层次化映射建立完成" << endl;
}
 
void HierarchicalTopologyManager::buildRoleMappings() {
    cout << "建立角色映射关系..." << endl;
    
    HierarchicalConfig& config = GlobalParams::hierarchical_config;
    
    // 找到GLB层和COMPUTE层的索引
    int glb_level = -1, compute_level = -1;
    for (int i = 0; i < config.levels.size(); i++) {
        if (config.levels[i].roles == ROLE_GLB) {
            glb_level = i;
        }
        if (config.levels[i].roles == ROLE_BUFFER) {
            compute_level = i;
        }
    }
    
    if (glb_level == -1 || compute_level == -1) {
        cout << "警告: 未找到GLB层或COMPUTE层配置，跳过角色映射" << endl;
        return;
    }
    
    // 计算各层的起始节点ID
    int glb_start = 0, compute_start = 0;
    for (int i = 0; i < glb_level; i++) {
        glb_start += nodes_per_level[i];
    }
    for (int i = 0; i < compute_level; i++) {
        compute_start += nodes_per_level[i];
    }
    
    // 清空现有映射
    GlobalParams::storage_to_compute_map.clear();
    GlobalParams::compute_to_storage_map.clear();
    
    // 遍历所有GLB节点，建立与COMPUTE节点的映射关系
    for (int i = 0; i < nodes_per_level[glb_level]; i++) {
        int glb_id = glb_start + i;
        vector<int> compute_nodes;
        
        // 递归查找所有子计算节点
        findComputeNodes(glb_id, compute_level, compute_nodes);
        
        GlobalParams::storage_to_compute_map[glb_id] = compute_nodes;
        
        // 建立反向映射
        for (int compute_id : compute_nodes) {
            GlobalParams::compute_to_storage_map[compute_id] = glb_id;
        }
        
        cout << "GLB节点 " << glb_id << " 管理计算节点: ";
        for (int cid : compute_nodes) {
            cout << cid << " ";
        }
        cout << endl;
    }
    
    cout << "角色映射建立完成" << endl;
}
 
void HierarchicalTopologyManager::findComputeNodes(int node_id, int target_level, vector<int>& result) {
    int current_level = node_level_map[node_id];
    
    if (current_level == target_level) {
        result.push_back(node_id);
        return;
    }
    
    // 递归遍历所有子节点
    if (child_map[node_id] != nullptr) {
        int fanout = GlobalParams::fanouts_per_level[current_level];
        for (int i = 0; i < fanout; i++) {
            if (child_map[node_id][i] != -1) {
                findComputeNodes(child_map[node_id][i], target_level, result);
            }
        }
    }
}
 
void HierarchicalTopologyManager::writeToGlobalParams() {
    // 将构建的映射写入GlobalParams
    GlobalParams::node_level_map = node_level_map;
    GlobalParams::parent_map = parent_map; 
    GlobalParams::child_map = child_map;
    GlobalParams::num_nodes = total_nodes;
}
 
void HierarchicalTopologyManager::printTopologyInfo() {
    cout << "\n=== 层次化拓扑信息 ===" << endl;
    cout << "总层数: " << num_levels << endl;
    cout << "总节点数: " << total_nodes << endl;
    
    for (int level = 0; level < num_levels; level++) {
        cout << "第" << level << "层: " << nodes_per_level[level] << " 个节点" << endl;
    }
    
    cout << "\n节点映射关系:" << endl;
    for (int i = 0; i < total_nodes; i++) {
        cout << "节点" << i << ": 层级=" << node_level_map[i] 
             << ", 父节点=" << (parent_map[i] == -1 ? "无" : to_string(parent_map[i]));
        
        if (child_map[i] != nullptr) {
            cout << ", 子节点=[";
            int current_level = node_level_map[i];
            if (current_level < num_levels - 1) {
                int fanout = GlobalParams::fanouts_per_level[current_level];
                for (int j = 0; j < fanout; j++) {
                    if (child_map[i][j] != -1) {
                        cout << child_map[i][j];
                        if (j < fanout - 1 && child_map[i][j+1] != -1) cout << ",";
                    }
                }
            }
            cout << "]";
        }
        cout << endl;
    }
}
 
// 查询接口实现
int HierarchicalTopologyManager::getNodesInLevel(int level) const {
    if (level >= 0 && level < num_levels) {
        return nodes_per_level[level];
    }
    return -1;
}
 
int HierarchicalTopologyManager::getNodeLevel(int node_id) const {
    if (node_id >= 0 && node_id < total_nodes && node_level_map != nullptr) {
        return node_level_map[node_id];
    }
    return -1;
}
 
int HierarchicalTopologyManager::getParentNode(int node_id) const {
    if (node_id >= 0 && node_id < total_nodes && parent_map != nullptr) {
        return parent_map[node_id];
    }
    return -1;
}
 
vector<int> HierarchicalTopologyManager::getChildNodes(int node_id) const {
    vector<int> children;
    if (node_id >= 0 && node_id < total_nodes && child_map != nullptr && child_map[node_id] != nullptr) {
        int current_level = node_level_map[node_id];
        if (current_level < num_levels - 1) {
            int fanout = GlobalParams::fanouts_per_level[current_level];
            for (int i = 0; i < fanout; i++) {
                if (child_map[node_id][i] != -1) {
                    children.push_back(child_map[node_id][i]);
                }
            }
        }
    }
    return children;
}
 
vector<int> HierarchicalTopologyManager::getComputeNodesForStorage(int storage_node_id) const {
    auto it = GlobalParams::storage_to_compute_map.find(storage_node_id);
    if (it != GlobalParams::storage_to_compute_map.end()) {
        return it->second;
    }
    return vector<int>();
}
 
int HierarchicalTopologyManager::getStorageNodeForCompute(int compute_node_id) const {
    auto it = GlobalParams::compute_to_storage_map.find(compute_node_id);
    if (it != GlobalParams::compute_to_storage_map.end()) {
        return it->second;
    }
    return -1;
}
 
bool HierarchicalTopologyManager::validateTopology() const {
    // 验证拓扑结构的一致性
    if (total_nodes <= 0 || num_levels <= 0) return false;
    if (node_level_map == nullptr || parent_map == nullptr) return false;
    
    // 验证每个节点的层级映射
    for (int i = 0; i < total_nodes; i++) {
        if (node_level_map[i] < 0 || node_level_map[i] >= num_levels) {
            cerr << "错误: 节点" << i << "的层级映射无效: " << node_level_map[i] << endl;
            return false;
        }
    }
    
    // 验证父子关系的一致性
    for (int i = 1; i < total_nodes; i++) { // 跳过根节点
        int parent = parent_map[i];
        if (parent == -1) {
            cerr << "错误: 非根节点" << i << "没有父节点" << endl;
            return false;
        }
        
        if (parent >= total_nodes) {
            cerr << "错误: 节点" << i << "的父节点ID超出范围: " << parent << endl;
            return false;
        }
        
        // 验证父节点确实将该节点作为子节点
        bool found_as_child = false;
        if (child_map[parent] != nullptr) {
            int parent_level = node_level_map[parent];
            if (parent_level < num_levels - 1) {
                int fanout = GlobalParams::fanouts_per_level[parent_level];
                for (int j = 0; j < fanout; j++) {
                    if (child_map[parent][j] == i) {
                        found_as_child = true;
                        break;
                    }
                }
            }
        }
        
        if (!found_as_child) {
            cerr << "错误: 节点" << i << "声称父节点为" << parent << "，但父节点的子节点列表中没有找到" << endl;
            return false;
        }
    }
    
    cout << "拓扑验证通过" << endl;
    return true;
}
 
void HierarchicalTopologyManager::dumpTopologyToFile(const string& filename) const {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "无法打开文件: " << filename << endl;
        return;
    }
    
    file << "# 层次化拓扑结构转储文件" << endl;
    file << "# 生成时间: " << __DATE__ << " " << __TIME__ << endl;
    file << endl;
    
    file << "总层数: " << num_levels << endl;
    file << "总节点数: " << total_nodes << endl;
    file << endl;
    
    file << "各层节点数:" << endl;
    for (int i = 0; i < num_levels; i++) {
        file << "Level " << i << ": " << nodes_per_level[i] << " nodes" << endl;
    }
    file << endl;
    
    file << "节点映射关系:" << endl;
    file << "NodeID\tLevel\tParent\tChildren" << endl;
    for (int i = 0; i < total_nodes; i++) {
        file << i << "\t" << node_level_map[i] << "\t";
        file << (parent_map[i] == -1 ? "ROOT" : to_string(parent_map[i])) << "\t";
        
        if (child_map[i] != nullptr) {
            int current_level = node_level_map[i];
            if (current_level < num_levels - 1) {
                int fanout = GlobalParams::fanouts_per_level[current_level];
                for (int j = 0; j < fanout; j++) {
                    if (child_map[i][j] != -1) {
                        file << child_map[i][j] << " ";
                    }
                }
            }
        } else {
            file << "LEAF";
        }
        file << endl;
    }
    
    file.close();
    cout << "拓扑结构已转储到文件: " << filename << endl;
}
 
void HierarchicalTopologyManager::cleanup() {
    if (nodes_per_level) {
        delete[] nodes_per_level;
        nodes_per_level = nullptr;
    }
    
    if (node_level_map) {
        delete[] node_level_map;
        node_level_map = nullptr;
    }
    
    if (parent_map) {
        delete[] parent_map;
        parent_map = nullptr;
    }
    
    if (child_map) {
        for (int i = 0; i < total_nodes; i++) {
            if (child_map[i]) {
                delete[] child_map[i];
            }
        }
        delete[] child_map;
        child_map = nullptr;
    }
}