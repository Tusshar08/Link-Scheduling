#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Config {
    struct ServerConfig {
        std::string ip;
        int port;
        int server_threads;
        int client_threads;
    } server;
};

// Function to load and parse config
Config load_config(const std::string& config_path);

