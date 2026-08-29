CXX = g++
CXXFLAGS = -std=c++17  -Wall -Wextra -g
OUTDIR = ./out
BIN = ./bin

# 默认目标
all: $(BIN)/main

# 确保输出目录存在 (Order-only 依赖)
$(OUTDIR) $(BIN):
	mkdir -p $@

$(BIN)/main: main.cpp $(OUTDIR)/TimeWheel.o | $(BIN)
	$(CXX) $(CXXFLAGS) main.cpp $(OUTDIR)/TimeWheel.o -o $(BIN)/main

$(OUTDIR)/TimeWheel.o: TimeWheel.cpp TimeWheel.h | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -c TimeWheel.cpp -o $(OUTDIR)/TimeWheel.o

# 清理规则
clean:
	rm -rf $(OUTDIR) $(BIN)

# 声明为目标, 防止与同名文件冲突
.PHONY: all clean
