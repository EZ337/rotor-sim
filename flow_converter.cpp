// flow_converter.cpp - Convert between Opera-sim and RotorNet flow formats
// Compile: g++ -std=c++17 -O3 flow_converter.cpp -o flow_converter

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Flow {
    uint64_t id;
    int src_rack;
    int dst_rack;
    int src_host;
    int dst_host;
    uint64_t size_bytes;
    double start_time_ms;
    std::string flow_type;
};

void convertOperaToRotorNet(const std::string& input_file, const std::string& output_file) {
    std::ifstream infile(input_file);
    std::ofstream outfile(output_file);
    
    if (!infile.is_open()) {
        std::cerr << "Cannot open input file: " << input_file << std::endl;
        return;
    }
    
    if (!outfile.is_open()) {
        std::cerr << "Cannot open output file: " << output_file << std::endl;
        return;
    }
    
    // Opera-sim format (from opera-sim/workload.cpp):
    // src_host dst_host size_bytes start_time_ns
    // We need to map hosts to racks
    
    std::string line;
    std::vector<Flow> flows;
    uint64_t flow_id = 0;
    
    // Assume 32 hosts per rack by default
    int hosts_per_rack = 32;
    
    std::cout << "Reading Opera-sim format..." << std::endl;
    
    while (std::getline(infile, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::stringstream ss(line);
        Flow flow;
        
        int src_host_global, dst_host_global;
        uint64_t start_time_ns;
        
        ss >> src_host_global >> dst_host_global >> flow.size_bytes >> start_time_ns;
        
        flow.id = flow_id++;
        flow.src_rack = src_host_global / hosts_per_rack;
        flow.src_host = src_host_global % hosts_per_rack;
        flow.dst_rack = dst_host_global / hosts_per_rack;
        flow.dst_host = dst_host_global % hosts_per_rack;
        flow.start_time_ms = start_time_ns / 1e6; // ns to ms
        
        // Classify based on size (15 MB threshold)
        flow.flow_type = (flow.size_bytes >= 15e6) ? "bulk" : "low_latency";
        
        flows.push_back(flow);
    }
    
    infile.close();
    
    // Write RotorNet format
    std::cout << "Writing RotorNet format..." << std::endl;
    outfile << "flow_id,src_rack,dst_rack,src_host,dst_host,size_bytes,start_time_ms,flow_type\n";
    
    for (const auto& flow : flows) {
        outfile << flow.id << ","
                << flow.src_rack << ","
                << flow.dst_rack << ","
                << flow.src_host << ","
                << flow.dst_host << ","
                << flow.size_bytes << ","
                << flow.start_time_ms << ","
                << flow.flow_type << "\n";
    }
    
    outfile.close();
    std::cout << "Converted " << flows.size() << " flows" << std::endl;
}

void convertRotorNetToOpera(const std::string& input_file, const std::string& output_file) {
    std::ifstream infile(input_file);
    std::ofstream outfile(output_file);
    
    if (!infile.is_open()) {
        std::cerr << "Cannot open input file: " << input_file << std::endl;
        return;
    }
    
    if (!outfile.is_open()) {
        std::cerr << "Cannot open output file: " << output_file << std::endl;
        return;
    }
    
    int hosts_per_rack = 32;
    
    std::string line;
    std::getline(infile, line); // Skip header
    
    std::cout << "Reading RotorNet format..." << std::endl;
    int count = 0;
    
    while (std::getline(infile, line)) {
        std::stringstream ss(line);
        std::string field;
        
        uint64_t flow_id;
        int src_rack, dst_rack, src_host, dst_host;
        uint64_t size_bytes;
        double start_time_ms;
        
        std::getline(ss, field, ','); flow_id = std::stoull(field);
        std::getline(ss, field, ','); src_rack = std::stoi(field);
        std::getline(ss, field, ','); dst_rack = std::stoi(field);
        std::getline(ss, field, ','); src_host = std::stoi(field);
        std::getline(ss, field, ','); dst_host = std::stoi(field);
        std::getline(ss, field, ','); size_bytes = std::stoull(field);
        std::getline(ss, field, ','); start_time_ms = std::stod(field);
        
        // Convert to global host IDs
        int src_host_global = src_rack * hosts_per_rack + src_host;
        int dst_host_global = dst_rack * hosts_per_rack + dst_host;
        uint64_t start_time_ns = static_cast<uint64_t>(start_time_ms * 1e6);
        
        // Opera-sim format: src_host dst_host size_bytes start_time_ns
        outfile << src_host_global << " " 
                << dst_host_global << " "
                << size_bytes << " "
                << start_time_ns << "\n";
        
        count++;
    }
    
    infile.close();
    outfile.close();
    
    std::cout << "Converted " << count << " flows" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <mode> <input_file> <output_file>" << std::endl;
        std::cout << "Modes:" << std::endl;
        std::cout << "  opera2rotor  - Convert Opera-sim format to RotorNet format" << std::endl;
        std::cout << "  rotor2opera  - Convert RotorNet format to Opera-sim format" << std::endl;
        return 1;
    }
    
    std::string mode = argv[1];
    std::string input_file = argv[2];
    std::string output_file = argv[3];
    
    if (mode == "opera2rotor") {
        convertOperaToRotorNet(input_file, output_file);
    } else if (mode == "rotor2opera") {
        convertRotorNetToOpera(input_file, output_file);
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }
    
    return 0;
}
