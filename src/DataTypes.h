#ifndef DATATYPES_H
#define DATATYPES_H

#include <string>
enum class DataType {
    INPUT,
    WEIGHT,
    OUTPUT,
    UNKNOWN
};


inline const char* DataType_to_str(DataType type) {
    switch (type) {
        case DataType::INPUT:  return "INPUT";
        case DataType::WEIGHT: return "WEIGHT";
        case DataType::OUTPUT: return "OUTPUT";
        default:               return "UNKNOWN";
    }
}
inline DataType stringToDataType(const std::string& str) {
    if (str == "INPUT" || str == "Inputs" || str == "input")  return DataType::INPUT;
    if (str == "WEIGHT" || str == "Weights" || str == "weight") return DataType::WEIGHT;
    if (str == "OUTPUT" || str == "Outputs" || str == "output") return DataType::OUTPUT;
    return DataType::UNKNOWN;
}

enum PE_Role { ROLE_UNUSED, ROLE_DRAM, ROLE_GLB, ROLE_BUFFER, ROLE_DISTRIBUTOR };
inline const char* roleToString(PE_Role role) {
    switch (role) {
        case ROLE_UNUSED: return "ROLE_UNUSED";
        case ROLE_DRAM:   return "ROLE_DRAM";
        case ROLE_GLB:    return "ROLE_GLB";
        case ROLE_BUFFER: return "ROLE_BUFFER";
        case ROLE_DISTRIBUTOR: return "ROLE_DISTRIBUTOR";
        default:          return "UNKNOWN_ROLE";
    }
}

inline PE_Role stringToRole(const std::string& str) {
    if (str == "ROLE_UNUSED") return ROLE_UNUSED;
    if (str == "ROLE_DRAM")   return ROLE_DRAM;
    if (str == "ROLE_GLB")    return ROLE_GLB;
    if (str == "ROLE_BUFFER") return ROLE_BUFFER;
    if (str == "ROLE_DISTRIBUTOR") return ROLE_DISTRIBUTOR;
    return ROLE_UNUSED; // 默认返回
}

typedef struct  {
    std::vector<int> main_channel_caps;
    std::vector<int> output_channel_caps;
} RoleChannelCapabilities;


struct LevelConfig {
    int level;
    int buffer_size;
    PE_Role roles;

};

struct HierarchicalConfig {
    std::vector<LevelConfig> levels;

    HierarchicalConfig() = default;
    HierarchicalConfig(const std::vector<LevelConfig>& lvl_configs) : levels(lvl_configs) {}

    const LevelConfig& get_level_config(int level_idx) const {
        return levels[level_idx];
    }
};
#endif // DATATYPES_H

