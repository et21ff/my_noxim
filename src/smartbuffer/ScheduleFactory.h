#ifndef SCHEDULE_FACTORY_H
#define SCHEDULE_FACTORY_H

#include "BufferManager.h"
#include <string>

/**
 * @file ScheduleFactory.h
 * @brief 定义了创建各种驱逐计划的工厂类
 * 
 * 该文件包含了为不同PE角色创建驱逐计划的静态方法，
 * 包括DRAM、GLB、BUFFER等角色的驱逐计划生成逻辑。
 */

class ScheduleFactory {
public:
    /**
     * @brief 为BUFFER PE创建驱逐计划
     * 
     * BUFFER PE在每次计算完成后驱逐本次使用的输入数据，
     * 在内层循环结束后驱逐权重数据。
     * 
     * @return EvictionSchedule 生成的驱逐计划
     */
    static EvictionSchedule createBufferPESchedule();
    
    /**
     * @brief 为GLB PE创建驱逐计划
     * 
     * GLB PE在发送FILL包后驱逐权重数据，
     * 在所有任务完成后驱逐输入数据。
     * 
     * @return EvictionSchedule 生成的驱逐计划
     */
    static EvictionSchedule createGLBEvictionSchedule();
    
    /**
     * @brief 为DRAM PE创建驱逐计划
     * 
     * DRAM PE通常不需要驱逐计划，因为它是数据源。
     * 返回空的驱逐计划。
     * 
     * @return EvictionSchedule 空的驱逐计划
     */
    static EvictionSchedule createDRAMEvictionSchedule();
    
    /**
     * @brief 为Output Buffer创建驱逐计划
     * 
     * Output Buffer用于存储计算产生的输出数据，
     * 可以根据需要设置驱逐策略。
     * 
     * @return EvictionSchedule 生成的驱逐计划
     */
    static EvictionSchedule createOutputBufferSchedule();

private:
    // 黄金参数定义
    static const int K2_LOOPS = 16;
    static const int P1_LOOPS = 16;
    static const int FILL_DATA_SIZE = 9;  // W(6) + I(3)
    static const int DELTA_DATA_SIZE = 1; // I(1)
    
    /**
     * @brief 辅助函数：打印驱逐计划信息
     * 
     * @param schedule 要打印的驱逐计划
     * @param schedule_name 计划名称
     */
    static void printScheduleInfo(const EvictionSchedule& schedule, const std::string& schedule_name);
};

#endif // SCHEDULE_FACTORY_H