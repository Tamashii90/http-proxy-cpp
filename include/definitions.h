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

std::string parse_field(std::string header_copy, std::string &&field_name);
Body identify_body(const std::string &http_header);
