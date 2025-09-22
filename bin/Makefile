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
INCDIR := -I$(SRCDIR) -I$(SRCDIR)/smartbuffer -isystem $(SYSTEMC)/include -I$(YAML)/include
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


##### 3. 更新辅助规则 #####

# 默认的 'all' 目标，现在编译 noxim 和我们的测试程序
all: $(MODULE) $(TEST_MODULE_HIERARCHICAL)

# 新的 clean 规则，确保能清理所有文件
# 注意：直接替换掉你旧的 clean 规则
clean:
	rm -f $(OBJS) $(MODULE) *~ $(TEST_HIERARCHICAL_MAIN_OBJ) $(TEST_MODULE_HIERARCHICAL)