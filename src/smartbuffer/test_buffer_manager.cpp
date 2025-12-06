#include <iostream>
#include <cassert>
#include <map>
#include <vector>
#include "BufferManager.h"

// Helper function to print test status
void PrintTestStatus(const std::string& testName) {
    std::cout << "[RUNNING] " << testName << "..." << std::endl;
}

void PrintTestSuccess(const std::string& testName) {
    std::cout << "[PASSED] " << testName << std::endl;
}

// 1. 构造函数测试
void TestConstructors() {
    PrintTestStatus("Constructor Tests");

    // 共享模式构造函数
    {
        size_t capacity = 100;
        BufferManager bm(capacity);
        assert(bm.GetCapacity() == capacity);
        assert(bm.GetCurrentSize() == 0);
        assert(bm.GetDataSize(DataType::INPUT) == 0);
        assert(bm.GetDataSize(DataType::WEIGHT) == 0);
        assert(bm.GetDataSize(DataType::OUTPUT) == 0);
    }

    // 独立模式构造函数
    {
        std::map<DataType, size_t> type_capacities;
        type_capacities[DataType::INPUT] = 50;
        type_capacities[DataType::WEIGHT] = 30;
        type_capacities[DataType::OUTPUT] = 20;

        BufferManager bm(type_capacities);
        assert(bm.GetCapacity(DataType::INPUT) == 50);
        assert(bm.GetCapacity(DataType::WEIGHT) == 30);
        assert(bm.GetCapacity(DataType::OUTPUT) == 20);
        assert(bm.GetCurrentSize() == 0);
        assert(bm.GetDataSize(DataType::INPUT) == 0);
    }

    PrintTestSuccess("Constructor Tests");
}

// 2. 数据接收测试
void TestDataReception() {
    PrintTestStatus("Data Reception Tests");

    // 共享模式容量限制
    {
        BufferManager bm(100);
        assert(bm.OnDataReceived(DataType::INPUT, 50) == true);
        assert(bm.GetCurrentSize() == 50);
        assert(bm.OnDataReceived(DataType::WEIGHT, 50) == true);
        assert(bm.GetCurrentSize() == 100);
        assert(bm.OnDataReceived(DataType::OUTPUT, 1) == false); // Overflow
        assert(bm.GetCurrentSize() == 100);
    }

    // 独立模式类型限制
    {
        std::map<DataType, size_t> caps = {{DataType::INPUT, 50}, {DataType::WEIGHT, 50}};
        BufferManager bm(caps);
        
        assert(bm.OnDataReceived(DataType::INPUT, 50) == true);
        assert(bm.OnDataReceived(DataType::INPUT, 1) == false); // Input full
        
        assert(bm.OnDataReceived(DataType::WEIGHT, 40) == true);
        assert(bm.OnDataReceived(DataType::WEIGHT, 10) == true);
        assert(bm.OnDataReceived(DataType::WEIGHT, 1) == false); // Weight full
    }

    PrintTestSuccess("Data Reception Tests");
}

// 3. 查询接口测试
void TestQueryInterface() {
    PrintTestStatus("Query Interface Tests");

    BufferManager bm(100);
    bm.OnDataReceived(DataType::INPUT, 30);
    bm.OnDataReceived(DataType::WEIGHT, 20);

    // GetCapacity
    assert(bm.GetCapacity() == 100);
    assert(bm.GetCapacity(DataType::INPUT) == 100); // Shared mode returns total capacity

    // GetCurrentSize
    assert(bm.GetCurrentSize() == 50);
    assert(bm.GetCurrentSize(DataType::INPUT) == 30);
    assert(bm.GetCurrentSize(DataType::WEIGHT) == 20);
    assert(bm.GetCurrentSize(DataType::OUTPUT) == 0);

    // IsFull
    assert(bm.IsFull() == false);
    bm.OnDataReceived(DataType::OUTPUT, 50);
    assert(bm.IsFull() == true);
    
    // GetDataSize
    assert(bm.GetDataSize(DataType::INPUT) == 30);

    PrintTestSuccess("Query Interface Tests");
}

// 4. 数据就绪检查测试
void TestDataReadiness() {
    PrintTestStatus("Data Readiness Tests");

    BufferManager bm(100);
    bm.OnDataReceived(DataType::INPUT, 50);
    bm.OnDataReceived(DataType::WEIGHT, 30);

    // AreDataTypeReady (Single)
    assert(bm.AreDataTypeReady(DataType::INPUT, 50) == true);
    assert(bm.AreDataTypeReady(DataType::INPUT, 51) == false);
    assert(bm.AreDataTypeReady(DataType::WEIGHT, 30) == true);
    assert(bm.AreDataTypeReady(DataType::OUTPUT, 1) == false);

    // AreDataTypesReady (Multiple)
    std::vector<DataType> types = {DataType::INPUT, DataType::WEIGHT};
    assert(bm.AreDataTypesReady(types, 30) == true); // Both have >= 30
    assert(bm.AreDataTypesReady(types, 40) == false); // Weight has 30 < 40

    PrintTestSuccess("Data Readiness Tests");
}

// 5. 数据移除测试
void TestDataRemoval() {
    PrintTestStatus("Data Removal Tests");

    BufferManager bm(100);
    bm.OnDataReceived(DataType::INPUT, 50);

    // RemoveData
    assert(bm.RemoveData(DataType::INPUT, 20) == true);
    assert(bm.GetCurrentSize() == 30);
    assert(bm.GetDataSize(DataType::INPUT) == 30);

    // Remove more than available
    assert(bm.RemoveData(DataType::INPUT, 31) == false);
    assert(bm.GetCurrentSize() == 30);

    // Remove remaining
    assert(bm.RemoveData(DataType::INPUT, 30) == true);
    assert(bm.GetCurrentSize() == 0);

    PrintTestSuccess("Data Removal Tests");
}

// 6. 边界和错误测试
void TestEdgeCases() {
    PrintTestStatus("Edge Case Tests");

    // 空缓冲区操作
    {
        BufferManager bm(100);
        assert(bm.RemoveData(DataType::INPUT, 1) == false);
        assert(bm.GetCurrentSize() == 0);
        assert(bm.IsFull() == false);
    }

    // 无效数据类型处理 (DataType::UNKNOWN)
    {
        BufferManager bm(100);
        // Assuming implementation handles UNKNOWN gracefully or ignores it
        // Based on previous code view, UNKNOWN might be treated as total capacity or 0 depending on context
        // Let's verify specific behaviors seen in BufferManager.cpp
        
        // GetCapacity(UNKNOWN) -> returns capacity_ in SHARED mode
        assert(bm.GetCapacity(DataType::UNKNOWN) == 100);
        
        // GetCurrentSize(UNKNOWN) -> returns current_size_
        bm.OnDataReceived(DataType::INPUT, 10);
        assert(bm.GetCurrentSize(DataType::UNKNOWN) == 10);
    }

    // 独立模式下未配置的数据类型
    {
        std::map<DataType, size_t> caps = {{DataType::INPUT, 50}};
        BufferManager bm(caps);
        // Adding unconfigured type
        // Based on implementation: type_capacities_.find(type) will fail, returns false
        assert(bm.OnDataReceived(DataType::WEIGHT, 10) == false);
    }

    PrintTestSuccess("Edge Case Tests");
}

// 7. 混合模式压力测试
void TestStress() {
    PrintTestStatus("Stress Tests");

    // 测试在独立模式下，一种类型满载不影响其他类型
    std::map<DataType, size_t> caps = {{DataType::INPUT, 10}, {DataType::WEIGHT, 100}};
    BufferManager bm(caps);
    
    // INPUT满载
    assert(bm.OnDataReceived(DataType::INPUT, 10) == true);
    assert(bm.IsFull(DataType::INPUT) == true);
    
    // 尝试继续添加INPUT应该失败
    assert(bm.OnDataReceived(DataType::INPUT, 1) == false);
    
    // WEIGHT应该仍然可以添加，不受INPUT影响
    assert(bm.OnDataReceived(DataType::WEIGHT, 50) == true);
    assert(bm.GetCurrentSize(DataType::WEIGHT) == 50);
    assert(bm.IsFull(DataType::WEIGHT) == false);
    
    // 继续添加WEIGHT直到满载
    assert(bm.OnDataReceived(DataType::WEIGHT, 50) == true);
    assert(bm.IsFull(DataType::WEIGHT) == true);

    PrintTestSuccess("Stress Tests");
}

int main() {
    std::cout << "Starting BufferManager Tests..." << std::endl;
    
    TestConstructors();
    TestDataReception();
    TestQueryInterface();
    TestDataReadiness();
    TestDataRemoval();
    TestEdgeCases();
    TestStress();

    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}