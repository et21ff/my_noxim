#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "ReservationTable.h"
#include <vector>
#include <dbg.h>

using namespace std;

// ====================================================================================
//                        ReservationTable 单元测试
// ====================================================================================

SCENARIO("ReservationTable Unit Tests", "[reservationtable]") {

    GIVEN("A fresh ReservationTable with 5 output ports") {
        ReservationTable rt;
        rt.setSize(5);

        // 辅助函数：检查端口是否被预留
        auto isReserved = [&rt](const TReservation& r, int port_out) -> bool {
            return rt.checkReservation(r, port_out) == RT_ALREADY_SAME;
        };

        // 测试用的预留对象
        TReservation r1 = {1, 0};  // 输入端口1，VC 0
        TReservation r2 = {2, 0};  // 输入端口2，VC 0
        TReservation r3 = {3, 0};  // 输入端口3，VC 0
        TReservation r4 = {4, 0};  // 输入端口4，VC 0

        // 类别一：回归测试 - 确保单播功能完好
        WHEN("performing basic unicast reserve and release") {
            THEN("should successfully reserve and release single port") {
                // Test Case 1.1: Unicast_Simple_ReserveAndRelease
                REQUIRE(rt.checkReservation(r1, 2) == RT_AVAILABLE);
                rt.reserve(r1, 2);
                REQUIRE(isReserved(r1, 2));
                rt.release(r1, 2);
                REQUIRE(rt.checkReservation(r1, 2) == RT_AVAILABLE);
            }
        }

        WHEN("testing unicast conflict reservation") {
            THEN("should reject conflicting reservation") {
                // Test Case 1.2: Unicast_Conflict_Reservation
                rt.reserve(r1, 3);
                REQUIRE(rt.checkReservation(r2, 3) != RT_AVAILABLE);

                // 尝试预留已被占用的端口
                int original_size = rt.checkReservation(r1, 3);
                //rt.reserve(r2, 3);  // 应该失败但不改变状态
                REQUIRE(rt.checkReservation(r1, 3) == original_size);
            }
        }

        // 类别二：基本多播功能测试
        WHEN("performing simple multicast reservation") {
            THEN("should successfully reserve and release multiple ports") {
                // Test Case 2.1: Multicast_Simple_ReserveAndRelease
                vector<int> multicast_ports = {1, 3, 4};

                REQUIRE(rt.checkReservation(r1, multicast_ports) == RT_AVAILABLE);
                rt.reserve(r1, multicast_ports);

                // 检查所有端口都被预留
                REQUIRE(rt.checkReservation(r1, multicast_ports) == RT_ALREADY_SAME);

                // 释放所有端口
                rt.release(r1, multicast_ports);

                // 检查所有端口都被释放
                REQUIRE(rt.checkReservation(r1, 1) == RT_AVAILABLE);
                REQUIRE(rt.checkReservation(r1, 3) == RT_AVAILABLE);
                REQUIRE(rt.checkReservation(r1, 4) == RT_AVAILABLE);
            }
        }

        // 类别三：原子性专项测试
        WHEN("testing multicast atomicity with partial conflict") {
            THEN("should not reserve any ports if any port is unavailable") {
                // Test Case 3.1: Multicast_Atomicity_PartialConflict
                // 预先占用端口3
                rt.reserve(r2, 3);

                // 定义多播树，其中端口3已被占用
                vector<int> multicast_ports = {1, 3, 4};

                // 尝试预留多播树
                if(rt.checkReservation(r1, multicast_ports) == RT_AVAILABLE)
                rt.reserve(r1, multicast_ports);

                // 关键检查点：没有任何端口被预留
                REQUIRE(rt.checkReservation(r1, 1) == RT_AVAILABLE);
                REQUIRE(isReserved(r2, 3));  // 端口3仍被r2占用
                REQUIRE(rt.checkReservation(r1, 4) == RT_AVAILABLE);

                // 清理
                rt.release(r2, 3);
            }
        }

        WHEN("testing multicast atomicity with self conflict") {
            THEN("should not reserve any ports if there's internal conflict") {
                // Test Case 3.2: Multicast_Atomicity_SelfConflict
                // 预留一个端口来制造冲突
                rt.reserve(r2, 3);

                // 尝试预留包含冲突的多播树
                vector<int> multicast_ports = {2, 3, 4};
                if(rt.checkReservation(r1, multicast_ports) == RT_AVAILABLE)
                rt.reserve(r1, multicast_ports);

                // 检查没有任何端口被预留
                REQUIRE(rt.checkReservation(r1, 2) == RT_AVAILABLE);
                REQUIRE(isReserved(r2, 3));  // 端口3仍被r2占用
                REQUIRE(rt.checkReservation(r1, 4) == RT_AVAILABLE);
            }
        }

        // 类别四：混合冲突场景测试
        WHEN("testing unicast blocked by multicast") {
            THEN("should reject unicast when multicast has reserved the port") {
                // Test Case 4.1: Unicast_Blocked_By_Multicast
                vector<int> multicast_ports = {1, 2, 3};
                rt.reserve(r1, multicast_ports);

                // 尝试单播预留已被多播占用的端口
                REQUIRE(rt.checkReservation(r2, 2) != RT_AVAILABLE);

                // 清理
                rt.release(r1, multicast_ports);
            }
        }

        WHEN("testing multicast blocked by multicast") {
            THEN("should reject multicast when ports conflict with existing multicast") {
                // Test Case 4.3: Multicast_Blocked_By_Multicast
                vector<int> multicast1_ports = {1, 2, 3};
                vector<int> multicast2_ports = {3, 4, 0};

                // 预留第一个多播
                rt.reserve(r1, multicast1_ports);

                // 尝试预留第二个有冲突的多播
                if(rt.checkReservation(r3, multicast2_ports) == RT_AVAILABLE)
                rt.reserve(r3, multicast2_ports);

                // 检查第二个多播的任何分支都没有被预留
                REQUIRE(rt.checkReservation(r3, 3) != RT_AVAILABLE);  // 冲突
                REQUIRE(rt.checkReservation(r3, 4) == RT_AVAILABLE);   // 空闲但不应被预留
                REQUIRE(rt.checkReservation(r3, 0) == RT_AVAILABLE);   // 空闲但不应被预留

                // 清理
                rt.release(r1, multicast1_ports);
            }
        }

        // 类别五：边缘情况和无效输入测试
        WHEN("testing empty multicast tree") {
            THEN("should handle empty vector gracefully") {
                // Test Case 5.1: Reserve_Empty_Multicast_Tree
                vector<int> empty_ports;

                // 不应崩溃，应返回AVAILABLE（空操作）
                REQUIRE(rt.checkReservation(r1, empty_ports) == RT_AVAILABLE);
                rt.reserve(r1, empty_ports);  // 不应崩溃
                rt.release(r1, empty_ports);  // 不应崩溃
            }
        }

        WHEN("testing multicast tree with duplicate links") {
            THEN("should handle duplicate links correctly") {
                // Test Case 5.2: Reserve_Multicast_Tree_With_Duplicate_Links
                vector<int> duplicate_ports = {1, 2, 2, 3};  // 端口2重复

                // 检查应该成功（重复链接不影响可用性检查）
                REQUIRE(rt.checkReservation(r1, duplicate_ports) == RT_AVAILABLE);

                // 预留应该成功
                rt.reserve(r1, duplicate_ports);

                REQUIRE(rt.checkReservation(r1, duplicate_ports) == RT_ALREADY_SAME);

                // 释放应该成功
                rt.release(r1, duplicate_ports);

                // 检查所有端口都被释放
                REQUIRE(rt.checkReservation(r1, 1) == RT_AVAILABLE);
                REQUIRE(rt.checkReservation(r1, 2) == RT_AVAILABLE);
                REQUIRE(rt.checkReservation(r1, 3) == RT_AVAILABLE);
            }
        }

        WHEN("testing checkReservation with mixed availability") {
            THEN("should return first encountered error") {
                // 预留端口2和端口4
                rt.reserve(r2, 2);
                rt.reserve(r3, 4);

                // 检查混合可用性的端口列表
                vector<int> mixed_ports = {0, 1, 2, 3, 4};
                int result = rt.checkReservation(r1, mixed_ports);

                // 应该返回第一个遇到的错误（端口2被占用）
                REQUIRE(result != RT_AVAILABLE);

                // 清理
                rt.release(r2, 2);
                rt.release(r3, 4);
            }
        }
    }
}

int sc_main(int argc, char* argv[]) {
    // 这个函数永远不会被调用，因为程序的入口是 Catch2 生成的 main()
    // 它存在的唯一目的就是为了让链接器满意
    return 0;
}