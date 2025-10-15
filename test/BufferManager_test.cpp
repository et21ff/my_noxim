// 关键：这个宏定义会告诉 Catch2 在这里自动生成 main() 函数
#define CATCH_CONFIG_MAIN
#include "catch.hpp" // 包含 Catch2 的单头文件

// 包含我们需要测试的模块和相关的数据结构
#include "../src/smartbuffer/BufferManager.h"
#include "../src/DataStructs.h"

// ---------------------------------------------------------------------------------
// 使用一个 Fixture 类来为每个测试用例提供一个干净的 BufferManager 实例
// 这等同于 Google Test 中的 `class BufferManagerTest : public ::testing::Test`
// ---------------------------------------------------------------------------------
class BufferManagerFixture {
public:
    BufferManagerFixture() {
        // 创建一个空的驱逐计划用于测试
        EvictionSchedule empty_schedule;
        buffer = new BufferManager(1000, empty_schedule);
    }

    ~BufferManagerFixture() {
        delete buffer;
    }

protected:
    BufferManager* buffer;
};

// ---------------------------------------------------------------------------------
// 测试套件: 使用 TEST_CASE_METHOD 将 Fixture 应用于每个测试用例
// 使用 [BufferManager] 标签来组织测试
// ---------------------------------------------------------------------------------

// 用例1: 验证流式策略 (STREAM) 会减少缓冲区大小
TEST_CASE_METHOD(BufferManagerFixture, "Stream strategy decrements buffer size on transfer", "[BufferManager]") {
    // 1. 准备 (Arrange)
    buffer->OnDataReceived(DataType::WEIGHT, 100);
    buffer->SetReuseStrategy(DataType::WEIGHT, ReuseStrategy::STREAM);

    // 2. 执行 (Act)
    buffer->OnDataTransferred(DataType::WEIGHT, 10);

    // 3. 断言 (Assert)
    REQUIRE(buffer->GetCurrentSize() == 90);
}

// 用例2: 验证驻留策略 (RESIDENT) 会保持缓冲区大小
TEST_CASE_METHOD(BufferManagerFixture, "Resident strategy preserves buffer size on transfer", "[BufferManager]") {
    // 1. 准备
    buffer->OnDataReceived(DataType::INPUT, 20);
    buffer->SetReuseStrategy(DataType::INPUT, ReuseStrategy::RESIDENT);

    // 2. 执行
    buffer->OnDataTransferred(DataType::INPUT, 5);

    // 3. 断言
    REQUIRE(buffer->GetCurrentSize() == 20);
}

// 用例3: 验证 EvictData 总会减少缓冲区大小，即使策略是 RESIDENT
TEST_CASE_METHOD(BufferManagerFixture, "EvictData always decrements size regardless of strategy", "[BufferManager]") {
    // 1. 准备
    buffer->OnDataReceived(DataType::INPUT, 20);
    buffer->SetReuseStrategy(DataType::INPUT, ReuseStrategy::RESIDENT); // 明确设置为驻留

    // 2. 执行
    bool success = buffer->EvictData(DataType::INPUT, 15);

    // 3. 断言
    REQUIRE(success == true);
    REQUIRE(buffer->GetCurrentSize() == 5);
}

// 用例4: 验证混合策略下的行为
TEST_CASE_METHOD(BufferManagerFixture, "Mixed stream and resident strategies work correctly", "[BufferManager]") {
    // 1. 准备
    buffer->OnDataReceived(DataType::WEIGHT, 100);
    buffer->OnDataReceived(DataType::INPUT, 20);
    buffer->SetReuseStrategy(DataType::WEIGHT, ReuseStrategy::STREAM);
    buffer->SetReuseStrategy(DataType::INPUT, ReuseStrategy::RESIDENT);

    // 2. 执行
    buffer->OnDataTransferred(DataType::WEIGHT, 10); // 应该将总大小减少 10
    buffer->OnDataTransferred(DataType::INPUT, 5);   // 应该不改变总大小

    // 3. 断言
    // 初始总大小 = 120. 减少了 10. 最终应为 110.
    REQUIRE(buffer->GetCurrentSize() == 110);
}

// 用例5: 验证对空缓冲区进行操作的安全性
TEST_CASE_METHOD(BufferManagerFixture, "Operations on empty buffer are safe", "[BufferManager]") {
    // 1. 准备: 缓冲区是空的
    REQUIRE(buffer->GetCurrentSize() == 0);

    // 2. 执行 & 3. 断言
    bool success = buffer->EvictData(DataType::WEIGHT, 10);
    REQUIRE(success == false);
    REQUIRE(buffer->GetCurrentSize() == 0);

    // 对一个不存在的数据类型设置策略然后操作，也不应崩溃
    buffer->SetReuseStrategy(DataType::OUTPUT, ReuseStrategy::STREAM);
    buffer->OnDataTransferred(DataType::OUTPUT, 5);
    REQUIRE(buffer->GetCurrentSize() == 0);
}

// 用例6: 验证未设置策略时的默认行为
TEST_CASE_METHOD(BufferManagerFixture, "Default behavior when no strategy is set", "[BufferManager]") {
    // 1. 准备 - 不设置任何策略
    buffer->OnDataReceived(DataType::OUTPUT, 50);

    // 2. 执行
    buffer->OnDataTransferred(DataType::OUTPUT, 10);

    // 3. 断言 - 应该保持不变，因为未设置策略时默认行为是不操作
    REQUIRE(buffer->GetCurrentSize() == 50);
}

// 用例7: 验证策略切换的行为
TEST_CASE_METHOD(BufferManagerFixture, "Strategy switching works correctly", "[BufferManager]") {
    // 1. 准备
    buffer->OnDataReceived(DataType::WEIGHT, 100);
    buffer->SetReuseStrategy(DataType::WEIGHT, ReuseStrategy::RESIDENT);

    // 2. 执行 - 第一次传输 (RESIDENT)
    buffer->OnDataTransferred(DataType::WEIGHT, 20);
    REQUIRE(buffer->GetCurrentSize() == 100); // 应该保持

    // 3. 切换策略并再次传输
    buffer->SetReuseStrategy(DataType::WEIGHT, ReuseStrategy::STREAM);
    buffer->OnDataTransferred(DataType::WEIGHT, 30);
    REQUIRE(buffer->GetCurrentSize() == 70); // 应该减少
}

// 用例8: 验证边界情况 - 传输大小为0
TEST_CASE_METHOD(BufferManagerFixture, "Zero size transfer handling", "[BufferManager]") {
    // 1. 准备
    buffer->OnDataReceived(DataType::INPUT, 50);
    buffer->SetReuseStrategy(DataType::INPUT, ReuseStrategy::STREAM);

    // 2. 执行
    buffer->OnDataTransferred(DataType::INPUT, 0);

    // 3. 断言
    REQUIRE(buffer->GetCurrentSize() == 50); // 应该保持不变
}

// 用例9: 验证边界情况 - 传输大小超过现有数据
TEST_CASE_METHOD(BufferManagerFixture, "Transfer size larger than available data", "[BufferManager]") {
    // 1. 准备
    buffer->OnDataReceived(DataType::WEIGHT, 30);
    buffer->SetReuseStrategy(DataType::WEIGHT, ReuseStrategy::STREAM);

    // 2. 执行
    buffer->OnDataTransferred(DataType::WEIGHT, 50); // 尝试传输超过现有数据的大小

    // 3. 断言
    REQUIRE(buffer->GetCurrentSize() == 0); // 应该只减少到0
}