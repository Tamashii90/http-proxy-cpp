#pragma once

#include <boost/asio.hpp>
#include <string>
constexpr const char *RED = "\x1B[31m";
constexpr const char *GREEN = "\x1B[32m";
constexpr const char *YELLOW = "\x1B[33m";
constexpr const char *BLU = "\x1B[34m";
constexpr const char *MAG = "\x1B[35m";
constexpr const char *CYN = "\x1B[36m";
constexpr const char *WHT = "\x1B[37m";
constexpr const char *RESET = "\x1B[0m";

enum class Body { CONTENT_LENGTH, CHUNKED, NONE };

std::string parse_field(const std::string &http_header,
                        std::string &&field_name);

bool recv_message(boost::asio::ip::tcp::socket &socket, std::string &message,
                  std::string *host_to_mutate);

bool connect_to_endpoint(boost::asio::io_context &io_context,
                         boost::asio::ip::tcp::socket &socket,
                         const std::string &host);
Body identify_body(const std::string &http_header);
