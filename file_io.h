#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Validate filename (no /, ., ..)
bool is_valid_filename(const std::string& filename);

// Get file size
size_t get_file_size(const std::string& filepath);

// Read chunk from file
std::vector<uint8_t> read_file_chunk(const std::string& filepath, 
                                     size_t offset, 
                                     size_t bytes);

// Read line from file (for GET with line boundaries)
std::string read_file_line(const std::string& filepath, size_t& offset);

// Write data to file
bool write_file_data(const std::string& filepath, const std::vector<uint8_t>& data);

// Check if file exists
bool file_exists(const std::string& filepath);

// Validate filename safety
bool is_path_safe(const std::string& filename);

