// voq.h - Virtual Output Queue management
#ifndef VOQ_H
#define VOQ_H

#include <queue>
#include <map>
#include <vector>

// VOQ system for a single rack
class VirtualOutputQueues {
private:
    int rack_id;
    int num_racks;
    int queue_capacity;
    
    // voqs[destination_rack] = queue of packet IDs
    std::map<int, std::queue<uint64_t>> voqs;
    
    // Track total packets in all queues
    int total_packets;

public:
    // Default constructor
    VirtualOutputQueues() = delete;
    
    // Parameterized constructor
    VirtualOutputQueues(int rack, int num_racks, int capacity) 
        : rack_id(rack), num_racks(num_racks), 
          queue_capacity(capacity), total_packets(0) {
        // Initialize VOQ for each destination
        for (int i = 0; i < num_racks; i++) {
            if (i != rack_id) {
                voqs[i] = std::queue<uint64_t>();
            }
        }
    }
    
    // Try to enqueue a packet to the VOQ for dst_rack
    bool enqueue(uint64_t packet_id, int dst_rack) {
        if (dst_rack == rack_id) {
            return false; // Local traffic, shouldn't be here
        }
        
        // Check capacity (per-VOQ or total, configurable)
        if (voqs[dst_rack].size() >= queue_capacity) {
            return false; // Queue full
        }
        
        voqs[dst_rack].push(packet_id);
        total_packets++;
        return true;
    }
    
    // Dequeue a packet destined for dst_rack
    bool dequeue(int dst_rack, uint64_t& packet_id) {
        if (voqs[dst_rack].empty()) {
            return false;
        }
        
        packet_id = voqs[dst_rack].front();
        voqs[dst_rack].pop();
        total_packets--;
        return true;
    }
    
    // Check if VOQ for dst_rack has packets
    bool hasPackets(int dst_rack) const {
        auto it = voqs.find(dst_rack);
        if (it == voqs.end()) return false;
        return !it->second.empty();
    }
    
    // Get number of packets in VOQ for dst_rack
    size_t getQueueSize(int dst_rack) const {
        auto it = voqs.find(dst_rack);
        if (it == voqs.end()) return 0;
        return it->second.size();
    }
    
    // Get total packets across all VOQs
    int getTotalPackets() const {
        return total_packets;
    }
    
    // Get all destination racks that have packets waiting
    std::vector<int> getNonemptyDestinations() const {
        std::vector<int> dests;
        for (const auto& pair : voqs) {
            if (!pair.second.empty()) {
                dests.push_back(pair.first);
            }
        }
        return dests;
    }
    
    // Clear all queues (for debugging/reset)
    void clear() {
        for (auto& pair : voqs) {
            while (!pair.second.empty()) {
                pair.second.pop();
            }
        }
        total_packets = 0;
    }
};

#endif // VOQ_H
