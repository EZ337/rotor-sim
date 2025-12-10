// simulator.h - Main simulation engine
#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <queue>
#include <map>
#include <memory>
#include <random>
#include "config.h"
#include "flow.h"
#include "topology.h"
#include "workload_generator.h"
#include "stats.h"
#include "voq.h"

// Event types for discrete event simulation
enum class EventType {
    FLOW_ARRIVAL,
    PACKET_ARRIVAL,
    PACKET_TRANSMISSION_COMPLETE
};

struct Event {
    EventType type;
    double time_us;
    uint64_t id; // Flow or packet ID
    
    bool operator>(const Event& other) const {
        return time_us > other.time_us;
    }
};

class Simulator {
private:
    const SimConfig& config;
    RotorTopology topology;
    Statistics stats;
    std::mt19937 rng;
    
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> event_queue;
    
    std::map<uint64_t, Flow> flows;
    std::map<uint64_t, Packet> packets;
    
    double current_time_us;
    uint64_t next_packet_id;
    
    // VOQ at each rack
    std::map<int, VirtualOutputQueues> rack_voqs;
    std::map<int, bool> rack_busy; // Is rack currently transmitting?
    std::map<int, double> rack_next_free_time;
    
    uint64_t total_bytes_transmitted;
    
    void scheduleEvent(EventType type, double time, uint64_t id) {
        Event e;
        e.type = type;
        e.time_us = time;
        e.id = id;
        event_queue.push(e);
    }
    
    void handleFlowArrival(uint64_t flow_id) {
        Flow& flow = flows[flow_id];
        
        // Create packets for this flow
        int num_packets = flow.getNumPackets(config.mtu_bytes);
        uint64_t remaining_bytes = flow.size_bytes;
        
        // For 2-hop VLB: randomly select intermediate rack for this flow
        std::uniform_int_distribution<int> rack_dist(0, config.num_racks - 1);
        int intermediate = -1;
        
        // TODO: Need to verify logic against RotorNet paper
        // Use VLB for skewed traffic or when configured
        // For simplicity: always use VLB for low-latency, probabilistic for bulk
        if (flow.type == FlowType::LOW_LATENCY) {
            // Always use 2-hop for low-latency
            do {
                intermediate = rack_dist(rng);
            } while (intermediate == flow.src_rack || intermediate == flow.dst_rack);
        } else {
            // Bulk: try direct first, use VLB if needed (decision made per packet based on queue state)
            intermediate = -1; // Will be set per-packet if needed
        }
        
        for (int i = 0; i < num_packets; i++) {
            Packet pkt;
            pkt.id = next_packet_id++;
            pkt.flow_id = flow_id;
            pkt.src_rack = flow.src_rack;
            pkt.dst_rack = flow.dst_rack;
            pkt.src_host = flow.src_host;
            pkt.dst_host = flow.dst_host;
            pkt.size_bytes = std::min((uint64_t)config.mtu_bytes, remaining_bytes);
            pkt.creation_time = current_time_us / 1000.0; // Convert to ms
            pkt.type = flow.type;
            pkt.dropped = false;
            pkt.hop_count = 0;
            pkt.intermediate_rack = intermediate;
            pkt.at_intermediate = false;
            
            remaining_bytes -= pkt.size_bytes;
            
            flow.packet_ids.push_back(pkt.id);
            packets[pkt.id] = pkt;
            
            // Enqueue packet at source rack
            enqueuePacket(pkt.id, flow.src_rack);
        }
    }
    
    void enqueuePacket(uint64_t packet_id, int rack_id) {
        Packet& pkt = packets[packet_id];
        
        // Determine destination for this hop
        int next_hop_dest;
        if (pkt.intermediate_rack >= 0 && !pkt.at_intermediate) {
            // Going to intermediate rack (first hop of VLB)
            next_hop_dest = pkt.intermediate_rack;
        } else {
            // Going to final destination
            next_hop_dest = pkt.dst_rack;
        }
        
        // Enqueue in appropriate VOQ
        if (!rack_voqs.at(rack_id).enqueue(packet_id, next_hop_dest)) {
            // Drop packet if queue is full
            pkt.dropped = true;
            stats.addDroppedPacket();
            return;
        }
        
        // If rack is not busy, start transmission
        if (!rack_busy[rack_id]) {
            startTransmission(rack_id);
        }
    }
    
    // TODO: Needs overhaul to support RotorLB: (1 vs 2 hop. We currently hold bulk flows)
    // until we have a direct connection. This is NOT like RotorNet
    void startTransmission(int rack_id) {
        // Find a VOQ with packets that has a direct path available now
        std::vector<int> nonempty_dests = rack_voqs.at(rack_id).getNonemptyDestinations();
        
        // TODO: Evaluate later: Should possibly still transmission completion event
        if (nonempty_dests.empty()) {
            rack_busy[rack_id] = false;
            return;
        }
        
        rack_busy[rack_id] = true;
        
        // Try to find a destination with direct path
        int selected_dest = -1;
        uint64_t packet_id = 0; // Updated in dequeue call
        
        // for queues with data
        for (int dest : nonempty_dests) {
            // if we have a connection (there's only one connection per per cycle generally)
            if (topology.hasDirectPath(rack_id, dest, current_time_us)) {
                if (rack_voqs.at(rack_id).dequeue(dest, packet_id)) {
                    selected_dest = dest;
                    break;
                }
            }
        }

        // TODO: Note that if we have a valid matching and we have flow that doesn't take the whole time,
        // we idle when we complete...
        
        // TODO: Modify so that it is inline with RotorNet paper
        // If no direct path available, check if we should wait or use VLB
        if (selected_dest == -1) {
            // For bulk traffic, wait for direct path
            // For low-latency, it should already have intermediate set
            // Just pick the first available destination
            selected_dest = nonempty_dests[0];
            rack_voqs.at(rack_id).dequeue(selected_dest, packet_id);
        }
        
        Packet& pkt = packets[packet_id];
        
        // For bulk traffic without intermediate set, wait for direct path
        if (pkt.type == FlowType::BULK && pkt.intermediate_rack == -1 && // TODO: isn't hasDirectPath always going to return false here?
            !topology.hasDirectPath(rack_id, selected_dest, current_time_us)) {
            
            double next_direct = topology.getNextDirectPathTime(
                rack_id, selected_dest, current_time_us);
            
            // Re-enqueue and schedule retry
            rack_voqs.at(rack_id).enqueue(packet_id, selected_dest);
            scheduleEvent(EventType::PACKET_TRANSMISSION_COMPLETE, 
                        next_direct, packet_id);
            return;
        }
        
        // Calculate transmission time
        double bits = pkt.size_bytes * 8.0;
        double tx_time_us = bits / (config.link_rate_gbps * 1e9) * 1e6;
        
        pkt.sent_time = current_time_us / 1000.0;
        
        scheduleEvent(EventType::PACKET_TRANSMISSION_COMPLETE,
                     current_time_us + tx_time_us, packet_id);
    }
    
    void handlePacketTransmissionComplete(uint64_t packet_id) {
        Packet& pkt = packets[packet_id];
        int current_rack = pkt.src_rack;
        
        // Add propagation delay
        double arrival_time = current_time_us + config.propagation_delay_us;
        
        // Determine where packet is going
        if (pkt.intermediate_rack >= 0 && !pkt.at_intermediate) {
            // Packet is at intermediate rack for first time
            if (pkt.src_rack == pkt.intermediate_rack) {
                pkt.at_intermediate = true;
                pkt.hop_count++;
                // Forward to final destination
                pkt.src_rack = pkt.intermediate_rack;
                scheduleEvent(EventType::PACKET_ARRIVAL, arrival_time, packet_id);
            } else {
                // Still going to intermediate
                pkt.hop_count++;
                pkt.src_rack = pkt.intermediate_rack;
                scheduleEvent(EventType::PACKET_ARRIVAL, arrival_time, packet_id);
            }
        } else {
            // Going to final destination
            if (pkt.src_rack == pkt.dst_rack) {
                // Arrived!
                pkt.arrival_time = arrival_time / 1000.0;
                pkt.hop_count++;
                total_bytes_transmitted += pkt.size_bytes;
                
                // Update flow
                Flow& flow = flows[pkt.flow_id];
                flow.packets_received++;
                
                if (flow.packets_received == flow.packet_ids.size()) {
                    flow.completed = true;
                    flow.completion_time = pkt.arrival_time;
                }
            } else {
                // Forward to destination
                pkt.hop_count++;
                pkt.src_rack = pkt.dst_rack;
                scheduleEvent(EventType::PACKET_ARRIVAL, arrival_time, packet_id);
            }
        }
        
        // Start next transmission at this rack
        rack_next_free_time[current_rack] = current_time_us;
        startTransmission(current_rack);
    }
    
    void handlePacketArrival(uint64_t packet_id) {
        Packet& pkt = packets[packet_id];
        enqueuePacket(packet_id, pkt.src_rack);
    }

public:
    Simulator(const SimConfig& cfg) 
        : config(cfg), topology(cfg), current_time_us(0), 
          next_packet_id(0), total_bytes_transmitted(0) {
        
        rng.seed(cfg.random_seed + 1000); // Different seed from workload gen
        
        // Initialize rack state and VOQs
        for (int i = 0; i < config.num_racks; i++) {
            rack_voqs.emplace(i, VirtualOutputQueues(i, config.num_racks, config.queue_size_pkts));
            rack_busy[i] = false;
            rack_next_free_time[i] = 0.0;
        }
    }
    
    void run() {
        std::cout << "Generating workload..." << std::endl;
        WorkloadGenerator wg(config);
        std::vector<Flow> flow_list;
        
        // Load or generate flows
        if (!config.flow_file.empty()) {
            flow_list = wg.loadFlowsFromFile(config.flow_file);
        } else {
            flow_list = wg.generateFlows();
            if (config.save_flows) {
                wg.saveFlowsToFile(flow_list, config.flow_output_file);
            }
        }
        
        // Add flows to map and schedule arrivals
        for (auto& flow : flow_list) {
            flows[flow.id] = flow;
            scheduleEvent(EventType::FLOW_ARRIVAL, 
                         flow.start_time * 1000.0, flow.id); // Convert ms to us
        }
        
        std::cout << "Running simulation..." << std::endl;
        int event_count = 0;
        int progress_interval = event_queue.size() / 20; // 5% progress updates
        if (progress_interval == 0) progress_interval = 1000;
        
        while (!event_queue.empty()) {
            Event evt = event_queue.top();
            event_queue.pop();
            
            current_time_us = evt.time_us;
            
            switch (evt.type) {
                case EventType::FLOW_ARRIVAL:
                    handleFlowArrival(evt.id);
                    break;
                case EventType::PACKET_ARRIVAL:
                    handlePacketArrival(evt.id);
                    break;
                case EventType::PACKET_TRANSMISSION_COMPLETE:
                    handlePacketTransmissionComplete(evt.id);
                    break;
            }
            
            event_count++;
            if (event_count % progress_interval == 0) {
                double progress = 100.0 * current_time_us / (config.sim_time_ms * 1000.0);
                std::cout << "  Progress: " << std::fixed << std::setprecision(1) 
                         << progress << "%" << std::endl;
            }
        }
        
        std::cout << "Simulation complete. Collecting statistics..." << std::endl;
        
        // Collect statistics
        for (auto& pair : flows) {
            stats.addFlow(pair.second);
        }
        
        double sim_time_s = config.sim_time_ms / 1000.0;
        double throughput_gbps = (total_bytes_transmitted * 8.0) / (sim_time_s * 1e9);
        stats.setTotalThroughput(throughput_gbps);
        stats.setSimTime(config.sim_time_ms);
    }
    
    Statistics getStatistics() const {
        return stats;
    }
};

#endif // SIMULATOR_H
