#include "server.h"

#include "file_io.h"
#include "protocol.h"
#include "socket.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

bool serve_get(
    Request& req,
    const std::string& file_dir,
    std::uint64_t budget,
    std::uint64_t& bytes_processed,
    bool allow_long_line_overrun,
    int packetization)
{
    bytes_processed = 0;

    if (!is_valid_filename(req.filename)) {
        send_error(req.client_fd, "invalid filename");
        return false;
    }

    const std::string filepath = file_dir + "/" + req.filename;

    if (!file_exists(filepath)) {
        std::cerr << "File not found: " << filepath << std::endl;
        send_error(req.client_fd, "file not found");
        return false;
    }

    const std::uint64_t file_size =
        static_cast<std::uint64_t>(get_file_size(filepath));

    req.total_bytes = file_size;

    // Send the GET response header only once.
    if (!req.response_sent) {
        send_response(req.client_fd, file_size);
        req.response_sent = true;

        std::cout
            << "  GET response: "
            << file_size
            << " bytes"
            << std::endl;
    }

    if (req.offset >= file_size || budget == 0) {
        return true;
    }

    if (packetization <= 0) {
        send_error(req.client_fd, "invalid packetization");
        return false;
    }

    const std::uint64_t remaining = file_size - req.offset;

    /*
     * FCFS/SJF give the complete remaining request as their budget.
     * RR/DRR use line-based scheduling.
     */
    const bool complete_request_round = (budget >= remaining);

    std::uint64_t round_bytes = 0;

    if (complete_request_round) {
        /*
         * The entire remaining request fits in this scheduling round.
         */
        round_bytes = remaining;
    } else {
        /*
         * RR and DRR:
         * Select only complete lines that fit inside the allowance.
         */
        std::size_t line_offset =
            static_cast<std::size_t>(req.offset);

        while (round_bytes < budget) {
            std::string line =
                read_file_line(filepath, line_offset);

            if (line.empty()) {
                break;
            }

            const std::uint64_t line_size =
                static_cast<std::uint64_t>(line.size());

            /*
             * A14:
             * RR may send one line larger than Q.
             * DRR must wait until its deficit covers the line.
             */
            if (round_bytes == 0 && line_size > budget) {
                if (allow_long_line_overrun) {
                    round_bytes = line_size;
                }

                break;
            }

            /*
             * Never split a GET line between scheduling rounds.
             */
            if (round_bytes + line_size > budget) {
                break;
            }

            round_bytes += line_size;
        }
    }

    /*
     * DRR may need several rounds before a long line fits.
     */
    if (round_bytes == 0) {
        std::cout
            << "  No complete line fits this round"
            << " (offset "
            << req.offset
            << "/"
            << file_size
            << ")"
            << std::endl;

        return true;
    }

    /*
     * Re-open and seek to the exact byte offset.
     */
    std::ifstream file(filepath, std::ios::binary);

    if (!file.is_open()) {
        send_error(req.client_fd, "cannot open file");
        return false;
    }

    file.seekg(
        static_cast<std::streamoff>(req.offset),
        std::ios::beg
    );

    if (!file.good()) {
        file.close();
        send_error(req.client_fd, "file seek error");
        return false;
    }

    /*
     * A8:
     * Gather up to `packetization` complete lines into one write.
     *
     * Packetization changes only the grouping of writes. It does not
     * change the selected bytes, their order, or scheduling accounting.
     */
    std::size_t line_offset =
        static_cast<std::size_t>(req.offset);

    std::uint64_t remaining_round = round_bytes;

    while (remaining_round > 0) {
        std::string packet;
        std::size_t lines_in_packet = 0;

        while (lines_in_packet <
                   static_cast<std::size_t>(packetization) &&
               remaining_round > 0) {

            std::string line =
                read_file_line(filepath, line_offset);

            if (line.empty()) {
                file.close();
                send_error(req.client_fd, "file read error");
                return false;
            }

            const std::uint64_t line_size =
                static_cast<std::uint64_t>(line.size());

            /*
             * Do not allow this packet to exceed the selected
             * scheduling round.
             */
            if (line_size > remaining_round) {
                file.close();
                send_error(req.client_fd, "packetization error");
                return false;
            }

            packet.append(line);
            remaining_round -= line_size;
            ++lines_in_packet;
        }

        /*
         * One send_all() call represents one packetization group.
         */
        if (!send_all(
                req.client_fd,
                packet.data(),
                packet.size())) {

            file.close();
            std::cerr << "Error sending file" << std::endl;
            return false;
        }

        bytes_processed +=
            static_cast<std::uint64_t>(packet.size());
    }

    file.close();

    std::cout
        << "  Sent " << bytes_processed << " bytes this round"
        << " (offset " << req.offset + bytes_processed
        << "/" << file_size << ")"
        << std::endl;

    return true;
}

bool serve_put(
    Request& req,
    const std::string& file_dir,
    std::uint64_t budget,
    std::uint64_t& bytes_processed)
{
    bytes_processed = 0;

    if (!is_valid_filename(req.filename)) {
        send_error(req.client_fd, "invalid filename");
        return false;
    }

    const std::string filepath = file_dir + "/" + req.filename;

    std::cout
        << "  PUT request: "
        << req.total_bytes
        << " bytes incoming"
        << std::endl;

    std::cout
        << "  Saving to: "
        << filepath
        << std::endl;

    // A zero-byte PUT is valid.
    if (req.total_bytes == 0) {
        return true;
    }

    if (req.offset == 0) {
        // First round: create/truncate the destination.
        std::ofstream create_file(
            filepath,
            std::ios::binary | std::ios::trunc
        );

        if (!create_file.is_open()) {
            send_error(req.client_fd, "cannot create file");
            return false;
        }

        create_file.close();
    }

    std::fstream file(
        filepath,
        std::ios::binary |
        std::ios::in |
        std::ios::out
    );

    if (!file.is_open()) {
        send_error(req.client_fd, "cannot open file");
        return false;
    }

    file.seekp(
        static_cast<std::streamoff>(req.offset),
        std::ios::beg
    );

    if (!file.good()) {
        file.close();
        send_error(req.client_fd, "file seek error");
        return false;
    }

    const std::uint64_t remaining =
        req.total_bytes - req.offset;

    const std::uint64_t to_receive =
        std::min(budget, remaining);

    const std::size_t CHUNK_SIZE = 8192;

    std::vector<char> buffer(
        std::min<std::uint64_t>(CHUNK_SIZE, to_receive)
    );

    std::uint64_t remaining_round = to_receive;

    while (remaining_round > 0) {
        const std::size_t chunk =
            static_cast<std::size_t>(
                std::min<std::uint64_t>(
                    buffer.size(),
                    remaining_round
                )
            );

        const ssize_t received =
            recv_data_with_timeout(
                req.client_fd,
                buffer.data(),
                chunk,
                5000
            );

        if (received < 0) {
            file.close();
            send_error(req.client_fd, "receive error");
            return false;
        }

        if (received == 0) {
            file.close();
            send_error(req.client_fd, "incomplete upload");
            return false;
        }

        file.write(
            buffer.data(),
            static_cast<std::streamsize>(received)
        );

        if (!file.good()) {
            file.close();
            send_error(req.client_fd, "file write error");
            return false;
        }

        bytes_processed +=
            static_cast<std::uint64_t>(received);

        remaining_round -=
            static_cast<std::uint64_t>(received);
    }

    file.close();

    std::cout
        << "  Received "
        << bytes_processed
        << " bytes this round"
        << " (offset "
        << req.offset + bytes_processed
        << "/"
        << req.total_bytes
        << ")"
        << std::endl;

    return true;
}
