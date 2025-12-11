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
        std::string flowCsv = "";
        std::string saveName = "results.csv";
        
        // Parse command-line arguments
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "-f" && i + 1 < argc) {
                flowCsv = argv[++i];
            } else if (arg == "-o" && i + 1 < argc) {
                saveName = argv[++i];
            }
        }
        
        // Load configuration from file or use defaults
        if (!flowCsv.empty()) {
            config.loadFromFile(flowCsv);
        } else {
            std::cout << "Usage: " << argv[0] << " -f [flowcsv] -o [outputCsv]" << std::endl;
            std::cout << "Using defaults" << std::endl;
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
        stats.saveToFile(saveName);
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
