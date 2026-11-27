#include "config.h"
#include <iostream>

int main() {
    try {
        Config cfg = load_config("config.json");
        std::cout << "IP: " << cfg.server.ip << std::endl;
        std::cout << "Port: " << cfg.server.port << std::endl;
        std::cout << "Threads: " << cfg.server.server_threads << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
