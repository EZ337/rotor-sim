// stats.h - Statistics collection and reporting
#ifndef STATS_H
#define STATS_H

#include <vector>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iomanip>
#include "flow.h"

class Statistics {
private:
    std::vector<double> fcts_bulk;
    std::vector<double> fcts_low_latency;
    std::vector<double> all_fcts;
    
    int total_flows;
    int completed_flows;
    int dropped_packets;
    double total_throughput_gbps;
    double sim_time_ms;

public:
    Statistics() : total_flows(0), completed_flows(0), 
                   dropped_packets(0), total_throughput_gbps(0),
                   sim_time_ms(0) {}
    
    void addFlow(const Flow& flow) {
        total_flows++;
        
        if (flow.completed) {
            completed_flows++;
            double fct = flow.getFCT();
            all_fcts.push_back(fct);
            
            if (flow.type == FlowType::BULK) {
                fcts_bulk.push_back(fct);
            } else {
                fcts_low_latency.push_back(fct);
            }
        }
    }
    
    void addDroppedPacket() {
        dropped_packets++;
    }
    
    void setTotalThroughput(double gbps) {
        total_throughput_gbps = gbps;
    }
    
    void setSimTime(double ms) {
        sim_time_ms = ms;
    }
    
    double getPercentile(const std::vector<double>& data, double percentile) const {
        if (data.empty()) return 0.0;
        
        std::vector<double> sorted = data;
        std::sort(sorted.begin(), sorted.end());
        
        size_t idx = static_cast<size_t>(percentile * sorted.size());
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        
        return sorted[idx];
    }
    
    double getMean(const std::vector<double>& data) const {
        if (data.empty()) return 0.0;
        return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    }
    
    void print() const {
        std::cout << "\n========== Simulation Results ==========" << std::endl;
        std::cout << std::fixed << std::setprecision(3);
        
        std::cout << "\nFlow Statistics:" << std::endl;
        std::cout << "  Total flows: " << total_flows << std::endl;
        std::cout << "  Completed flows: " << completed_flows 
                  << " (" << (100.0 * completed_flows / total_flows) << "%)" << std::endl;
        std::cout << "  Dropped packets: " << dropped_packets << std::endl;
        
        if (!all_fcts.empty()) {
            std::cout << "\nFlow Completion Times (all flows):" << std::endl;
            std::cout << "  Mean: " << getMean(all_fcts) << " ms" << std::endl;
            std::cout << "  Median: " << getPercentile(all_fcts, 0.5) << " ms" << std::endl;
            std::cout << "  95th: " << getPercentile(all_fcts, 0.95) << " ms" << std::endl;
            std::cout << "  99th: " << getPercentile(all_fcts, 0.99) << " ms" << std::endl;
            std::cout << "  Max: " << getPercentile(all_fcts, 1.0) << " ms" << std::endl;
        }
        
        if (!fcts_low_latency.empty()) {
            std::cout << "\nLow-latency FCTs:" << std::endl;
            std::cout << "  Count: " << fcts_low_latency.size() << std::endl;
            std::cout << "  Mean: " << getMean(fcts_low_latency) << " ms" << std::endl;
            std::cout << "  99th: " << getPercentile(fcts_low_latency, 0.99) << " ms" << std::endl;
        }
        
        if (!fcts_bulk.empty()) {
            std::cout << "\nBulk FCTs:" << std::endl;
            std::cout << "  Count: " << fcts_bulk.size() << std::endl;
            std::cout << "  Mean: " << getMean(fcts_bulk) << " ms" << std::endl;
            std::cout << "  99th: " << getPercentile(fcts_bulk, 0.99) << " ms" << std::endl;
        }
        
        std::cout << "\nThroughput:" << std::endl;
        std::cout << "  Average: " << total_throughput_gbps << " Gb/s" << std::endl;
        
        std::cout << "\n========================================" << std::endl;
    }
    
    void saveToFile(const std::string& filename) const {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open " << filename << " for writing" << std::endl;
            return;
        }
        
        file << "metric,value\n";
        file << "total_flows," << total_flows << "\n";
        file << "completed_flows," << completed_flows << "\n";
        file << "dropped_packets," << dropped_packets << "\n";
        file << "throughput_gbps," << total_throughput_gbps << "\n";
        
        if (!all_fcts.empty()) {
            file << "mean_fct_ms," << getMean(all_fcts) << "\n";
            file << "median_fct_ms," << getPercentile(all_fcts, 0.5) << "\n";
            file << "p95_fct_ms," << getPercentile(all_fcts, 0.95) << "\n";
            file << "p99_fct_ms," << getPercentile(all_fcts, 0.99) << "\n";
        }
        
        file.close();
        std::cout << "Results saved to " << filename << std::endl;
    }
};

#endif // STATS_H
