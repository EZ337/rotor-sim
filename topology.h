// topology.h - RotorNet topology and matching management
#ifndef TOPOLOGY_H
#define TOPOLOGY_H

#include <vector>
#include <random>
#include <algorithm>
#include "config.h"

class RotorTopology {
private:
    const SimConfig& config;
    int num_matchings;
    double slot_time_us;
    double cycle_time_us;
    
    // matchings[switch_id][matching_id][rack_id] = connected_rack_id
    std::vector<std::vector<std::vector<int>>> matchings;
    
    // Generate a random perfect matching (permutation)
    std::vector<int> generateRandomMatching(std::mt19937& rng) {
        std::vector<int> matching(config.num_racks);
        for (int i = 0; i < config.num_racks; i++) {
            matching[i] = i;
        }
        std::shuffle(matching.begin(), matching.end(), rng);
        return matching;
    }
    
    // Generate disjoint matchings using a simple rotation method
    void generateMatchings() {
        num_matchings = config.getNumMatchings();
        slot_time_us = config.getSlotTime();
        cycle_time_us = config.getCycleTime();
        
        matchings.resize(config.num_switches);
        
        // Generate (num_racks - 1) disjoint matchings using round-robin
        std::vector<std::vector<int>> all_matchings;
        int n = config.num_racks;
        
        // Classic round-robin tournament scheduling
        for (int m = 0; m < n - 1; m++) {
            std::vector<int> matching(n);
            
            // Fix position 0, rotate others
            matching[0] = 0;
            for (int i = 1; i < n; i++) {
                int partner = (n - i + m) % (n - 1);
                if (partner == 0) partner = n - 1;
                matching[i] = partner;
            }
            
            all_matchings.push_back(matching);
        }
        
        // Distribute matchings across switches
        for (int s = 0; s < config.num_switches; s++) {
            for (int m = s; m < all_matchings.size(); m += config.num_switches) {
                matchings[s].push_back(all_matchings[m]);
            }
        }
    }

public:
    RotorTopology(const SimConfig& cfg) : config(cfg) {
        generateMatchings();
        
        std::cout << "Topology initialized:" << std::endl;
        std::cout << "  Matchings per switch: " << num_matchings << std::endl;
        std::cout << "  Slot time: " << slot_time_us << " μs" << std::endl;
        std::cout << "  Cycle time: " << cycle_time_us << " μs" << std::endl;
        std::cout << std::endl;
    }
    
    // Get the rack connected to src_rack on switch_id at time t
    int getConnectedRack(int src_rack, int switch_id, double time_us) {
        double time_in_cycle = fmod(time_us, cycle_time_us);
        int matching_idx = static_cast<int>(time_in_cycle / slot_time_us) % num_matchings;

        double time_in_slot = fmod(time_in_cycle, slot_time_us);
        if (time_in_slot < config.reconfig_delay_us) {
            return -1; // link down during reconfig
        }

        if (switch_id >= 0 && switch_id < matchings.size() &&
            matching_idx >= 0 && matching_idx < matchings[switch_id].size()) {
            return matchings[switch_id][matching_idx][src_rack];
        }

        return -1;
    }


    
    // Check if direct path exists from src to dst at given time
    // We are assuming no reconfig delay
    bool hasDirectPath(int src_rack, int dst_rack, double time_us) {
        for (int s = 0; s < config.num_switches; s++) {
            if (getConnectedRack(src_rack, s, time_us) == dst_rack) {
                return true;
            }
        }
        return false;
    }
    
    // Find next time when direct path will be available
    double getNextDirectPathTime(int src_rack, int dst_rack, double current_time_us) {
        double check_time = current_time_us;
        double max_time = current_time_us + cycle_time_us;
        
        while (check_time < max_time) {
            if (hasDirectPath(src_rack, dst_rack, check_time)) {
                return check_time;
            }
            check_time += slot_time_us;
        }
        
        return current_time_us + cycle_time_us; // Next cycle
    }
    
    double getCycleTime() const { return cycle_time_us; }
    double getSlotTime() const { return slot_time_us; }
};

#endif // TOPOLOGY_H
