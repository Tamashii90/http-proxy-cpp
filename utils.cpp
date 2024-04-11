#include "utils.h"

void to_lowercase(std::string &str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](auto c) { return std::tolower(c); });
}

// case-insensitive version of str.find()
bool find_ci(const std::string &haystack, const std::string &needle) {
  auto it = std::search(haystack.begin(), haystack.end(), needle.begin(),
                        needle.end(), [](unsigned char ch1, unsigned char ch2) {
                          return std::tolower(ch1) == std::tolower(ch2);
                        });
  return it != haystack.end();
}

Body identify_body(const std::string &http_header) {
  if (find_ci(http_header, "content-length")) {
    return Body::CONTENT_LENGTH;
  }
  if (find_ci(http_header, "chunked")) {
    return Body::CHUNKED;
  }
  return Body::NONE;
}

std::string parse_field(std::string http_header, std::string &&field) {
  to_lowercase(http_header);
  to_lowercase(field);
  // There's a colon at the end of field names
  auto len_field_beg = http_header.find(field) + field.size() + strlen(":");
  auto len_field_end = http_header.find("\r\n", len_field_beg);
  std::string result =
      http_header.substr(len_field_beg, len_field_end - len_field_beg);
  // trim both sides
  if (result.find(" ") != std::string::npos) {
    result.erase(result.find(" "), result.find_first_not_of(" "));
  }
  if (result.find(" ") != std::string::npos) {
    result.erase(result.begin() + result.find(" "),
                 result.begin() + result.find_first_not_of(" "));
  }
  return result;
}
