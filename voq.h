// voq.h - Virtual Output Queue management
#ifndef VOQ_H
#define VOQ_H

#include <queue>
#include <map>
#include <vector>

// VOQ system for a single rack
// Maintains two types of queues:
// 1. local_voqs: Packets originating at this rack (first hop)
// 2. nonlocal_voqs: Packets that arrived here as intermediate (second hop)
class VirtualOutputQueues {
private:
    int rack_id;
    int num_racks;
    int queue_capacity;
    
    // local_voqs[final_dst] = queue of packet IDs originating at this rack
    // These are packets on their first hop (either direct or to intermediate)
    std::map<int, std::queue<uint64_t>> local_voqs;
    
    // nonlocal_voqs[final_dst] = queue of packet IDs that arrived here as intermediate
    // These are packets on their second hop (intermediate -> final_dst)
    std::map<int, std::queue<uint64_t>> nonlocal_voqs;
    
    // Track total packets in all queues
    int total_packets;

public:
    // Default constructor deleted - must provide parameters
    VirtualOutputQueues() = delete;

    enum class VoqType {LOCAL, NONLOCAL};
    
    // Parameterized constructor
    VirtualOutputQueues(int rack, int num_racks, int capacity) 
        : rack_id(rack), num_racks(num_racks), 
          queue_capacity(capacity), total_packets(0) {
        // Initialize VOQs for each destination
        for (int i = 0; i < num_racks; i++) {
            if (i != rack_id) {
                local_voqs[i] = std::queue<uint64_t>();
                nonlocal_voqs[i] = std::queue<uint64_t>();
            }
        }
    }

    /// @brief Enqueues a packet in `type` voq
    /// @param packet_id 
    /// @param nexthop 
    /// @param type true if success. False otherwise
    /// @return 
    bool enqueue(int packet_id, int nexthop, VoqType type)
    {
        switch (type)
        {
        case VoqType::LOCAL:
            return enqueueLocal(packet_id, nexthop);
        
        case VoqType::NONLOCAL:
            return enqueueNonlocal(packet_id, nexthop);
        default:
            assert(false && "Unexpected VOQType");
            return false;
        }
        // unreached code
        return false;
    }

    /// @brief Dequeues a packet in `type` voq
    /// @param packet_id 
    /// @param nexthop 
    /// @param type  
    /// @return true if success. false otherwise
    bool dequeue(int dst_rack, uint64_t& packet_id, VoqType type)
    {
        switch (type)
        {
        case VoqType::LOCAL:
            return dequeueLocal(dst_rack, packet_id);
        
        case VoqType::NONLOCAL:
            return dequeueNonlocal(dst_rack, packet_id);
        default:
            assert(false && "Unexpected VOQType");
            return false;
        }
        // unreached code
        return false;
    }
    
    // Enqueue a LOCAL packet (originating at this rack, first hop)
    // dst_rack is the final destination or intermediate for this packet
    bool enqueueLocal(uint64_t packet_id, int dst_rack) {
        if (dst_rack == rack_id) {
            return false; // Local traffic, shouldn't be here
        }
        
        if (local_voqs[dst_rack].size() >= queue_capacity) {
            return false; // Queue full
        }
        
        local_voqs[dst_rack].push(packet_id);
        total_packets++;
        return true;
    }
    
    // Enqueue a NON-LOCAL packet (arrived here as intermediate, second hop)
    // final_dst is the ultimate destination for this packet
    bool enqueueNonlocal(uint64_t packet_id, int final_dst) {
        if (final_dst == rack_id) {
            return false; // This is the final destination, shouldn't be here
        }
        
        if (nonlocal_voqs[final_dst].size() >= queue_capacity) {
            return false; // Queue full
        }
        
        nonlocal_voqs[final_dst].push(packet_id);
        total_packets++;
        return true;
    }
    
    // Dequeue from LOCAL VOQ for given destination
    bool dequeueLocal(int dst_rack, uint64_t& packet_id) {
        if (local_voqs[dst_rack].empty()) {
            return false;
        }
        
        packet_id = local_voqs[dst_rack].front();
        local_voqs[dst_rack].pop();
        total_packets--;
        return true;
    }
    
    // Dequeue from NON-LOCAL VOQ for given final destination
    bool dequeueNonlocal(int final_dst, uint64_t& packet_id) {
        if (nonlocal_voqs[final_dst].empty()) {
            return false;
        }
        
        packet_id = nonlocal_voqs[final_dst].front();
        nonlocal_voqs[final_dst].pop();
        total_packets--;
        return true;
    }
    
    // Check if LOCAL VOQ has packets for dst_rack
    bool hasLocalPackets(int dst_rack) const {
        auto it = local_voqs.find(dst_rack);
        if (it == local_voqs.end()) return false;
        return !it->second.empty();
    }
    
    // Check if NON-LOCAL VOQ has packets for final_dst
    bool hasNonlocalPackets(int final_dst) const {
        auto it = nonlocal_voqs.find(final_dst);
        if (it == nonlocal_voqs.end()) return false;
        return !it->second.empty();
    }
    
    // Get size of LOCAL VOQ for dst_rack
    size_t getLocalQueueSize(int dst_rack) const {
        auto it = local_voqs.find(dst_rack);
        if (it == local_voqs.end()) return 0;
        return it->second.size();
    }
    
    // Get size of NON-LOCAL VOQ for final_dst
    size_t getNonlocalQueueSize(int final_dst) const {
        auto it = nonlocal_voqs.find(final_dst);
        if (it == nonlocal_voqs.end()) return 0;
        return it->second.size();
    }
    
    // Get total packets across all VOQs
    int getTotalPackets() const {
        return total_packets;
    }
    
    // Get all destination racks that have LOCAL packets waiting
    std::vector<int> getNonemptyLocalDestinations() const {
        std::vector<int> dests;
        for (const auto& pair : local_voqs) {
            if (!pair.second.empty()) {
                dests.push_back(pair.first);
            }
        }
        return dests;
    }
    
    // Get all final destination racks that have NON-LOCAL packets waiting
    std::vector<int> getNonemptyNonlocalDestinations() const {
        std::vector<int> dests;
        for (const auto& pair : nonlocal_voqs) {
            if (!pair.second.empty()) {
                dests.push_back(pair.first);
            }
        }
        return dests;
    }
    
    // Clear all queues (for debugging/reset)
    void clear() {
        for (auto& pair : local_voqs) {
            while (!pair.second.empty()) {
                pair.second.pop();
            }
        }
        for (auto& pair : nonlocal_voqs) {
            while (!pair.second.empty()) {
                pair.second.pop();
            }
        }
        total_packets = 0;
    }
};

#endif // VOQ_H
