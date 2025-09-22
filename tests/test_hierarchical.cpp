#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <vector>
#include <algorithm>
#include <memory>    // C++11: 用于 std::shared_ptr

#include "NoC.h"
#include "Router.h"
#include "GlobalParams.h"
#include "ConfigurationManager.h"

using namespace std;

unsigned int drained_volume = 0; // 全局变量，用于跟踪已排空的流量
// ====================================================================================
//                        测试环境 Fixture (和之前一样)
// ====================================================================================
struct TestFixture {
    std::unique_ptr<NoC> noc;
    int* fanouts_per_level_ptr;

    TestFixture() : fanouts_per_level_ptr(NULL) {
        cout << "--- [Fixture Setup] Constructing Test Fixture ONCE ---" << endl;
        const char* mocked_argv[] = {"test_program", "-config", "../config_examples/hirearchy.yaml"};
        int mocked_argc = sizeof(mocked_argv) / sizeof(char*);
        configure(mocked_argc, const_cast<char**>(mocked_argv));
        GlobalParams::topology = TOPOLOGY_HIERARCHICAL;
        GlobalParams::num_levels = 3;
        fanouts_per_level_ptr = new int[3]{4, 4, 0};
        GlobalParams::fanouts_per_level = fanouts_per_level_ptr;
        GlobalParams::n_virtual_channels = 1;
        GlobalParams::buffer_depth = 8;
        noc.reset(new NoC("TestNoC"));
    }

    ~TestFixture() {
        cout << "--- [Fixture Teardown] Destroying Test Fixture ONCE ---" << endl;
        delete[] fanouts_per_level_ptr;
    }
};

// ====================================================================================
//            *** 核心修改：全局共享的 Fixture 获取函数 ***
// 这个函数使用一个静态的 shared_ptr 来确保 TestFixture 对象只被创建一次。
// 1. 第一次调用时，`fixture_ptr` 为空，于是 new 一个 TestFixture 并交给它管理。
// 2. 后续所有调用，`fixture_ptr` 已存在，直接返回已创建的指针。
// 3. 程序结束时，静态的 shared_ptr 会被销毁，自动调用 TestFixture 的析构函数。
// ====================================================================================
std::shared_ptr<TestFixture> getSharedFixture() {
    static std::shared_ptr<TestFixture> fixture_ptr;
    if (!fixture_ptr) {
        fixture_ptr = std::make_shared<TestFixture>();
    }
    return fixture_ptr;
}

// ====================================================================================
// 现在，我们的测试用例不再自己创建 Fixture，而是从全局函数获取共享的实例。
// ====================================================================================
SCENARIO("Hierarchical NoC Multicast Routing Logic", "[routing][hierarchical]") {

    GIVEN("A single, shared, fully constructed hierarchical NoC") {
        
        // 从全局函数获取共享的 Fixture 实例
        auto fixture = getSharedFixture();
        
        REQUIRE(fixture != nullptr);
        REQUIRE(fixture->noc != nullptr);
        NoC& noc = *fixture->noc;

        WHEN("the root node (0) multicasts to nodes in different subtrees {5, 10, 15}") {
            int src_id = 0;
            vector<int> dst_ids = {5, 10, 15};
            
            Router* r = noc.t[src_id]->r;
            MulticastRouteData mc_data;
            mc_data.current_id = src_id; mc_data.src_id = src_id; mc_data.dst_ids = dst_ids;
            mc_data.dir_in = 0; mc_data.vc_id = 0;
            
            vector<int> routes = r->routeMulticast(mc_data);

            THEN("it should produce routes towards three distinct ports {1, 2, 3}") {
                REQUIRE(routes.size() == 3);
                std::sort(routes.begin(), routes.end());
                REQUIRE(routes == std::vector<int>{1, 2, 3});
            }
        }

        WHEN("an intermediate node (1) multicasts to its direct children {5, 6, 7}") {
            int src_id = 1;
            vector<int> dst_ids = {5, 9, 13};

            Router* r = noc.t[src_id]->r;
            MulticastRouteData mc_data;
            mc_data.current_id = src_id; mc_data.src_id = src_id; mc_data.dst_ids = dst_ids;
            mc_data.dir_in = 0; mc_data.vc_id = 0;

            vector<int> routes = r->routeMulticast(mc_data);

            THEN("it should produce a single route towards its children") {
                REQUIRE(routes.size() == 3);
                REQUIRE(routes[0] == r->getLogicalPortIndex(r->PORT_DOWN, 0));
                REQUIRE(routes[1] == r->getLogicalPortIndex(r->PORT_DOWN, 1));
                REQUIRE(routes[2] == r->getLogicalPortIndex(r->PORT_DOWN, 2));
            }
        }
    }
}

int sc_main(int argc, char* argv[]) {
    // 这个函数永远不会被调用，因为程序的入口是 Catch2 生成的 main()
    // 它存在的唯一目的就是为了让链接器满意
    return 0;
}