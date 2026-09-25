#include "file_io.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <algorithm>

bool is_valid_filename(const std::string& filename) {
    // Reject empty
    if (filename.empty()) return false;
    
    // Reject . and ..
    if (filename == "." || filename == "..") return false;
    
    // Reject / in filename
    if (filename.find('/') != std::string::npos) return false;
    
    return true;
}

bool is_path_safe(const std::string& filename) {
    // Simple check: filename shouldn't have path separators
    return is_valid_filename(filename);
}

bool file_exists(const std::string& filepath) {
    struct stat buffer;
    return (stat(filepath.c_str(), &buffer) == 0);
}

size_t get_file_size(const std::string& filepath) {
    struct stat buffer;
    if (stat(filepath.c_str(), &buffer) == 0) {
        return buffer.st_size;
    }
    return 0;
}

std::vector<uint8_t> read_file_chunk(const std::string& filepath, 
                                      size_t offset, 
                                      size_t bytes) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    
    file.seekg(offset);
    std::vector<uint8_t> buffer(bytes);
    file.read(reinterpret_cast<char*>(buffer.data()), bytes);
    
    size_t bytes_read = file.gcount();
    buffer.resize(bytes_read);
    
    return buffer;
}

bool read_file_line(
    const std::string& filepath,
    size_t& offset,
    std::string& line) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(static_cast<std::streamoff>(offset));

    if (!file.good()) {
        return false;
    }

    line.clear();

    if (!std::getline(file, line)) {
        return false;
    }

    std::streampos new_position = file.tellg();

    if (new_position == std::streampos(-1)) {
        // EOF reached.
        offset = get_file_size(filepath);
    } else {
        offset = static_cast<size_t>(new_position);
    }

    // getline() removes the newline, so add it back
    // when the original line had a newline.
    if (!file.eof()) {
        line += '\n';
    }

    return true;
}

bool write_file_data(const std::string& filepath, const std::vector<uint8_t>& data) {
    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);

    if (!file.is_open()) {
        return false;
    }

    if (!data.empty()) {
        file.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size())
        );
    }

    if (!file.good()) {
        file.close();
        return false;
    }

    file.close();

    return true;
}


