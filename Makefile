CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -pthread
CPPFLAGS ?= -Iinclude

TARGET = simulador

.PHONY: all clean

all: $(TARGET)

$(TARGET): src/main.cpp include/pcb.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp -o $@

clean:
	rm -f $(TARGET)
