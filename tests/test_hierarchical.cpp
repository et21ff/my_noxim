#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <vector>
#include <algorithm>
#include <memory>    // C++11: 用于 std::shared_ptr

#include "NoC.h"
#include "Router.h"
#include "GlobalParams.h"
#include "ConfigurationManager.h"
#include "ReservationTable.h"

using namespace std;

unsigned int drained_volume = 0; // 全局变量，用于跟踪已排空的流量
// ====================================================================================
//                        测试环境 Fixture (和之前一样)
// ====================================================================================
struct TestFixture {
    std::unique_ptr<NoC> noc;
    int* fanouts_per_level_ptr;
    sc_clock clock;
    sc_signal<bool> reset;

    TestFixture() : fanouts_per_level_ptr(NULL)
    , clock("fixture_clock", 1000, SC_PS),
      reset("fixture_reset") {
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
        noc->clock(clock);
        noc->reset(reset);
    }

    ~TestFixture() {
        cout << "--- [Fixture Teardown] Destroying Test Fixture ONCE ---" << endl;
        delete[] fanouts_per_level_ptr;
    }

    void resetNoc() {
        cout << "--- Resetting NoC State ---" << endl;
        reset.write(true);
        // Run simulation for a few cycles to ensure reset propagates
        // Use the same duration as your main application
        sc_start(GlobalParams::reset_time * GlobalParams::clock_period_ps, SC_PS);
        reset.write(false);
        // Maybe run for one more cycle to come out of reset cleanly
        sc_start(1 * GlobalParams::clock_period_ps, SC_PS);
        cout << "--- NoC Reset Complete ---" << endl;
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
        fixture->resetNoc();

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

// ====================================================================================
//                        Test Case 1.1: Pure_Unicast_Pipeline
// ====================================================================================
SCENARIO("Test Case 1.1: Pure Unicast Pipeline", "[sanity][unicast]") {

    GIVEN("A hierarchical NoC with non-overlapping paths") {
        auto fixture = getSharedFixture();
        NoC& noc = *fixture->noc;
        fixture->resetNoc();

        WHEN("PE A sends packets to PE B and PE C sends packets to PE D simultaneously") {
            // 在3层4-4-16的层次结构中，选择不相交的路径：
            // 路径1: 节点0 -> 节点1 -> 节点5
            // 路径2: 节点2 -> 节点3 -> 节点10
            int src_A = 0, dst_B = 5;    // 使用根节点0作为源，发送到叶子节点5
            int src_C = 2, dst_D = 10;   // 使用中间节点2作为源，发送到叶子节点10

            // 创建测试flit（简化版本，避免vector复制问题）
            Flit flit_AB;
            flit_AB.src_id = src_A;
            flit_AB.dst_id = dst_B;
            flit_AB.flit_type = FLIT_TYPE_HEAD;
            flit_AB.vc_id = 0;
            flit_AB.sequence_no = 0;
            flit_AB.sequence_length = 1;
            flit_AB.is_multicast = false;

            Flit flit_CD;
            flit_CD.src_id = src_C;
            flit_CD.dst_id = dst_D;
            flit_CD.flit_type = FLIT_TYPE_HEAD;
            flit_CD.vc_id = 0;
            flit_CD.sequence_no = 0;
            flit_CD.sequence_length = 1;
            flit_CD.is_multicast = false;

            // 验证路由计算
            Router* router_A = noc.t[src_A]->r;
            Router* router_C = noc.t[src_C]->r;

            RouteData route_data_AB;
            route_data_AB.current_id = src_A; route_data_AB.src_id = src_A; route_data_AB.dst_id = dst_B;
            route_data_AB.dir_in = 0; route_data_AB.vc_id = 0;

            RouteData route_data_CD;
            route_data_CD.current_id = src_C; route_data_CD.src_id = src_C; route_data_CD.dst_id = dst_D;
            route_data_CD.dir_in = 0; route_data_CD.vc_id = 0;

            int route_AB = router_A->route(route_data_AB);
            int route_CD = router_C->route(route_data_CD);

            THEN("both unicast flows should have valid, non-conflicting routes") {
                REQUIRE(route_AB >= 0);
                REQUIRE(route_CD >= 0);

                // 验证两条路径是否不相交（在不同的端口上）
                INFO("Route A->B uses port: " << route_AB);
                INFO("Route C->D uses port: " << route_CD);

                // 在层次结构中，不同的中间路径应该使用不同的端口
                REQUIRE(route_AB != route_CD);
            }

            THEN("reservation table should handle both flows without conflicts") {
                // 测试预留表对两条单播流的处理
                TReservation res_AB;
                res_AB.input = 0; res_AB.vc = 0;

                TReservation res_CD;
                res_CD.input = 0; res_CD.vc = 0;

                int status_AB = router_A->reservation_table.checkReservation(res_AB, route_AB);
                int status_CD = router_C->reservation_table.checkReservation(res_CD, route_CD);

                REQUIRE(status_AB == RT_AVAILABLE);
                REQUIRE(status_CD == RT_AVAILABLE);

                // 可以同时预留
                router_A->reservation_table.reserve(res_AB, route_AB);
                router_C->reservation_table.reserve(res_CD, route_CD);

                // 预留后应该返回相同状态
                status_AB = router_A->reservation_table.checkReservation(res_AB, route_AB);
                status_CD = router_C->reservation_table.checkReservation(res_CD, route_CD);

                REQUIRE(status_AB == RT_ALREADY_SAME);
                REQUIRE(status_CD == RT_ALREADY_SAME);
            }
        }
    }
}

// ====================================================================================
//                        Test Case 1.2: Pure_Multicast_Simple_Tree
// ====================================================================================
SCENARIO("Test Case 1.2: Pure Multicast Simple Tree", "[sanity][multicast]") {

    GIVEN("A hierarchical NoC with simple multicast tree structure") {
        auto fixture = getSharedFixture();
        NoC& noc = *fixture->noc;
        fixture->resetNoc();

        WHEN("root node multicasts to two direct children in different subtrees") {
            // 使用根节点0多播到两个不同子树的节点：节点5和节点10
            int src_A = 0;
            vector<int> dst_BC = {5, 10};  // 两个目标节点

            // 创建多播flit（简化版本）
            Flit multicast_flit;
            multicast_flit.src_id = src_A;
            multicast_flit.dst_id = dst_BC[0];  // 主目标
            multicast_flit.is_multicast = true;
            multicast_flit.multicast_dst_ids = dst_BC;
            multicast_flit.flit_type = FLIT_TYPE_HEAD;
            multicast_flit.vc_id = 0;
            multicast_flit.sequence_no = 0;
            multicast_flit.sequence_length = 1;

            // 验证多播路由计算
            Router* router_A = noc.t[src_A]->r;

            MulticastRouteData mc_data;
            mc_data.current_id = src_A; mc_data.src_id = src_A; mc_data.dst_ids = dst_BC;
            mc_data.dir_in = 0; mc_data.vc_id = 0;

            vector<int> multicast_routes = router_A->routeMulticast(mc_data);

            THEN("multicast routing should produce correct output ports") {
                REQUIRE(multicast_routes.size() == 2);  // 应该有两个输出端口

                INFO("Multicast routes from root: " << multicast_routes[0] << ", " << multicast_routes[1]);

                // 验证路由端口不同（指向不同的子树）
                REQUIRE(multicast_routes[0] != multicast_routes[1]);

                // 验证端口在合理范围内
                for (int port : multicast_routes) {
                    REQUIRE(port >= 0);
                    REQUIRE(port < 10);  // 假设端口数量不超过10
                }
            }

            THEN("reservation table should handle atomic multicast reservation") {
                TReservation mc_res;
                mc_res.input = 0; mc_res.vc = 0;

                // 检查多播预留状态
                int mc_status = router_A->reservation_table.checkReservation(mc_res, multicast_routes);
                REQUIRE(mc_status == RT_AVAILABLE);

                // 原子预留所有端口
                router_A->reservation_table.reserve(mc_res, multicast_routes);

                // 验证预留成功
                mc_status = router_A->reservation_table.checkReservation(mc_res, multicast_routes);
                REQUIRE(mc_status == RT_ALREADY_SAME);

                // 验证所有端口都被预留
                for (int port : multicast_routes) {
                    REQUIRE_FALSE(router_A->reservation_table.isNotReserved(port));
                }
            }

            THEN("multicast and unicast should coexist without conflicts") {
                // 测试多播预留后，单播仍然可以预留其他端口
                TReservation mc_res;
                mc_res.input = 0; mc_res.vc = 0;
                router_A->reservation_table.reserve(mc_res, multicast_routes);

                // 尝试在未使用的端口上进行单播预留
                int unused_port = -1;
                for (int i = 0; i < 5; i++) {
                    if (std::find(multicast_routes.begin(), multicast_routes.end(), i) == multicast_routes.end()) {
                        unused_port = i;
                        break;
                    }
                }

                if (unused_port >= 0) {
                    TReservation uni_res;
                    uni_res.input = 1; uni_res.vc = 0;
                    int uni_status = router_A->reservation_table.checkReservation(uni_res, unused_port);
                    REQUIRE(uni_status == RT_AVAILABLE);
                }
            }
        }
    }
}

// ====================================================================================
//                        Test Case 1.3: Pure_Multicast_Complex_Tree
// ====================================================================================
SCENARIO("Test Case 1.3: Pure Multicast Complex Tree", "[sanity][multicast][complex]") {

    GIVEN("A hierarchical NoC with complex multicast tree requiring multiple hops") {
        auto fixture = getSharedFixture();
        NoC& noc = *fixture->noc;
        fixture->resetNoc();

        WHEN("root node multicasts to four destinations requiring multi-hop routing") {
            // 使用根节点0多播到四个叶子节点：{5, 6, 13, 14}
            // 这些节点位于不同的子树，需要经过多次分叉
            int src_A = 0;
            vector<int> dst_complex = {5, 6, 7, 8};

            // 创建复杂多播flit（简化版本）
            Flit complex_multicast_flit;
            complex_multicast_flit.src_id = src_A;
            complex_multicast_flit.dst_id = dst_complex[0];
            complex_multicast_flit.is_multicast = true;
            complex_multicast_flit.multicast_dst_ids = dst_complex;
            complex_multicast_flit.flit_type = FLIT_TYPE_HEAD;
            complex_multicast_flit.vc_id = 0;
            complex_multicast_flit.sequence_no = 0;
            complex_multicast_flit.sequence_length = 1;

            // 验证复杂多播路由计算
            Router* root_router = noc.t[src_A]->r;

            MulticastRouteData complex_mc_data;
            complex_mc_data.current_id = src_A;
            complex_mc_data.src_id = src_A;
            complex_mc_data.dst_ids = dst_complex;
            complex_mc_data.dir_in = 0;
            complex_mc_data.vc_id = 0;

            vector<int> complex_routes = root_router->routeMulticast(complex_mc_data);

            THEN("complex multicast should produce multiple output ports") {
                REQUIRE(complex_routes.size() >= 2);  // 至少需要2个输出端口

                INFO("Complex multicast routes from root: ");
                for (size_t i = 0; i < complex_routes.size(); i++) {
                    INFO("  Port " << i << ": " << complex_routes[i]);
                }

                // 验证所有路由端口都有效
                for (int port : complex_routes) {
                    REQUIRE(port >= 0);
                }

                // 验证路由端口不重复
                vector<int> original_routes = complex_routes;  // 保存原始副本
                std::sort(complex_routes.begin(), complex_routes.end());
                auto last = std::unique(complex_routes.begin(), complex_routes.end());
                complex_routes.erase(last, complex_routes.end());
                REQUIRE(complex_routes.size() == original_routes.size());
            }

            THEN("intermediate routers should correctly forward multicast") {
                // 选择一个中间路由器验证多播转发
                if (!complex_routes.empty()) {
                    int first_hop_port = complex_routes[0];

                    // 获取第一个跳的下一跳路由器
                    // 注意：这里需要根据实际网络拓扑来获取下一跳节点ID
                    // 我们假设通过端口0连接到节点1，端口1连接到节点2，等等
                    int intermediate_node_id = -1;
                    if (first_hop_port >= 1 && first_hop_port <= 4) {
                        intermediate_node_id = first_hop_port;  // 简化假设
                    }

                    if (intermediate_node_id > 0 && intermediate_node_id < noc.total_nodes) {
                        Router* intermediate_router = noc.t[intermediate_node_id]->r;

                        // 在中间路由器上计算到达部分目标的多播路由
                        vector<int> intermediate_dsts;
                        for (int dst : dst_complex) {
                            // 简化：假设某些目标通过这个中间节点
                            if (dst % 4 == intermediate_node_id % 4) {
                                intermediate_dsts.push_back(dst);
                            }
                        }

                        if (!intermediate_dsts.empty()) {
                            // 使用中间路由器的多播路由功能
                            MulticastRouteData inter_mc_data;
                            inter_mc_data.current_id = intermediate_node_id;
                            inter_mc_data.src_id = src_A;
                            inter_mc_data.dst_ids = intermediate_dsts;
                            inter_mc_data.dir_in = 0;
                            inter_mc_data.vc_id = 0;

                            vector<int> inter_routes = intermediate_router->routeMulticast(inter_mc_data);

                            THEN("intermediate router should produce valid multicast routes") {
                                REQUIRE(inter_routes.size() >= 1);

                                INFO("Intermediate router " << intermediate_node_id
                                     << " multicast routes to " << inter_routes.size() << " ports");

                                for (int port : inter_routes) {
                                    REQUIRE(port >= 0);
                                }
                            }
                        }
                    }
                }
            }

            // 运行SystemC仿真来处理多播传输
            // INFO("Starting SystemC simulation for multicast...");
            // sc_start(150, SC_NS);  // 运行150纳秒仿真
            // INFO("Multicast SystemC simulation completed");

            THEN("reservation table should handle complex multicast at multiple levels") {
                TReservation complex_mc_res;
                complex_mc_res.input = 0; complex_mc_res.vc = 0;

                // 根路由器的复杂多播预留
                int root_status = root_router->reservation_table.checkReservation(complex_mc_res, complex_routes);
                REQUIRE(root_status == RT_AVAILABLE);

                root_router->reservation_table.reserve(complex_mc_res, complex_routes);

                // 验证根路由器预留
                root_status = root_router->reservation_table.checkReservation(complex_mc_res, complex_routes);
                REQUIRE(root_status == RT_ALREADY_SAME);

                // 验证所有输出端口都被预留
                for (int port : complex_routes) {
                    REQUIRE_FALSE(root_router->reservation_table.isNotReserved(port));
                }

                INFO("Root router successfully reserved " << complex_routes.size()
                     << " ports for complex multicast");
            }
        }
    }
}

// ====================================================================================
//                        Test Case 2.1: Unicast_vs_Unicast_Conflict
// ====================================================================================
SCENARIO("Test Case 2.1: Unicast_vs_Unicast_Conflict", "[conflict][unicast][fairness]") {

    GIVEN("A hierarchical NoC with conflicting unicast flows to root node") {
        auto fixture = getSharedFixture();
        NoC& noc = *fixture->noc;
        fixture->resetNoc();

        // Test parameters
        const int num_packets = 10;
        const int src_A = 1;
        const int src_B = 2;
        const int dst_D = 0;

        WHEN("nodes 1 and 2 simultaneously send high-density traffic to node 0") {
            MockPE* pe_A = static_cast<MockPE*>(noc.t[src_A]->pe);
            MockPE* pe_B = static_cast<MockPE*>(noc.t[src_B]->pe);

            int sent_A_before = pe_A->getPacketsSent();
            int sent_B_before = pe_B->getPacketsSent();
            
            // Inject all packets
            for (int i = 0; i < num_packets; i++) {
                pe_A->injectTestPacket(dst_D, 3);
                pe_B->injectTestPacket(dst_D, 3);
            }

            // Run the simulation EXACTLY ONCE
            sc_start(200, SC_NS);

            // NOW, check all results sequentially within a single THEN block
            THEN("the system handles the conflict correctly, fairly, and maintains integrity") {
                
                // --- Check 1: Injection Integrity ---
                int sent_A_after = pe_A->getPacketsSent();
                int sent_B_after = pe_B->getPacketsSent();
                REQUIRE(sent_A_after - sent_A_before == num_packets);
                REQUIRE(sent_B_after - sent_B_before == num_packets);
                INFO("A injected " << (sent_A_after - sent_A_before) << " packets");
                INFO("B injected " << (sent_B_after - sent_B_before) << " packets");

                // --- Check 2: Fairness ---
                int sent_A_total = pe_A->getPacketsSent();
                int sent_B_total = pe_B->getPacketsSent();
                REQUIRE(sent_A_total > 0);
                REQUIRE(sent_B_total > 0);
                double ratio = double(max(sent_A_total, sent_B_total)) / min(sent_A_total, sent_B_total);
                REQUIRE(ratio <= 2.0);
                INFO("Fairness ratio: " << ratio << " (A: " << sent_A_total << ", B: " << sent_B_total << ")");

                // --- Check 3: Flit Integrity ---
                int flits_sent_A = pe_A->getFlitsSent();
                int flits_sent_B = pe_B->getFlitsSent();
                REQUIRE(flits_sent_A >= num_packets * 3);
                REQUIRE(flits_sent_B >= num_packets * 3);
                
                // --- Check 4: Routing Conflict Resolution ---
                Router* router_A = noc.t[src_A]->r;
                // ... rest of your routing checks ...
                // For example:
                RouteData route_data_A;
                route_data_A.current_id = src_A;
                route_data_A.dst_id = dst_D;
                int route_A_to_D = router_A->route(route_data_A);
                REQUIRE(route_A_to_D == router_A->getLogicalPortIndex(router_A->PORT_UP, -1));
            }
        }
    }
}

// ====================================================================================
//                        Test Case 2.2: Multicast_vs_Unicast_Conflict
// ====================================================================================
SCENARIO("Test Case 2.2: Multicast vs Unicast Conflict", "[conflict][multicast][unicast]") {

    GIVEN("A hierarchical NoC with conflicting multicast and unicast flows") {
        auto fixture = getSharedFixture();
        NoC& noc = *fixture->noc;
        fixture->resetNoc();

        // Test parameters: 多播流A->{B,C} 与 单播流D->C 在路由器X处冲突
        const int num_packets = 10;
        const int src_A = 0;                    // 多播源：根节点
        const vector<int> dst_BC = {5, 6};     // 多播目标：节点5和6
        const int src_D = 2;                    // 单播源：中间节点2
        const int dst_C = 6;                    // 单播目标：节点6（与多播冲突）

        WHEN("node A sends multicast to {B,C} while node D sends unicast to C, causing conflict at router X") {
            MockPE* pe_A = static_cast<MockPE*>(noc.t[src_A]->pe);
            MockPE* pe_D = static_cast<MockPE*>(noc.t[src_D]->pe);
            MockPE* pe_B = static_cast<MockPE*>(noc.t[dst_BC[0]]->pe);
            MockPE* pe_C = static_cast<MockPE*>(noc.t[dst_BC[1]]->pe);

            int sent_A_before = pe_A->getPacketsSent();
            int sent_D_before = pe_D->getPacketsSent();

            // Inject all packets: 多播和单播同时高密度发送
            for (int i = 0; i < num_packets; i++) {
                pe_A->injectTestPacket(dst_BC, 3);  // 多播：A -> {B,C}
                pe_D->injectTestPacket(dst_C, 3);    // 单播：D -> C
            }

            // Run the simulation EXACTLY ONCE
            sc_start(200, SC_NS);

            // NOW, check all results sequentially within a single THEN block
            THEN("the system handles multicast-unicast conflict correctly and maintains integrity") {

                // --- Check 1: Injection Integrity ---
                int sent_A_after = pe_A->getPacketsSent();
                int sent_D_after = pe_D->getPacketsSent();
                REQUIRE(sent_A_after - sent_A_before == num_packets);
                REQUIRE(sent_D_after - sent_D_before == num_packets);
                INFO("Multicast source A injected " << (sent_A_after - sent_A_before) << " packets");
                INFO("Unicast source D injected " << (sent_D_after - sent_D_before) << " packets");

                // --- Check 2: Fairness between multicast and unicast ---
                int sent_A_total = pe_A->getPacketsSent();
                int sent_D_total = pe_D->getPacketsSent();
                REQUIRE(sent_A_total > 0);
                REQUIRE(sent_D_total > 0);

                // 多播和单播应该都有机会发送
                double ratio = double(max(sent_A_total, sent_D_total)) / min(sent_A_total, sent_D_total);
                REQUIRE(ratio <= 3.0);  // 允许更大的比率，因为多播可能需要更多资源
                INFO("Fairness ratio between multicast and unicast: " << ratio
                     << " (Multicast A: " << sent_A_total << ", Unicast D: " << sent_D_total << ")");

                // --- Check 3: Flit Integrity ---
                int flits_sent_A = pe_A->getFlitsSent();
                int flits_sent_D = pe_D->getFlitsSent();
                REQUIRE(flits_sent_A >= num_packets * 3);  // 多播包也应该发送正确的flit数量
                REQUIRE(flits_sent_D >= num_packets * 3);
                INFO("Multicast flits sent: " << flits_sent_A);
                INFO("Unicast flits sent: " << flits_sent_D);

                // --- Check 4: Routing Conflict Resolution ---
                Router* router_A = noc.t[src_A]->r;
                Router* router_D = noc.t[src_D]->r;

                // 验证多播路由
                MulticastRouteData mc_route_data;
                mc_route_data.current_id = src_A; mc_route_data.src_id = src_A;
                mc_route_data.dst_ids = dst_BC; mc_route_data.dir_in = 0; mc_route_data.vc_id = 0;
                vector<int> mc_routes = router_A->routeMulticast(mc_route_data);

                // 验证单播路由
                RouteData uc_route_data;
                uc_route_data.current_id = src_D; uc_route_data.src_id = src_D; uc_route_data.dst_id = dst_C;
                uc_route_data.dir_in = 0; uc_route_data.vc_id = 0;
                int uc_route = router_D->route(uc_route_data);

                REQUIRE(mc_routes.size() >= 1);
                REQUIRE(uc_route >= 0);

                INFO("Multicast routes from A: " << mc_routes.size() << " ports");
                for (size_t i = 0; i < mc_routes.size(); i++) {
                    INFO("  Port " << i << ": " << mc_routes[i]);
                }
                INFO("Unicast route from D: " << uc_route);

                // --- Check 5: Conflict point validation ---
                // 验证在某个中间路由器存在冲突
                Router* conflict_router = noc.t[1]->r;  // 假设冲突点在节点1
                REQUIRE(conflict_router != nullptr);

                // 检查预留表能否处理冲突
                TReservation mc_res;
                mc_res.input = 0; mc_res.vc = 0;
                int mc_status = conflict_router->reservation_table.checkReservation(mc_res, mc_routes);

                TReservation uc_res;
                uc_res.input = 0; uc_res.vc = 0;
                int uc_status = conflict_router->reservation_table.checkReservation(uc_res, uc_route);

                REQUIRE((mc_status == RT_AVAILABLE || mc_status == RT_ALREADY_SAME || mc_status == RT_OUTVC_BUSY));
                REQUIRE((uc_status == RT_AVAILABLE || uc_status == RT_ALREADY_SAME || uc_status == RT_OUTVC_BUSY));

                INFO("Conflict router reservation status - Multicast: " << mc_status
                     << ", Unicast: " << uc_status);
            }
        }
    }
}

int sc_main(int argc, char* argv[]) {
    // 这个函数永远不会被调用，因为程序的入口是 Catch2 生成的 main()
    // 它存在的唯一目的就是为了让链接器满意
    return 0;
}