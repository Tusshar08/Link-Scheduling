#pragma once

#include "request.h"
#include <cstdint>
#include <string>

bool serve_get(
    Request& req,
    const std::string& file_dir,
    std::uint64_t budget,
    std::uint64_t& bytes_processed,
    bool allow_long_line_overrun,
    int packetization);

bool serve_put(
    Request& req,
    const std::string& file_dir,
    std::uint64_t budget,
    std::uint64_t& bytes_processed);
