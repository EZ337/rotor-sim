#include "simulator.h"
#include <iostream>
#include <iomanip>

Simulator::Simulator(const SimConfig& cfg) 
    : config(cfg), topology(cfg), current_time_us(0), 
      next_packet_id(0), total_bytes_transmitted(0), 
      DIRECT_THRESHOLD(cfg.queue_threshold) {
    
    rng.seed(cfg.random_seed + 1000); // Different seed from workload gen
    
    // Initialize rack state and VOQs
    for (int i = 0; i < config.num_racks; i++) {
        rack_voqs.emplace(i, VirtualOutputQueues(i, config.num_racks, config.queue_size_pkts));
        rack_busy[i] = false;
        rack_next_free_time[i] = 0.0;
    }
}

void Simulator::run() {
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

    // Set the sim end time
    end_time_us = config.sim_time_ms * 1000.0;
    
    while (!event_queue.empty()) {
        Event evt = event_queue.top();
        if (evt.time_us > end_time_us) // Stop simulation
        {
            std::cout << "Simulation: Next event time: " << evt.time_us << "us, exceeds endTime: "
                << end_time_us <<"us. Stopping\n" << std::endl;
            break; // stop simulation at configured time
        }
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

Statistics Simulator::getStatistics() const {
    return stats;
}

void Simulator::enqueuePacket(uint64_t packet_id, int current_rack) {
    Packet& pkt = packets[packet_id];
    VirtualOutputQueues& voq = rack_voqs.at(current_rack);
    bool queueSuccess = false;

    // Case 1: Packet on second hop (must reach final dst now)
    if (pkt.hop_count == 1) {
        pkt.current_dst = pkt.final_dst;
        queueSuccess = voq.enqueue(packet_id, pkt.final_dst, VoqType::NONLOCAL);
    } 
    // Case 2: Packet on first hop - decide direct vs VLB
    else {
        if (shouldUseDirect(pkt, current_rack)) {
            pkt.current_dst = pkt.final_dst;
            queueSuccess = voq.enqueue(packet_id, pkt.final_dst, VoqType::LOCAL);  // ← Fixed!
        } else {
            int intermediate = selectIntermediateRack(pkt.current_rack, pkt.final_dst);
            pkt.current_dst = intermediate;
            queueSuccess = voq.enqueue(packet_id, intermediate, VoqType::LOCAL);
        }
    }

    // Check enqueue success
    if (!queueSuccess) {
        pkt.dropped = true;
        stats.addDroppedPacket();
        return;
    }

    // If rack is not busy, start transmission
    if (!rack_busy[current_rack]) {
        startTransmission(current_rack);
    }
}


void Simulator::scheduleEvent(EventType type, double time, uint64_t id) {
    Event e;
    e.type = type;
    e.time_us = time;
    e.id = id;
    event_queue.push(e);
}

void Simulator::handleFlowArrival(uint64_t flow_id) {
    Flow& flow = flows[flow_id];
    
    // Create packets for this flow
    int num_packets = flow.getNumPackets(config.mtu_bytes);
    uint64_t remaining_bytes = flow.size_bytes;
    
    // For 2-hop VLB: randomly select intermediate rack for this flow
    std::uniform_int_distribution<int> rack_dist(0, config.num_racks - 1);
    int intermediate = -1;
    
    // Use VLB for skewed traffic or when configured
    // For simplicity: always use VLB for low-latency, probabilistic for bulk
    if (flow.type == FlowType::LOW_LATENCY) {
        // Always use 2-hop for low-latency
        assert(false && "LOW_LATENCY not used in this config");
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
        pkt.final_dst = flow.dst_rack;
        // pkt.current_dst = flow.src_rack;
        pkt.src_host = flow.src_host;
        pkt.dst_host = flow.dst_host;
        pkt.size_bytes = std::min((uint64_t)config.mtu_bytes, remaining_bytes);
        pkt.creation_time = current_time_us / 1000.0; // Convert to ms
        pkt.type = flow.type;
        pkt.dropped = false;
        pkt.hop_count = 0;
        pkt.current_rack = flow.src_rack;
        // Set randomly when we connect because we may have a direct connection insteda
        // of 2Hop each time
        // pkt.intermediate_rack = intermediate;
        // pkt.at_intermediate = false;
        
        remaining_bytes -= pkt.size_bytes;
        
        flow.packet_ids.push_back(pkt.id);
        packets[pkt.id] = pkt;
        
        // Enqueue packet at source rack
        enqueuePacket(pkt.id, flow.src_rack);
    }
}

void Simulator::startTransmission(int rack_id) {
    // This rack's voqs
    VirtualOutputQueues& myVoq = rack_voqs.at(rack_id);
    std::vector<int> localDests = myVoq.getNonemptyLocalDestinations();
    std::vector<int> nonLocalDests = myVoq.getNonemptyNonlocalDestinations();
    
    if (localDests.empty() && nonLocalDests.empty()) {
        rack_busy[rack_id] = false;
        return;
    }
    
    rack_busy[rack_id] = true;
    int selected_dest = -1;
    uint64_t packet_id = -1;    // filled in by VOQ::dequeue(dest, packet_id, voqType)
    VoqType selected_type;

    // Priority 1: Nonlocal packets with direct path (these are second hop traffic)
    for (int dest : nonLocalDests)
    {
        if (topology.hasDirectPath(rack_id, dest, current_time_us))
        {
            if (myVoq.dequeue(dest, packet_id, VoqType::NONLOCAL))
            {
                selected_dest = dest;
                selected_type = VoqType::NONLOCAL;
                // ready to transmit
            }
        }
    }

    if (selected_dest < 0)  // No second hop, direct traffic found
    {
        // PRIORITY 2: Local packets with direct path (these are direct connections)
        for (int dest : localDests)
        {
            if (topology.hasDirectPath(rack_id, dest, current_time_us))
            {
                if (myVoq.dequeue(dest, packet_id, VoqType::LOCAL))
                {
                    selected_dest = dest;
                    selected_type = VoqType::LOCAL;
                }
                // ready to transmit
            }
        }
    }

    if (selected_dest < 0)  // Still no direct traffic found. 
    {
        // We just re-enqueue and retry later. RotorNet buffers it
        rack_busy[rack_id] = false;
        return;
    }

    Packet& pkt = packets[packet_id];
    
    // There should not be any low_latency flows
    if (pkt.type == FlowType::LOW_LATENCY)
        assert(false && "LOW_LATENCY not used in this config");

    
    // Calculate transmission time
    double bits = pkt.size_bytes * 8.0;
    double tx_time_us = bits / (config.link_rate_gbps * 1e9) * 1e6;
    
    pkt.sent_time = current_time_us / 1000.0;
    
    scheduleEvent(EventType::PACKET_TRANSMISSION_COMPLETE,
                 current_time_us + tx_time_us, packet_id);
}

void Simulator::handlePacketTransmissionComplete(uint64_t packet_id) {
    Packet& pkt = packets[packet_id];
    int current_rack = pkt.current_rack;
    
    // Increment hop count BEFORE checking destination
    ++pkt.hop_count;
    
    // Add propagation delay
    double arrival_time = current_time_us + config.propagation_delay_us;
    
    // Determine packet's next location based on current_dst
    int next_rack = pkt.current_dst;
    
    // RECEIVE PATH LOGIC:
    // Case 1: Packet arrived at final destination
    if (next_rack == pkt.final_dst) {
        // Packet has reached its ultimate destination
        pkt.arrival_time = arrival_time / 1000.0;
        total_bytes_transmitted += pkt.size_bytes;
        
        // Update flow completion
        Flow& flow = flows[pkt.flow_id];
        flow.packets_received++;
        
        if (flow.packets_received == flow.packet_ids.size()) {
            flow.completed = true;
            flow.completion_time = pkt.arrival_time;
        }
    }
    // Case 2: Packet arrived at intermediate rack (not final destination)
    else {
        // INVARIANT: Update packet state for second hop
        // - current_dst becomes final_dst (packet now targets final destination)
        // - hop_count was already incremented above
        pkt.current_dst = pkt.final_dst;
        pkt.current_rack = next_rack;  // Update currentRack for next transmission
        
        // Schedule packet arrival at intermediate rack
        // It will be enqueued in nonlocal VOQ there
        if (arrival_time <= end_time_us)
            scheduleEvent(EventType::PACKET_ARRIVAL, arrival_time, packet_id);
        else
            std::cout << "PacketId " << pkt.id << " from flow " << pkt.flow_id << " from srcRack " << pkt.src_rack 
                << " to dstRack " << pkt.final_dst << "'s arrival time " << arrival_time << " at currentRack " 
                << pkt.current_rack << " will exceed endtime " << end_time_us << "us. Not queuing arrival event"
                << std::endl;
    }
    
    // Start next transmission at the rack we just left
    rack_next_free_time[current_rack] = current_time_us;
    startTransmission(current_rack);
}

void Simulator::handlePacketArrival(uint64_t packet_id) {
    Packet& pkt = packets[packet_id];
    int current_rack = pkt.current_rack;

    // Packet arrived at intermediate rack after first hop
    if (pkt.hop_count == 1 && current_rack != pkt.final_dst)
    {
        // Enqueu in NONLOCAL VOQ (This rack will forward it to 2nd hop (which should be final dst))
        pkt.current_dst = pkt.final_dst;
        VirtualOutputQueues& voq = rack_voqs.at(current_rack);
        if (!voq.enqueue(packet_id, pkt.final_dst, VoqType::NONLOCAL))
        {
            pkt.dropped = true;
            stats.addDroppedPacket();
            return;
        }
    }

    // Start transmitting 
    if (!rack_busy[current_rack])
    {
        startTransmission(current_rack);
    }
}

bool Simulator::shouldUseDirect(const Packet& pkt, int current_rack) 
{
    double direct_wait = topology.getNextDirectPathTime(
        current_rack, pkt.final_dst, current_time_us) - current_time_us;
    
    // If direct path available very soon (< slot time), use it
    if (direct_wait < config.getSlotTime()) {
        return true;
    }
    
    // Check if direct queue is heavily loaded
    size_t direct_queue = rack_voqs.at(current_rack).getLocalQueueSize(pkt.final_dst);
    if (direct_queue > DIRECT_THRESHOLD) {
        return false; // Too congested, try VLB
    }
    
    return true; // Default to direct
}

int Simulator::selectIntermediateRack(int src, int dst) {
    std::uniform_int_distribution<int> dist(0, config.num_racks - 1);
    int intermediate;
    do {
        intermediate = dist(rng);
    } while (intermediate == src || intermediate == dst);
    return intermediate;
}
