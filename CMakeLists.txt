cmake_minimum_required(VERSION 3.9)
project(noxim)
 
# 设置默认构建类型为Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
 
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-pthread")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-pthread")
set(CMAKE_MODULE_LINKER_FLAGS "-pthread")
 
# 可选：添加编译器特定的Debug标志
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
    endif()
endif()
 
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
 
add_executable(noxim
        # 核心文件
        src/Main.cpp
        src/NoC.cpp
        src/NoC.h
        src/Tile.cpp
        src/Tile.h
        src/Router.cpp
        src/Router.h
        src/ProcessingElement.cpp
        src/ProcessingElement.h
        src/Buffer.cpp
        src/Buffer.h
        src/Channel.cpp
        src/Channel.h
        
        # 全局组件
        src/GlobalParams.cpp
        src/GlobalParams.h
        src/GlobalRoutingTable.cpp
        src/GlobalRoutingTable.h
        src/GlobalTrafficTable.cpp
        src/GlobalTrafficTable.h
        src/GlobalStats.cpp
        src/GlobalStats.h
        
        # 配置和管理
        src/ConfigurationManager.cpp
        src/ConfigurationManager.h
        src/DataStructs.h
        src/DataTypes.h
        src/Stats.cpp
        src/Stats.h
        src/Power.cpp
        src/Power.h
        
        # 其他组件
        src/Hub.cpp
        src/Hub.h
        src/Initiator.cpp
        src/Initiator.h
        src/Target.cpp
        src/Target.h
        src/MM.cpp
        src/MM.h
        src/ReservationTable.cpp
        src/ReservationTable.h
        src/LocalRoutingTable.cpp
        src/LocalRoutingTable.h
        # src/MockPE.cpp
        # src/MockPE.h
        src/TokenRing.cpp
        src/TokenRing.h
        src/Utils.h
        
        # 路由算法
        src/routingAlgorithms/RoutingAlgorithm.h
        src/routingAlgorithms/RoutingAlgorithms.cpp
        src/routingAlgorithms/RoutingAlgorithms.h
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
        
        # 任务管理器
        src/taskmanager/TaskManager.cpp
        src/taskmanager/TaskManager.h
        src/taskmanager/ConfigParser.h
        src/taskmanager/WorkloadStructs.h
        
        # 智能缓冲区
        src/smartbuffer/BufferManager.cpp
        src/smartbuffer/BufferManager.h
        src/smartbuffer/ScheduleFactory.cpp
        src/smartbuffer/ScheduleFactory.h
)

target_link_libraries(noxim yaml-cpp.a systemc.a)

add_executable(test_glb_tile
        # 核心文件
        tests/test_glb_tile.cpp
        src/NoC.cpp
        src/NoC.h
        src/Tile.cpp
        src/Tile.h
        src/Router.cpp
        src/Router.h
        src/ProcessingElement.cpp
        src/ProcessingElement.h
        src/Buffer.cpp
        src/Buffer.h
        src/Channel.cpp
        src/Channel.h
        
        # 全局组件
        src/GlobalParams.cpp
        src/GlobalParams.h
        src/GlobalRoutingTable.cpp
        src/GlobalRoutingTable.h
        src/GlobalTrafficTable.cpp
        src/GlobalTrafficTable.h
        src/GlobalStats.cpp
        src/GlobalStats.h
        
        # 配置和管理
        src/ConfigurationManager.cpp
        src/ConfigurationManager.h
        src/DataStructs.h
        src/DataTypes.h
        src/Stats.cpp
        src/Stats.h
        src/Power.cpp
        src/Power.h
        src/HierarchicalTopologyManager.cpp
        src/HierarchicalTopologyManager.h
        
        # 其他组件
        src/Hub.cpp
        src/Hub.h
        src/Initiator.cpp
        src/Initiator.h
        src/Target.cpp
        src/Target.h
        src/MM.cpp
        src/MM.h
        src/ReservationTable.cpp
        src/ReservationTable.h
        src/LocalRoutingTable.cpp
        src/LocalRoutingTable.h
        # src/MockPE.cpp
        # src/MockPE.h
        src/TokenRing.cpp
        src/TokenRing.h
        src/Utils.h
        
        # 路由算法
        src/routingAlgorithms/RoutingAlgorithm.h
        src/routingAlgorithms/RoutingAlgorithms.cpp
        src/routingAlgorithms/RoutingAlgorithms.h
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
        
        # 任务管理器
        src/taskmanager/TaskManager.cpp
        src/taskmanager/TaskManager.h
        src/taskmanager/ConfigParser.h
        src/taskmanager/WorkloadStructs.h
        
        # 智能缓冲区
        src/smartbuffer/BufferManager.cpp
        src/smartbuffer/BufferManager.h
        src/smartbuffer/ScheduleFactory.cpp
        src/smartbuffer/ScheduleFactory.h
)

target_link_libraries(test_glb_tile yaml-cpp.a systemc.a)
