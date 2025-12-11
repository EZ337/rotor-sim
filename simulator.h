// simulator.h - Main simulation engine
#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <queue>
#include <map>
#include <memory>
#include <random>
#include <assert.h>
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

using VoqType = VirtualOutputQueues::VoqType;

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
    int DIRECT_THRESHOLD;
    
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> event_queue;
    
    std::map<uint64_t, Flow> flows;
    std::map<uint64_t, Packet> packets;
    
    double current_time_us;
    double end_time_us;
    uint64_t next_packet_id;
    
    // VOQ at each rack
    std::map<int, VirtualOutputQueues> rack_voqs;
    std::map<int, bool> rack_busy; // Is rack currently transmitting?
    std::map<int, double> rack_next_free_time;
    
    uint64_t total_bytes_transmitted;
    
    void scheduleEvent(EventType type, double time, uint64_t id);
    void handleFlowArrival(uint64_t flow_id);
    void enqueuePacket(uint64_t packet_id, int current_rack);
    void startTransmission(int rack_id);
    void handlePacketTransmissionComplete(uint64_t packet_id);
    void handlePacketArrival(uint64_t packet_id);
    /// Gets whether this packet at current rack should try direct connection
    /// based on Rotor Principle of if waitTime < slotTime. Defaults to true.
    /// returns false if localQueueSize > DIRECT_THRESHOLD
    bool shouldUseDirect(const Packet& pkt, int current_rack);
    int selectIntermediateRack(int src, int dst);

public:
    Simulator(const SimConfig& cfg);
    
    void run();
    
    Statistics getStatistics() const;
};

#endif // SIMULATOR_H
