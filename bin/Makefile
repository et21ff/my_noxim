##### LIBRARIES CONFIGURATION #####

SYSTEMC := /root/my_noxim/bin/libs/systemc-2.3.1/install_debug
YAML    := libs/yaml-cpp


##### DIRECTORIES #####

SYSTEMC_LIBS := $(wildcard $(SYSTEMC)/lib-*)

SRCDIR  := ../src
SYSTEMC_LIBS := $(SYSTEMC)/lib-linux64
OBJDIR  := ./build

SRCS := $(shell find $(SRCDIR) -name '*.cpp')
OBJS := $(subst $(SRCDIR),$(OBJDIR),$(SRCS:.cpp=.o))

MODULE := noxim


##### COMPILER OPTIONS #####

CXX      := g++
OPT      := -O0 
# to enable logs with LOG macro, uncomment "-g DDEBUG" in the line below
# and recompile everythin (make clean)
DEBUG    :=  -g -DDEBUG
OTHER    := -Wall -DSC_NO_WRITE_CHECK --std=c++11 # -Wno-deprecated
CXXFLAGS := $(OPT) $(OTHER) $(DEBUG)

LDFLAGS  := -Wl,-rpath,$(SYSTEMC_LIBS)
INCDIR := -I$(SRCDIR) -I$(SRCDIR)/smartbuffer -isystem $(SYSTEMC)/include -I$(YAML)/include -I$(SRCDIR)/taskmanager
LIBDIR := -L$(SRCDIR) -L$(SYSTEMC_LIBS) -L$(YAML)/lib

LIBS := -lsystemc -lm -lyaml-cpp

SPACE := $(subst ,, )
VPATH := $(SRCDIR):$(subst $(SPACE),:,$(SUBDIRS))

##### RULES #####

$(MODULE): $(OBJS)
	$(CXX) $(LIBDIR) $^  $(LDFLAGS) $(LIBS) -o $@ 2>&1 | c++filt

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCDIR) -c $< -o $@

clean:
	rm -f $(OBJS) $(MODULE) *~

depend: 
	makedepend $(SRCS) -Y -f- 2>/dev/null | sed 's_$(SRCDIR)_$(OBJDIR)_' > Makefile.deps

-include Makefile.deps

# --------------------------------------------------------------------
#               #####  TESTBENCH FOR HIERARCHICAL  #####
# --------------------------------------------------------------------

##### 1. 定义测试相关的文件 #####

# 测试可执行文件的名字
TEST_MODULE_HIERARCHICAL := test_hierarchical

# 测试程序的源文件 (它有自己的 main 函数)
TEST_HIERARCHICAL_MAIN_SRC := ../tests/test_hierarchical.cpp
TEST_HIERARCHICAL_MAIN_OBJ := $(subst ../tests,$(OBJDIR),$(TEST_HIERARCHICAL_MAIN_SRC:.cpp=.o))

# **[核心修正]**
# 定义测试程序不需要的 .o 文件列表
# 我们只需要排除 Main.o，因为它包含重复的 sc_main
OBJS_TO_EXCLUDE_FOR_TEST := $(OBJDIR)/Main.o

# 从完整的 .o 文件列表中，过滤掉我们不需要的
OBJS_FOR_TEST := $(filter-out $(OBJS_TO_EXCLUDE_FOR_TEST), $(OBJS))
# 然后加上我们自己的测试主程序 .o 文件
TEST_HIERARCHICAL_OBJS := $(OBJS_FOR_TEST) $(TEST_HIERARCHICAL_MAIN_OBJ)


##### 2. 编写新的编译规则 #####

# 为位于 ../tests 目录下的源文件，添加专门的编译规则
$(OBJDIR)/%.o: ../tests/%.cpp
	@echo "--- Compiling test source: $< ---"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCDIR) -c $< -o $@

# 链接测试可执行文件的规则
$(TEST_MODULE_HIERARCHICAL): $(TEST_HIERARCHICAL_OBJS)
	@echo "--- Linking test executable: $(TEST_MODULE_HIERARCHICAL) ---"
	$(CXX) $(LIBDIR) $^ $(LDFLAGS) $(LIBS) -o $@ 2>&1 | c++filt


# --------------------------------------------------------------------
#               #####  TESTBENCH FOR RESERVATION TABLE  #####
# --------------------------------------------------------------------

##### 1. 定义 ReservationTable 测试相关的文件 #####

# 测试可执行文件的名字
TEST_MODULE_RESERVATION_TABLE := test_reservation_table

# 测试程序的源文件 (它使用 CATCH framework)
TEST_RESERVATION_TABLE_MAIN_SRC := ../tests/test_reservation_table.cpp
TEST_RESERVATION_TABLE_MAIN_OBJ := $(subst ../tests,$(OBJDIR),$(TEST_RESERVATION_TABLE_MAIN_SRC:.cpp=.o))

# 对于 ReservationTable 测试，我们只需要最基本的组件
OBJS_FOR_RES_TEST := $(OBJDIR)/ReservationTable.o
# 然后加上我们自己的测试主程序 .o 文件
TEST_RESERVATION_TABLE_OBJS := $(OBJS_FOR_RES_TEST) $(TEST_RESERVATION_TABLE_MAIN_OBJ)

##### 2. 为 ReservationTable 测试编写特殊的编译规则 #####

# 为 ReservationTable 测试创建特殊的编译规则（包含 SystemC）
$(OBJDIR)/test_reservation_table.o: ../tests/test_reservation_table.cpp
	@echo "--- Compiling ReservationTable test: $< ---"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCDIR) -c $< -o $@

# 编译测试所需的简单对象文件
$(OBJDIR)/ReservationTable.o: $(SRCDIR)/ReservationTable.cpp
	@echo "--- Compiling ReservationTable: $< ---"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCDIR) -c $< -o $@


# 链接 ReservationTable 测试可执行文件的规则（包含 SystemC 库）
$(TEST_MODULE_RESERVATION_TABLE): $(TEST_RESERVATION_TABLE_OBJS)
	@echo "--- Linking ReservationTable test executable: $(TEST_MODULE_RESERVATION_TABLE) ---"
	$(CXX) $(LIBDIR) $^ $(LDFLAGS) $(LIBS) -o $@ 2>&1 | c++filt



# ##############################################################################
# TESTING CONFIGURATION (for ConfigParser only)
# ##############################################################################

# 1. 定义测试可执行文件的名字
TEST_MODULE_CONFIG_PARSER := test_config_parser

# 2. 定义测试程序的源文件和对应的目标文件
TEST_CONFIG_PARSER_MAIN_SRC := ../tests/test_ConfigParser.cpp
TEST_CONFIG_PARSER_MAIN_OBJ := $(subst ../tests,$(OBJDIR),$(TEST_CONFIG_PARSER_MAIN_SRC:.cpp=.o))

# 3. 定义构建测试程序所需的所有 .o 文件
#    (当前只需要测试主程序的 .o 文件，因为 ConfigParser.h 是纯头文件)
TEST_CONFIG_PARSER_OBJS := $(TEST_CONFIG_PARSER_MAIN_OBJ)



# ##############################################################################
# RULES FOR test_config_parser
# ##############################################################################

# --- 1. 新增一个目标来构建并运行测试 ---
test_parser: $(TEST_MODULE_CONFIG_PARSER)
	@echo "\n--- Running ConfigParser tests ---"
	./$(TEST_MODULE_CONFIG_PARSER)
	@echo "--- ConfigParser tests completed ---\n"


# --- 2. ConfigParser 测试主程序的编译规则 ---
#    (它需要能够找到 systemc.h 和 yaml.h)
$(TEST_CONFIG_PARSER_MAIN_OBJ): $(TEST_CONFIG_PARSER_MAIN_SRC)
	@echo "--- Compiling ConfigParser test: $< ---"
	@mkdir -p $(dir $@)
	# 使用全局的 CXXFLAGS 和 INCDIR 来确保所有头文件都能被找到
	$(CXX) $(CXXFLAGS) $(INCDIR) -I../tests/include -c $< -o $@


# --- 3. ConfigParser 测试可执行文件的链接规则 ---
#    (它需要链接 systemc 和 yaml-cpp 库)
$(TEST_MODULE_CONFIG_PARSER): $(TEST_CONFIG_PARSER_OBJS)
	@echo "--- Linking ConfigParser test executable: $(TEST_MODULE_CONFIG_PARSER) ---"
	# 使用全局的 LIBDIR 和 LIBS 来确保所有库都被链接
	$(CXX) $(LIBDIR) $^ $(LDFLAGS) $(LIBS) -o $@ 2>&1 | c++filt



# --------------------------------------------------------------------
#               #####  TESTBENCH FOR TASK MANAGER  #####
# --------------------------------------------------------------------

# 测试可执行文件的名字
TEST_MODULE_TASK_MANAGER := test_task_manager

# 测试程序的源文件和对应的目标文件
TEST_TASK_MANAGER_MAIN_SRC := ../tests/test_TaskManager.cpp
TEST_TASK_MANAGER_MAIN_OBJ := $(subst ../tests,$(OBJDIR),$(TEST_TASK_MANAGER_MAIN_SRC:.cpp=.o))

# TaskManager 测试需要链接 TaskManager.o
# 我们定义 TaskManager 的源文件和目标文件
TASK_MANAGER_SRC := $(SRCDIR)/taskmanager/TaskManager.cpp
TASK_MANAGER_OBJ := $(subst $(SRCDIR),$(OBJDIR),$(TASK_MANAGER_SRC:.cpp=.o))

# 构建测试程序所需的所有 .o 文件
TEST_TASK_MANAGER_OBJS := $(TASK_MANAGER_OBJ) $(TEST_TASK_MANAGER_MAIN_OBJ)




# ##############################################################################
# RULES FOR test_task_manager
# ##############################################################################

# --- 1. 新增一个目标来构建并运行测试 ---
test_taskmanager: $(TEST_MODULE_TASK_MANAGER)
	@echo "\n--- Running TaskManager tests ---"
	./$(TEST_MODULE_TASK_MANAGER)
	@echo "--- TaskManager tests completed ---\n"


# --- 2. TaskManager 测试主程序的编译规则 ---
$(TEST_TASK_MANAGER_MAIN_OBJ): $(TEST_TASK_MANAGER_MAIN_SRC)
	@echo "--- Compiling TaskManager test: $< ---"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCDIR) -I../tests/include -c $< -o $@


# --- 3. TaskManager 模块自身的编译规则 ---
#    (它可能已经包含在全局的 $(OBJS) 规则中，但为了清晰和独立，
#     我们在这里提供一个明确的规则)
$(TASK_MANAGER_OBJ): $(TASK_MANAGER_SRC)
	@echo "--- Compiling TaskManager module: $< ---"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCDIR) -c $< -o $@


# --- 4. TaskManager 测试可执行文件的链接规则 ---
$(TEST_MODULE_TASK_MANAGER): $(TEST_TASK_MANAGER_OBJS)
	@echo "--- Linking TaskManager test executable: $(TEST_MODULE_TASK_MANAGER) ---"
	$(CXX) $(LIBDIR) $^ $(LDFLAGS) $(LIBS) -o $@ 2>&1 | c++filt


# 测试可执行文件的名字
TEST_MODULE_PE := test_pe

# 测试程序的源文件和对应的目标文件
TEST_PE_MAIN_SRC := ../tests/test_PE_smoke_test.cpp
TEST_PE_MAIN_OBJ := $(subst ../tests,$(OBJDIR),$(TEST_PE_MAIN_SRC:.cpp=.o))

# [核心修改] 构建 PE 测试所需的所有 .o 文件
# 我们需要除了 main.o 之外的所有主程序对象文件

# 1. 首先，定义 main.o 的路径
MAIN_OBJ := $(OBJDIR)/Main.o

# 2. 使用 filter-out 函数，从 $(OBJS) 列表中剔除 main.o
OBJS_WITHOUT_MAIN := $(filter-out $(MAIN_OBJ),$(OBJS))

# 3. 最终的依赖列表 = (所有.o - main.o) + 测试主程序的.o
TEST_PE_OBJS := $(OBJS_WITHOUT_MAIN) $(TEST_PE_MAIN_OBJ)


# ... (在 ##### RULES ##### 部分，编译和链接规则保持不变) ...

# ##############################################################################
# RULES FOR test_pe
# ##############################################################################

# test_pe 目标 (保持不变)
test_pe: $(TEST_MODULE_PE)
	@echo "\n--- Running ProcessingElement module tests ---"
	./$(TEST_MODULE_PE)
	@echo "--- ProcessingElement tests completed ---\n"


# PE 测试主程序的编译规则 (保持不变)
$(TEST_PE_MAIN_OBJ): $(TEST_PE_MAIN_SRC)
	@echo "--- Compiling PE testbench: $< ---"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCDIR) -I../tests/include -c $< -o $@


# PE 测试可执行文件的链接规则 (保持不变)
# 它现在会自动使用我们上面定义的、不包含 main.o 的 $(TEST_PE_OBJS)
$(TEST_MODULE_PE): $(TEST_PE_OBJS)
	@echo "--- Linking PE test executable: $(TEST_MODULE_PE) ---"
	$(CXX) $(LIBDIR) $^ $(LDFLAGS) $(LIBS) -o $@ 2>&1 | c++filt


 
##### 1. 定义 GLB Tile 测试相关的文件 #####
 
# 测试可执行文件的名字
TEST_MODULE_GLB_TILE := test_glb_tile
 
# 测试程序的源文件
TEST_GLB_TILE_MAIN_SRC := ../tests/test_glb_tile.cpp
TEST_GLB_TILE_MAIN_OBJ := $(subst ../tests,$(OBJDIR),$(TEST_GLB_TILE_MAIN_SRC:.cpp=.o))
 
# 排除包含重复 sc_main 的文件
OBJS_TO_EXCLUDE_FOR_GLB_TEST := $(OBJDIR)/Main.o
 
# 从完整的 .o 文件列表中过滤掉不需要的，然后添加测试主程序
OBJS_FOR_GLB_TEST := $(filter-out $(OBJS_TO_EXCLUDE_FOR_GLB_TEST), $(OBJS))
TEST_GLB_TILE_OBJS := $(OBJS_FOR_GLB_TEST) $(TEST_GLB_TILE_MAIN_OBJ)
 
##### 2. 编写编译和链接规则 #####
 
# 为测试源文件添加编译规则
$(OBJDIR)/%.o: ../tests/%.cpp
	@echo "--- Compiling test source: $< ---"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCDIR) -c $< -o $@
 
# 链接 GLB Tile 测试可执行文件
$(TEST_MODULE_GLB_TILE): $(TEST_GLB_TILE_OBJS)
	@echo "--- Linking GLB Tile test executable: $(TEST_MODULE_GLB_TILE) ---"
	$(CXX) $(LIBDIR) $^ $(LDFLAGS) $(LIBS) -o $@ 2>&1 | c++filt
 
# 便捷目标：构建并运行测试
test_glb_tile: $(TEST_MODULE_GLB_TILE)
	@echo "\n--- Running GLB Tile tests ---"
	@echo "--- GLB Tile tests completed ---\n"
 
##### 3. 更新辅助规则 #####
 
# 更新 all 目标，包含新的测试程序
all: $(MODULE) $(TEST_MODULE_HIERARCHICAL) $(TEST_MODULE_RESERVATION_TABLE) $(TEST_MODULE_GLB_TILE)
 
# 更新 clean 规则
clean:
	rm -f $(OBJS) $(MODULE) *~ $(TEST_HIERARCHICAL_MAIN_OBJ) $(TEST_MODULE_HIERARCHICAL) $(TEST_RESERVATION_TABLE_MAIN_OBJ) $(TEST_MODULE_RESERVATION_TABLE)
	rm -f $(TEST_CONFIG_PARSER_OBJS) $(TEST_MODULE_CONFIG_PARSER)
	rm -f $(TEST_TASK_MANAGER_OBJS) $(TEST_MODULE_TASK_MANAGER)
	rm -f $(TEST_PE_MAIN_OBJ) $(TEST_MODULE_PE)
	rm -f $(TEST_GLB_TILE_MAIN_OBJ) $(TEST_MODULE_GLB_TILE)