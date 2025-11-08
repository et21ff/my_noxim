cmake_minimum_required(VERSION 3.9)
project(noxim)
 
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-pthread")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-pthread")
set(CMAKE_MODULE_LINKER_FLAGS "-pthread")
 
# add extra include directories
include_directories(
        bin/libs/systemc-2.3.1/include
        bin/libs/yaml-cpp/include
)
 
# add extra lib directories
link_directories(
        bin/libs/systemc-2.3.1/lib-linux64
        bin/libs/yaml-cpp/lib
)
 
add_definitions(-DSC_NO_WRITE_CHECK)
 
# 主要的noxim可执行文件
add_executable(noxim
        src/routingAlgorithms/Routing_DELTA.cpp
        src/routingAlgorithms/Routing_DELTA.h
        src/routingAlgorithms/Routing_NEGATIVE_FIRST.cpp
        src/routingAlgorithms/Routing_NEGATIVE_FIRST.h
        src/routingAlgorithms/Routing_NORTH_LAST.cpp
        src/routingAlgorithms/Routing_NORTH_LAST.h
        src/routingAlgorithms/Routing_ODD_EVEN.cpp
        src/routingAlgorithms/Routing_ODD_EVEN.h
        src/routingAlgorithms/Routing_TABLE_BASED.cpp
        src/routingAlgorithms/Routing_TABLE_BASED.h
        src/routingAlgorithms/Routing_WEST_FIRST.cpp
        src/routingAlgorithms/Routing_WEST_FIRST.h
        src/routingAlgorithms/Routing_XY.cpp
        src/routingAlgorithms/Routing_XY.h
        src/routingAlgorithms/RoutingAlgorithm.h
        src/routingAlgorithms/RoutingAlgorithms.cpp
        src/routingAlgorithms/RoutingAlgorithms.h
        src/Buffer.cpp
        src/Buffer.h
        src/Channel.cpp
        src/Channel.h
        src/ConfigurationManager.cpp
        src/ConfigurationManager.h
        src/DataStructs.h
        src/DataTypes.h
        src/GlobalParams.cpp
        src/GlobalParams.h
        src/GlobalRoutingTable.cpp
        src/GlobalRoutingTable.h
        src/GlobalStats.cpp
        src/GlobalStats.h
        src/GlobalTrafficTable.cpp
        src/GlobalTrafficTable.h
        src/Hub.cpp
        src/Hub.h
        src/Initiator.cpp
        src/Initiator.h
        src/LocalRoutingTable.cpp
        src/LocalRoutingTable.h
        src/Main.cpp
        src/MM.cpp
        src/MM.h
        src/MockPE.cpp
        src/MockPE.h
        src/NoC.cpp
        src/NoC.h
        src/Power.cpp
        src/Power.h
        src/ProcessingElement.cpp
        src/ProcessingElement.h
        src/ReservationTable.cpp
        src/ReservationTable.h
        src/Router.cpp
        src/Router.h
        src/Stats.cpp
        src/Stats.h
        src/Target.cpp
        src/Target.h
        src/Tile.cpp
        src/Tile.h
        src/TokenRing.cpp
        src/TokenRing.h
        src/Utils.h
)
 
# GLB Tile测试可执行文件
add_executable(test_glb_tile
        test/test_glb_tile.cpp
        src/Buffer.cpp
        src/Buffer.h
        src/Channel.cpp
        src/Channel.h
        src/ConfigurationManager.cpp
        src/ConfigurationManager.h
        src/DataStructs.h
        src/DataTypes.h
        src/GlobalParams.cpp
        src/GlobalParams.h
        src/GlobalRoutingTable.cpp
        src/GlobalRoutingTable.h
        src/GlobalStats.cpp
        src/GlobalStats.h
        src/GlobalTrafficTable.cpp
        src/GlobalTrafficTable.h
        src/Hub.cpp
        src/Hub.h
        src/MM.cpp
        src/MM.h
        src/MockPE.cpp
        src/MockPE.h
        src/NoC.cpp
        src/NoC.h
        src/Power.cpp
        src/Power.h
        src/ProcessingElement.cpp
        src/ProcessingElement.h
        src/ReservationTable.cpp
        src/ReservationTable.h
        src/Router.cpp
        src/Router.h
        src/Stats.cpp
        src/Stats.h
        src/Tile.cpp
        src/Tile.h
        src/TokenRing.cpp
        src/TokenRing.h
        src/Utils.h
        # 路由算法依赖
        src/routingAlgorithms/RoutingAlgorithm.h
        src/routingAlgorithms/RoutingAlgorithms.cpp
        src/routingAlgorithms/RoutingAlgorithms.h
        src/routingAlgorithms/Routing_XY.cpp
        src/routingAlgorithms/Routing_XY.h
)
 
# 链接库
target_link_libraries(noxim yaml-cpp.a systemc.a)
target_link_libraries(test_glb_tile yaml-cpp.a systemc.a)
 
# 可选：为测试添加编译标志
target_compile_definitions(test_glb_tile PRIVATE -DSC_NO_WRITE_CHECK)