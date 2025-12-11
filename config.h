// config.h - Configuration management
#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <fstream>
#include <iostream>

enum class WorkloadType {
    DATAMINING,
    WEBSEARCH,
    HADOOP
};

struct SimConfig {
    // Network parameters
    int num_racks = 16;
    int num_switches = 4;
    int hosts_per_rack = 32;
    double link_rate_gbps = 10.0;
    int mtu_bytes = 1500;
    double propagation_delay_us = 0.5;
    int queue_threshold = 3;
    
    // RotorNet specific
    double reconfig_delay_us = 20.0;
    double duty_cycle = 0.9;
    
    // Workload parameters
    WorkloadType workload = WorkloadType::DATAMINING;
    double load_factor = 0.25; // Network load (0.0 to 1.0)
    double sim_time_ms = 1000.0;
    int random_seed = 42;
    std::string flow_file = ""; // If set, load flows from file instead of generating
    bool save_flows = false;    // If true, save generated flows to file
    std::string flow_output_file = "flows.csv";
    
    // Transport parameters
    int queue_size_pkts = 100;
    
    void setDefaults() {
        // Already set above
#ifdef DEBUG
        num_racks = 8;
        hosts_per_rack = 8;
        link_rate_gbps = 10;
        mtu_bytes = 1500;
        sim_time_ms = 100;
        load_factor = 0.3;
        workload = WorkloadType::DATAMINING;
        random_seed = 1;
        // save_flows = true;
        flow_output_file="debugflows.csv";
        flow_file="debugflows.csv";
#endif
        
    }
    
    void loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open config file: " + filename);
        }
        
        std::string key;
        while (file >> key) {
            if (key == "num_racks") file >> num_racks;
            else if (key == "num_switches") file >> num_switches;
            else if (key == "hosts_per_rack") file >> hosts_per_rack;
            else if (key == "link_rate_gbps") file >> link_rate_gbps;
            else if (key == "load_factor") file >> load_factor;
            else if (key == "sim_time_ms") file >> sim_time_ms;
            else if (key == "random_seed") file >> random_seed;
            else if (key == "workload") {
                std::string wl;
                file >> wl;
                if (wl == "datamining") workload = WorkloadType::DATAMINING;
                else if (wl == "websearch") workload = WorkloadType::WEBSEARCH;
                else if (wl == "hadoop") workload = WorkloadType::HADOOP;
            }
            else if (key == "flow_file") file >> flow_file;
            else if (key == "save_flows") {
                std::string val;
                file >> val;
                save_flows = (val == "true" || val == "1");
            }
            else if (key == "flow_output_file") file >> flow_output_file;
        }
    }
    
    void print() const {
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Racks: " << num_racks << std::endl;
        std::cout << "  Switches: " << num_switches << std::endl;
        std::cout << "  Hosts per rack: " << hosts_per_rack << std::endl;
        std::cout << "  Link rate: " << link_rate_gbps << " Gb/s" << std::endl;
        std::cout << "  Load factor: " << load_factor << std::endl;
        std::cout << "  Simulation time: " << sim_time_ms << " ms" << std::endl;
        
        std::string wl_name;
        switch(workload) {
            case WorkloadType::DATAMINING: wl_name = "Datamining"; break;
            case WorkloadType::WEBSEARCH: wl_name = "Websearch"; break;
            case WorkloadType::HADOOP: wl_name = "Hadoop"; break;
        }
        std::cout << "  Workload: " << wl_name << std::endl;
        std::cout << std::endl;
    }
    
    int getNumMatchings() const {
        return static_cast<int>(std::ceil(static_cast<double>(num_racks - 1) / num_switches));
    }
    
    double getSlotTime() const {
        return reconfig_delay_us / (1.0 - duty_cycle);
    }
    
    double getCycleTime() const {
        return getNumMatchings() * getSlotTime();
    }
};

#endif // CONFIG_H
