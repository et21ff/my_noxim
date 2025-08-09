#include "BufferManager.h"
#include <iostream>
#include <cassert>
#include <string>
#include <functional>

// --- 一个简单的测试框架 ---

// 定义一个测试函数的类型别名
using TestFunction = std::function<void()>;

// 运行单个测试用例的辅助函数
void run_test(const TestFunction& test_func, const std::string& test_name) {
    std::cout << "Running test: " << test_name << "..." << std::endl;
    try {
        test_func();
        std::cout << "  [PASSED]" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "  [FAILED] with exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "  [FAILED] with unknown exception." << std::endl;
    }
}

// --- 测试用例实现 ---

/**
 * @brief 测试1: 正确初始化
 * 验证Buffer能正确加载容量，且初始状态为空。
 */
void test_initialization() {
    EvictionSchedule empty_schedule;
    BufferManager buffer(1024, empty_schedule);

    assert(buffer.GetCapacity() == 1024);
    assert(buffer.GetCurrentSize() == 0);
    assert(!buffer.IsFull());
    assert(buffer.GetDataSize(DataType::INPUT) == 0);
    assert(buffer.GetDataSize(DataType::WEIGHT) == 0);
    assert(buffer.GetDataSize(DataType::OUTPUT) == 0);
}

/**
 * @brief 测试2: 数据接收与空间不足处理
 * 验证OnDataReceived能正确增加数据，并在空间不足时正确拒绝。
 */
void test_data_receive_and_space_limit() {
    EvictionSchedule empty_schedule;
    BufferManager buffer(100, empty_schedule);

    // 成功接收数据
    bool success = buffer.OnDataReceived(DataType::INPUT, 60);
    assert(success);
    assert(buffer.GetCurrentSize() == 60);
    assert(buffer.GetDataSize(DataType::INPUT) == 60);

    // 再次成功接收数据，达到容量上限
    success = buffer.OnDataReceived(DataType::WEIGHT, 40);
    assert(success);
    assert(buffer.GetCurrentSize() == 100);
    assert(buffer.GetDataSize(DataType::WEIGHT) == 40);
    assert(buffer.IsFull());

    // 尝试接收更多数据，应该失败
    success = buffer.OnDataReceived(DataType::OUTPUT, 1);
    assert(!success);
    // 验证状态没有被错误地修改
    assert(buffer.GetCurrentSize() == 100);
    assert(buffer.GetDataSize(DataType::OUTPUT) == 0);
}

/**
 * @brief 测试3: 无提前驱逐
 * 验证在未达到预定驱逐时间点时，数据必须保持驻留。
 */
void test_no_premature_eviction() {
    EvictionSchedule schedule = {
        { 20, { {DataType::INPUT, 50} } } // 驱逐计划在 t=20
    };
    BufferManager buffer(200, schedule);
    buffer.OnDataReceived(DataType::INPUT, 100);
    
    // 在 t=10 (早于计划时间) 调用 OnComputeFinished
    buffer.OnComputeFinished(10);

    // 验证数据没有被驱逐
    assert(buffer.GetCurrentSize() == 100);
    assert(buffer.GetDataSize(DataType::INPUT) == 100);
}

/**
 * @brief 测试4: 精确的触发式驱逐
 * 验证OnComputeFinished(t)调用后，只有在t时间点被标记为可驱逐的数据被移除。
 */
void test_precise_triggered_eviction() {
    EvictionSchedule schedule = {
        { 15, { {DataType::INPUT, 30}, {DataType::WEIGHT, 10} } }
    };
    BufferManager buffer(200, schedule);
    buffer.OnDataReceived(DataType::INPUT, 50);
    buffer.OnDataReceived(DataType::WEIGHT, 40);
    buffer.OnDataReceived(DataType::OUTPUT, 20); // OUTPUT不应被驱逐

    assert(buffer.GetCurrentSize() == 110);

    // 触发驱逐
    buffer.OnComputeFinished(15);

    // 验证驱逐结果
    assert(buffer.GetDataSize(DataType::INPUT) == 50 - 30);   // 50 -> 20
    assert(buffer.GetDataSize(DataType::WEIGHT) == 40 - 10);  // 40 -> 30
    assert(buffer.GetDataSize(DataType::OUTPUT) == 20);      // 保持不变
    assert(buffer.GetCurrentSize() == 20 + 30 + 20);         // 总大小应为 70
}

/**
 * @brief 测试5: 驱逐健壮性 (驱逐量大于实际量)
 * 验证当计划驱逐的大小超过实际存在的大小时，能正确处理而不出错。
 */
void test_eviction_robustness() {
    EvictionSchedule schedule = {
        { 10, { {DataType::INPUT, 999} } } // 计划驱逐一个超大的量
    };
    BufferManager buffer(1000, schedule);
    buffer.OnDataReceived(DataType::INPUT, 100);
    
    assert(buffer.GetCurrentSize() == 100);

    // 触发驱逐
    buffer.OnComputeFinished(10);
    
    // 验证INPUT被完全清空，且大小不会变成负数
    assert(buffer.GetDataSize(DataType::INPUT) == 0);
    assert(buffer.GetCurrentSize() == 0);
}

/**
 * @brief 测试6: 策略特化 (完全驻留模式)
 * 验证传入一个空的驱逐计划，Buffer表现为“完全驻留”模式。
 */
void test_fully_resident_mode() {
    EvictionSchedule empty_schedule; // 空计划
    BufferManager buffer(500, empty_schedule);
    buffer.OnDataReceived(DataType::INPUT, 100);
    buffer.OnDataReceived(DataType::WEIGHT, 100);

    assert(buffer.GetCurrentSize() == 200);

    // 在多个时间步调用 OnComputeFinished
    buffer.OnComputeFinished(10);
    buffer.OnComputeFinished(20);
    buffer.OnComputeFinished(30);

    // 验证数据大小始终不变
    assert(buffer.GetCurrentSize() == 200);
}


// --- 主函数：运行所有测试 ---

// int main() {
//     std::cout << "=======================================" << std::endl;
//     std::cout << "Starting LivenessAwareBuffer Test Suite" << std::endl;
//     std::cout << "=======================================" << std::endl;

//     run_test(test_initialization, "Correct Initialization");
//     run_test(test_data_receive_and_space_limit, "Data Receive & Space Limit");
//     run_test(test_no_premature_eviction, "No Premature Eviction");
//     run_test(test_precise_triggered_eviction, "Precise Triggered Eviction");
//     run_test(test_eviction_robustness, "Eviction Robustness (Over-eviction)");
//     run_test(test_fully_resident_mode, "Policy Specialization (Fully Resident)");

//     std::cout << "=======================================" << std::endl;
//     std::cout << "Test suite finished." << std::endl;
//     std::cout << "=======================================" << std::endl;

//     return 0;
// }