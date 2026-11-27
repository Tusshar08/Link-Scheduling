#pragma once

#include <string>
#include <vector>

struct Config {
    struct ServerConfig {
        std::string ip;
        int port = 0;
        int server_threads = 0;
        int client_threads = 0;
    } server;

    struct BackendConfig {
        std::string ip;
        int port = 0;
    };

    struct LoadBalancerConfig {
        std::string ip;
        int port = 0;
        int health_interval_ms = 0;
        std::vector<BackendConfig> backends;
    } load_balancer;

    bool has_load_balancer = false;
};

Config load_config(
    const std::string& config_path,
    bool require_load_balancer = false);
