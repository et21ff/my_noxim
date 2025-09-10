#include "ScheduleFactory.h"
#include <iostream>

EvictionSchedule ScheduleFactory::createBufferPESchedule() {
    EvictionSchedule schedule;
    int timestep = 0;
    
    for (int k2 = 0; k2 < K2_LOOPS; ++k2) {
        for (int p1 = 0; p1 < P1_LOOPS; ++p1) {
            timestep++;
            
            // 在每次计算完成后，驱逐本次使用的输入数据
            if (p1 == P1_LOOPS-1) { // FILL 阶段
                schedule[timestep][DataType::INPUT] = 3; // 驱逐FILL的3字节输入
            } else { // DELTA 阶段
                schedule[timestep][DataType::INPUT] = 1; // 驱逐DELTA的1字节输入
            }
        }
        // 在内层P1循环结束后，K2循环复用的权重可以被驱逐了
        schedule[timestep][DataType::WEIGHT] = 6;
    }
    
    printScheduleInfo(schedule, "Buffer PE");
    return schedule;
}

EvictionSchedule ScheduleFactory::createGLBEvictionSchedule() {
    EvictionSchedule schedule;
    int logical_timestamp = 0;
    
    for (int k2 = 0; k2 < K2_LOOPS; ++k2) {
        for (int p1 = 0; p1 < P1_LOOPS; ++p1) {
            logical_timestamp++;
            
            if (p1 == 0) { // 这是发送FILL包的任务
                // 发送的6字节WEIGHT，在本步完成后即可驱逐
                schedule[logical_timestamp][DataType::WEIGHT] = 6;
            } else { // 这是发送DELTA包的任务
                // 发送的0字节WEIGHT，在本步完成后即可驱逐
                schedule[logical_timestamp][DataType::WEIGHT] = 0;
            }
        }
    }
    // 在所有任务完成后，驱逐输入数据
    schedule[logical_timestamp][DataType::INPUT] = 18;
    
    printScheduleInfo(schedule, "GLB");
    return schedule;
}

EvictionSchedule ScheduleFactory::createDRAMEvictionSchedule() {
    EvictionSchedule schedule; // 空计划
    printScheduleInfo(schedule, "DRAM");
    return schedule;
}

EvictionSchedule ScheduleFactory::createOutputBufferSchedule() {
    EvictionSchedule schedule;
    int timestep = 0;
    
    // Output Buffer的驱逐策略：每4个时间步驱逐一次OUTPUT数据
    // 这样可以积累一些output数据后再发送，而不是立即驱逐
    for (int i = 0; i < K2_LOOPS * P1_LOOPS; ++i) {
        if (i % 2 == 0 && i > 0) { // 每2步驱逐一次
            timestep++;
            schedule[timestep][DataType::OUTPUT] = 4; // 驱逐积累的4字节output数据
        }
    }
    
    printScheduleInfo(schedule, "Output Buffer");
    return schedule;
} 

void ScheduleFactory::printScheduleInfo(const EvictionSchedule& schedule, const std::string& schedule_name) {
    std::cout << "Created " << schedule_name << " eviction schedule with " 
              << schedule.size() << " timesteps" << std::endl;
    
    // 打印前几个时间步的详细信息（用于调试）
    int count = 0;
    for (EvictionSchedule::const_iterator it = schedule.begin(); it != schedule.end(); ++it) {
        if (count >= 3) break; // 只打印前3个时间步
        
        int timestep = it->first;
        const std::map<DataType, size_t>& evictions = it->second;
        
        std::cout << "  Timestep " << timestep << ": ";
        for (std::map<DataType, size_t>::const_iterator ev_it = evictions.begin(); ev_it != evictions.end(); ++ev_it) {
            DataType type = ev_it->first;
            size_t size = ev_it->second;
            
            std::string type_str;
            switch (type) {
                case DataType::INPUT:  type_str = "INPUT"; break;
                case DataType::WEIGHT: type_str = "WEIGHT"; break;
                case DataType::OUTPUT: type_str = "OUTPUT"; break;
            }
            std::cout << type_str << "=" << size << " ";
        }
        std::cout << std::endl;
        count++;
    }
    
    if (schedule.size() > 3) {
        std::cout << "  ... (showing first 3 of " << schedule.size() << " timesteps)" << std::endl;
    }
}