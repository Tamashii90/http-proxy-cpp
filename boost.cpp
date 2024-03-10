#include <algorithm>
#include <boost/asio.hpp>
#include <iostream>
#include <istream>
#include <ostream>
#include <regex>
#include <string>

constexpr unsigned PORT = 8000;

constexpr const char *RED = "\x1B[31m";
constexpr const char *GREEN = "\x1B[32m";
constexpr const char *YELLOW = "\x1B[33m";
constexpr const char *BLU = "\x1B[34m";
constexpr const char *MAG = "\x1B[35m";
constexpr const char *CYN = "\x1B[36m";
constexpr const char *WHT = "\x1B[37m";
constexpr const char *RESET = "\x1B[0m";

using namespace boost;

std::string parse_field(const std::string &http_header,
                        std::string &&field_name);

void to_lowercase(std::string &str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](auto c) { return std::tolower(c); });
}

// TODO handle errors in writes and reads (e.g. most writes don't have
// error_code in their arguments)
void request(asio::ip::tcp::socket &client_socket, const std::string &host,
             asio::io_context &io_context, const std::string &request) {
  asio::ip::tcp::resolver resolver{io_context};
  system::error_code err;
  const auto endpoints = resolver.resolve(host, "http", err);
  if (err) {
    std::cout << RED << err.message() << "\n"
              << "Host: [" << host << "]" << RESET << std::endl;
    return;
  }
  asio::ip::tcp::socket socket{io_context};
  const auto connected_endpoint = asio::connect(socket, endpoints);

  asio::write(socket, asio::buffer(request));

  std::string response_header_plus;
  // CAUTION read_until might read past the delimiter, but returns real length
  auto header_len = asio::read_until(
      socket, asio::dynamic_buffer(response_header_plus), "\r\n\r\n", err);
  if (err && err.value() != asio::error::eof) {
    throw boost::system::system_error{err};
  }

  // After reading, header_len <= response_header.size() because of potential
  // extra characters
  asio::write(client_socket, asio::buffer(response_header_plus, header_len));
  std::cout << GREEN << response_header_plus.substr(0, header_len) << RESET
            << std::endl;

  /*****************************************************************
  // TODO Does this block successfully replace parse_field()?
  // Do NOT transform everything! There could be parts related to the
  // response_BODY
  std::transform(response_header.begin(), response_header.begin() + header_len,
                 response_header.begin(),
                 [](auto c) { return std::tolower(c); });

  // Parse content-length field
  auto len_field_beg =
      response_header.find("content-length:") + strlen("content-length:");
  auto len_field_end = response_header.find("\r\n", len_field_beg);
  auto content_length = stoul(
      response_header.substr(len_field_beg, len_field_end - len_field_beg));
  *****************************************************************/

  // TODO handle chunked messages
  auto content_length = stoul(parse_field(
      response_header_plus.substr(0, header_len), "content-length"));
  std::string response_body{response_header_plus.substr(
      header_len, response_header_plus.size() - header_len)};
  auto transferred = asio::read(
      socket, asio::dynamic_buffer(response_body, content_length), err);
  if (err && err.value() != asio::error::eof) {
    throw boost::system::system_error{err};
  }
  asio::write(client_socket, asio::buffer(response_body));
}

void handle(asio::ip::tcp::socket &socket, asio::io_context &io_context) {
  // TODO handle requests that have a body (e.g. POST)
  std::string request_header;
  system::error_code err;
  asio::read_until(socket, asio::dynamic_buffer(request_header), "\r\n\r\n");
  if (err && err.value() != asio::error::eof) {
    throw boost::system::system_error{err};
  }
  std::cout << YELLOW << request_header << RESET << std::endl;
  std::string first_line =
      request_header.substr(0, request_header.find("\r\n"));
  std::istringstream iss{first_line};
  std::string method, url, http_version;
  iss >> method >> url >> http_version;
  // http_version looks like "HTTP/1.1"
  if (stod(http_version.substr(http_version.find("/") + 1)) > 1.1) {
    std::cerr << RED << "HTTP Version Not Supported" << RESET << std::endl;
    return;
  }
  const std::string host = parse_field(request_header, "host");
  request(socket, host, io_context, request_header);
}

std::string parse_field(const std::string &http_header_, std::string &&field) {
  std::string http_header{http_header_};
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

int main() {
  try {
    asio::io_context io_context;
    asio::ip::tcp::acceptor acceptor{
        io_context, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), PORT}};
    printf("Listening on port %u\n", PORT);
    while (true) {
      asio::ip::tcp::socket socket{io_context};
      asio::ip::tcp::endpoint client_endpoint;
      acceptor.accept(socket, client_endpoint);
      std::cout << "New client! on port " << client_endpoint.port()
                << std::endl;
      handle(socket, io_context);
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}
