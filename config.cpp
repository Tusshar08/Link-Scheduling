#include "config.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

Config load_config(const std::string& config_path) {
    Config config;
    
    try {
        // Read JSON file
        std::ifstream config_file(config_path);
        if (!config_file.is_open()) {
            throw std::runtime_error("Could not open config file: " + config_path);
        }
        
        json j;
        config_file >> j;
        config_file.close();
        
        // Validate and extract server config
        if (!j.contains("server")) {
            throw std::runtime_error("error: missing required field 'server'");
        }
        
        auto& server_obj = j["server"];
        
        // Check all required fields
        if (!server_obj.contains("ip")) {
            throw std::runtime_error("error: missing required field 'server.ip'");
        }
        if (!server_obj.contains("port")) {
            throw std::runtime_error("error: missing required field 'server.port'");
        }
        if (!server_obj.contains("server_threads")) {
            throw std::runtime_error("error: missing required field 'server.server_threads'");
        }
        if (!server_obj.contains("client_threads")) {
            throw std::runtime_error("error: missing required field 'server.client_threads'");
        }
        
        // Extract and validate types
        try {
            config.server.ip = server_obj["ip"].get<std::string>();
            config.server.port = server_obj["port"].get<int>();
            config.server.server_threads = server_obj["server_threads"].get<int>();
            config.server.client_threads = server_obj["client_threads"].get<int>();
        } catch (const std::exception& e) {
            throw std::runtime_error("error: invalid field type in config file");
        }
        
        return config;
        
    } catch (const json::exception& e) {
        throw std::runtime_error("error: malformed JSON in config file: " + std::string(e.what()));
    }
}
