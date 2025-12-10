// workload_generator.h - Generate flows based on published distributions
#ifndef WORKLOAD_GENERATOR_H
#define WORKLOAD_GENERATOR_H

#include <random>
#include <vector>
#include <cmath>
#include <sstream>
#include "flow.h"
#include "config.h"

class WorkloadGenerator {
private:
    std::mt19937 rng;
    const SimConfig& config;
    uint64_t next_flow_id;
    
    // CDF breakpoints for flow size distributions (bytes, cumulative probability)
    struct CDFPoint {
        uint64_t size;
        double prob;
    };
    
    std::vector<CDFPoint> getCDFForWorkload(WorkloadType type) {
        switch(type) {
            case WorkloadType::DATAMINING:
                // From VL2 paper - Datamining workload
                return {
                    {100, 0.0},
                    {1000, 0.5},
                    {10000, 0.6},
                    {100000, 0.7},
                    {1000000, 0.8},
                    {10000000, 0.9},
                    {100000000, 0.97},
                    {1000000000, 1.0}
                };
                
            case WorkloadType::WEBSEARCH:
                // From DCTCP paper - Websearch workload
                return {
                    {100, 0.0},
                    {1000, 0.15},
                    {10000, 0.2},
                    {100000, 0.3},
                    {1000000, 0.4},
                    {10000000, 0.53},
                    {100000000, 0.6},
                    {300000000, 1.0}
                };
                
            case WorkloadType::HADOOP:
                // From Facebook paper - Hadoop workload
                return {
                    {1000, 0.0},
                    {10000, 0.05},
                    {100000, 0.2},
                    {1000000, 0.5},
                    {10000000, 0.7},
                    {100000000, 0.85},
                    {1000000000, 1.0}
                };
        }
        return {};
    }
    
    uint64_t sampleFlowSize() {
        std::vector<CDFPoint> cdf = getCDFForWorkload(config.workload);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double rand_val = dist(rng);
        
        // Find the appropriate CDF segment
        for (size_t i = 1; i < cdf.size(); i++) {
            if (rand_val <= cdf[i].prob) {
                // Linear interpolation within segment
                double frac = (rand_val - cdf[i-1].prob) / (cdf[i].prob - cdf[i-1].prob);
                
                // Log-scale interpolation for size
                double log_size = std::log10(cdf[i-1].size) + 
                                 frac * (std::log10(cdf[i].size) - std::log10(cdf[i-1].size));
                return static_cast<uint64_t>(std::pow(10.0, log_size));
            }
        }
        
        return cdf.back().size;
    }
    
    double getAverageFlowSize() {
        // Approximate average based on workload type
        switch(config.workload) {
            case WorkloadType::DATAMINING: return 50e6;  // 50 MB
            case WorkloadType::WEBSEARCH: return 5e6;    // 5 MB
            case WorkloadType::HADOOP: return 30e6;      // 30 MB
        }
        return 10e6;
    }

public:
    WorkloadGenerator(const SimConfig& cfg) 
        : config(cfg), next_flow_id(0) {
        rng.seed(cfg.random_seed);
    }
    
    std::vector<Flow> generateFlows() {
        std::vector<Flow> flows;
        
        // Calculate arrival rate based on load factor
        int total_hosts = config.num_racks * config.hosts_per_rack;
        double total_capacity = total_hosts * config.link_rate_gbps * 1e9; // bits/s
        double avg_flow_size_bits = getAverageFlowSize() * 8;
        
        // Poisson arrival process
        double lambda = (config.load_factor * total_capacity) / avg_flow_size_bits; // flows/s
        double lambda_per_ms = lambda / 1000.0;
        
        std::exponential_distribution<double> interarrival(lambda_per_ms);
        std::uniform_int_distribution<int> rack_dist(0, config.num_racks - 1);
        std::uniform_int_distribution<int> host_dist(0, config.hosts_per_rack - 1);
        
        double current_time = 0.0;
        
        while (current_time < config.sim_time_ms) {
            Flow flow;
            flow.id = next_flow_id++;
            flow.start_time = current_time;
            flow.completed = false;
            
            // Random source and destination
            flow.src_rack = rack_dist(rng);
            flow.dst_rack = rack_dist(rng);
            
            // Ensure inter-rack traffic
            while (flow.src_rack == flow.dst_rack) {
                flow.dst_rack = rack_dist(rng);
            }
            
            flow.src_host = host_dist(rng);
            flow.dst_host = host_dist(rng);
            
            // Sample flow size
            flow.size_bytes = sampleFlowSize();
            
            // Classify as bulk or low-latency (15 MB threshold per Opera paper)
            // flow.type = (flow.size_bytes >= 15e6) ? FlowType::BULK : FlowType::LOW_LATENCY;
            // all flow types are Bulk. We are only simulating RotorNet. low-latency packets are sent over packet switch
            // which is not rotornet
            flow.type = FlowType::BULK;
            
            flows.push_back(flow);
            
            // Next arrival time
            current_time += interarrival(rng);
        }
        
        std::cout << "Generated " << flows.size() << " flows" << std::endl;
        
        return flows;
    }
    
    void saveFlowsToFile(const std::vector<Flow>& flows, const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file for writing: " + filename);
        }
        
        // Write header
        file << "flow_id,src_rack,dst_rack,src_host,dst_host,size_bytes,start_time_ms,flow_type\n";
        
        // Write flows
        for (const auto& flow : flows) {
            file << flow.id << ","
                 << flow.src_rack << ","
                 << flow.dst_rack << ","
                 << flow.src_host << ","
                 << flow.dst_host << ","
                 << flow.size_bytes << ","
                 << flow.start_time << ","
                 << (flow.type == FlowType::BULK ? "bulk" : "low_latency")
                 << "\n";
        }
        
        file.close();
        std::cout << "Saved " << flows.size() << " flows to " << filename << std::endl;
    }
    
    std::vector<Flow> loadFlowsFromFile(const std::string& filename) {
        std::vector<Flow> flows;
        std::ifstream file(filename);
        
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file for reading: " + filename);
        }
        
        std::string line;
        std::getline(file, line); // Skip header
        
        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string field;
            Flow flow;
            
            std::getline(ss, field, ','); flow.id = std::stoull(field);
            std::getline(ss, field, ','); flow.src_rack = std::stoi(field);
            std::getline(ss, field, ','); flow.dst_rack = std::stoi(field);
            std::getline(ss, field, ','); flow.src_host = std::stoi(field);
            std::getline(ss, field, ','); flow.dst_host = std::stoi(field);
            std::getline(ss, field, ','); flow.size_bytes = std::stoull(field);
            std::getline(ss, field, ','); flow.start_time = std::stod(field);
            std::getline(ss, field, ',');
            flow.type = (field == "bulk") ? FlowType::BULK : FlowType::LOW_LATENCY;
            
            flow.completed = false;
            flow.packets_sent = 0;
            flow.packets_received = 0;
            
            flows.push_back(flow);
            
            if (flow.id >= next_flow_id) {
                next_flow_id = flow.id + 1;
            }
        }
        
        file.close();
        std::cout << "Loaded " << flows.size() << " flows from " << filename << std::endl;
        
        return flows;
    }
};

#endif // WORKLOAD_GENERATOR_H
