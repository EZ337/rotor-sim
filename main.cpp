// main.cpp - Entry point for RotorNet simulator
#include <iostream>
#include <memory>
#include "simulator.h"
#include "config.h"
#include "stats.h"

int main(int argc, char* argv[]) {
    try {
        // Load configuration
        SimConfig config;
        if (argc > 1) {
            config.loadFromFile(argv[1]);
        } else {
            config.setDefaults();
        }
        
        std::cout << "RotorNet Packet Simulator" << std::endl;
        std::cout << "=========================" << std::endl;
        config.print();
        
        // Create and run simulator
        Simulator sim(config);
        sim.run();
        
        // Print statistics
        Statistics stats = sim.getStatistics();
        stats.print();
        stats.saveToFile("results.csv");
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
