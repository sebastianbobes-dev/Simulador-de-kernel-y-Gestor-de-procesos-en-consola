CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -pthread

OBJECTS = process_manager.o

.PHONY: all clean

all: libkernel_base.a

libkernel_base.a: $(OBJECTS)
	ar rcs $@ $^

process_manager.o: process_manager.cpp process_manager.hpp pcb.hpp
	$(CXX) $(CXXFLAGS) -c process_manager.cpp -o $@

clean:
	rm -f $(OBJECTS) libkernel_base.a
