CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -Iinclude
SRC_DIR = src
BUILD_DIR = build
TARGET = kernel_sim

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))

all: $(TARGET)

all: $(TARGET)

$(TARGET): src/main.cpp include/pcb.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp -o $@

clean:
	rm -f $(TARGET)
