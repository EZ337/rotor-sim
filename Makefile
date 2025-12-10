# Makefile for RotorNet Packet Simulator

CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra
TARGET = run_rotornet_simulator

# Source and header files
SOURCES = main.cpp
HEADERS = config.h flow.h workload_generator.h topology.h stats.h simulator.h

# Build target
$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

# Debug build
debug: CXXFLAGS = -std=c++17 -g -Wall -Wextra
debug: $(TARGET)

# Clean
clean:
	rm -f $(TARGET) *.o results.csv

# Run with default config
run: $(TARGET)
	./$(TARGET)

# Run with custom config
run-config: $(TARGET)
	./$(TARGET) config.txt

.PHONY: clean run run-config debug
