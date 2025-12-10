// simulator.h - Main simulation engine
#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <queue>
#include <map>
#include <memory>
#include "config.h"
#include "flow.h"
#include "topology.h"
#include "workload_generator.h"
#include "stats.h"

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
    
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> event_queue;
    
    std::map<uint64_t, Flow> flows;
    std::map<uint64_t, Packet> packets;
    
    double current_time_us;
    uint64_t next_packet_id;
    
    // Queue at each rack (simplified - single queue per rack)
    std::map<int, std::queue<uint64_t>> rack_queues;
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
            
            remaining_bytes -= pkt.size_bytes;
            
            flow.packet_ids.push_back(pkt.id);
            packets[pkt.id] = pkt;
            
            // Enqueue packet at source rack
            enqueuePacket(pkt.id, flow.src_rack);
        }
    }
    
    void enqueuePacket(uint64_t packet_id, int rack_id) {
        // Check queue capacity
        if (rack_queues[rack_id].size() >= config.queue_size_pkts) {
            // Drop packet
            packets[packet_id].dropped = true;
            stats.addDroppedPacket();
            return;
        }
        
        rack_queues[rack_id].push(packet_id);
        
        // If rack is not busy, start transmission
        if (!rack_busy[rack_id]) {
            startTransmission(rack_id);
        }
    }
    
    void startTransmission(int rack_id) {
        if (rack_queues[rack_id].empty()) {
            rack_busy[rack_id] = false;
            return;
        }
        
        rack_busy[rack_id] = true;
        uint64_t packet_id = rack_queues[rack_id].front();
        rack_queues[rack_id].pop();
        
        Packet& pkt = packets[packet_id];
        
        // For bulk traffic, wait for direct path
        if (pkt.type == FlowType::BULK && pkt.hop_count == 0) {
            double next_direct = topology.getNextDirectPathTime(
                pkt.src_rack, pkt.dst_rack, current_time_us);
            
            if (next_direct > current_time_us) {
                // Wait for direct path
                scheduleEvent(EventType::PACKET_TRANSMISSION_COMPLETE, 
                            next_direct, packet_id);
                return;
            }
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
        pkt.hop_count++;
        
        // Add propagation delay
        double arrival_time = current_time_us + config.propagation_delay_us;
        
        if (pkt.src_rack == pkt.dst_rack || pkt.hop_count > 5) {
            // Arrived at destination (or too many hops - drop)
            if (pkt.hop_count > 5) {
                pkt.dropped = true;
                stats.addDroppedPacket();
            } else {
                pkt.arrival_time = arrival_time / 1000.0;
                total_bytes_transmitted += pkt.size_bytes;
                
                // Update flow
                Flow& flow = flows[pkt.flow_id];
                flow.packets_received++;
                
                if (flow.packets_received == flow.packet_ids.size()) {
                    flow.completed = true;
                    flow.completion_time = pkt.arrival_time;
                }
            }
        } else {
            // Forward packet - update source rack for next hop
            // For simplicity, use single-hop forwarding to destination
            pkt.src_rack = pkt.dst_rack;
            scheduleEvent(EventType::PACKET_ARRIVAL, arrival_time, packet_id);
        }
        
        // Start next transmission at this rack
        rack_next_free_time[pkt.src_rack] = current_time_us;
        startTransmission(pkt.src_rack);
    }
    
    void handlePacketArrival(uint64_t packet_id) {
        Packet& pkt = packets[packet_id];
        enqueuePacket(packet_id, pkt.src_rack);
    }

public:
    Simulator(const SimConfig& cfg) 
        : config(cfg), topology(cfg), current_time_us(0), 
          next_packet_id(0), total_bytes_transmitted(0) {
        
        // Initialize rack state
        for (int i = 0; i < config.num_racks; i++) {
            rack_busy[i] = false;
            rack_next_free_time[i] = 0.0;
        }
    }
    
    void run() {
        std::cout << "Generating workload..." << std::endl;
        WorkloadGenerator wg(config);
        std::vector<Flow> flow_list = wg.generateFlows();
        
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
