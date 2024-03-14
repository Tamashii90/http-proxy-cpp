// TODO use async boost::asio on different branch?
#include <algorithm>
#include <boost/asio.hpp>
#include <iostream>
#include <istream>
#include <ostream>
#include <regex>
#include <string>

using namespace boost;

constexpr unsigned PORT = 8000;

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

void to_lowercase(std::string &str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](auto c) { return std::tolower(c); });
}

bool connect_to_endpoint(asio::io_context &io_context,
                         asio::ip::tcp::socket &socket,
                         const std::string &host) {
  asio::ip::tcp::resolver resolver{io_context};
  system::error_code err;
  const auto endpoints = resolver.resolve(host, "http", err);
  if (err) {
    std::cout << RED << err.message() << ". "
              << "Host: [" << host << "]" << RESET << std::endl;
    return false;
  }
  asio::connect(socket, endpoints, err);
  if (err) {
    std::cout << RED << err.message() << RESET << std::endl;
    return false;
  }
  return true;
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

// TODO Deprecated?
/*******************************************************************************
void send_request(asio::ip::tcp::socket &client_socket, const std::string &host,
                  asio::io_context &io_context, const std::string &request) {
  asio::ip::tcp::resolver resolver{io_context};
  system::error_code err;
  const auto endpoints = resolver.resolve(host, "http", err);
  if (err) {
    std::cout << RED << err.message() << ". "
              << "Host: [" << host << "]" << RESET << std::endl;
    return;
  }
  asio::ip::tcp::socket socket{io_context};
  asio::connect(socket, endpoints);

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
  const std::string response_header{response_header_plus.substr(0, header_len)};
  asio::write(client_socket, asio::buffer(response_header));
  std::cout << GREEN << response_header << RESET << std::endl;

  if (identify_body(response_header) != Body::CONTENT_LENGTH) {
    std::cout << RED << "Whoops! Only content-length is supported for now."
              << RESET << std::endl;
    return;
  }
  auto content_length = stoul(parse_field(response_header, "content-length"));
  std::string response_body{response_header_plus.substr(
      header_len, response_header_plus.size() - header_len)};
  auto transferred = asio::read(
      socket, asio::dynamic_buffer(response_body, content_length), err);
  if (err && err.value() != asio::error::eof) {
    throw boost::system::system_error{err};
  }
  asio::write(client_socket, asio::buffer(response_body));
}
*******************************************************************************/

bool recv_message(asio::ip::tcp::socket &socket, std::string &message,
                  std::string *host_to_mutate = nullptr) {
  system::error_code err;
  auto header_len =
      asio::read_until(socket, asio::dynamic_buffer(message), "\r\n\r\n", err);
  if (err) {
    if (err.value() == asio::error::eof && !header_len) {
      return false;
    }
    throw boost::system::system_error{err};
  }
  if (host_to_mutate) {
    // Everything inside this block is exclusive to requests received from the
    // CLIENT
    std::string first_line = message.substr(0, message.find("\r\n"));
    std::istringstream iss{first_line};
    std::string method, url, http_version;
    iss >> method >> url >> http_version;
    // http_version looks like "HTTP/1.1"
    if (stod(http_version.substr(http_version.find("/") + 1)) > 1.1) {
      std::cerr << RED << "HTTP Version Not Supported" << RESET << std::endl;
      return false;
    }
    *host_to_mutate = parse_field(message, "host");
  }
  const std::string header{message.substr(0, header_len)};
  std::cout << (host_to_mutate ? YELLOW : GREEN) << header << RESET
            << std::endl;

  Body body_type = identify_body(header);
  if (body_type == Body::CONTENT_LENGTH) {
    auto content_length = stoul(parse_field(header, "content-length"));
    asio::read(socket,
               asio::dynamic_buffer(message, content_length + header_len), err);
  } else if (body_type == Body::CHUNKED) {
    asio::read_until(socket, asio::dynamic_buffer(message), "0\r\n\r\n", err);
  }
  if (err && err.value() != asio::error::eof) {
    throw boost::system::system_error{err};
  }
  return true;
}

// TODO handle errors in writes and reads (e.g. most writes don't have
// error_code in their arguments)
void new_handle(const std::shared_ptr<asio::ip::tcp::socket> &client_socket_p,
                asio::io_context &io_context) {
  asio::ip::tcp::socket &client_socket = *client_socket_p;
  asio::ip::tcp::socket server_socket{io_context};
  std::string message;
  std::string prev_host;
  while (client_socket.is_open()) {
    std::string curr_host;
    if (!recv_message(client_socket, message, &curr_host)) {
      break;
    }
    if (!server_socket.is_open() || prev_host != curr_host) {
      if (!connect_to_endpoint(io_context, server_socket, curr_host)) {
        break;
      }
    }
    asio::write(server_socket, asio::buffer(message));
    message.clear();
    if (!recv_message(server_socket, message, nullptr)) {
      break;
    }
    asio::write(client_socket, asio::buffer(message));
    message.clear();
    if (!server_socket.is_open()) {
      break;
    }
    prev_host = curr_host;
  }
  std::cout << RED << "Socket down" << RESET << std::endl;
  // TODO correct sockets breakdown?
  // client_socket.close();
}

// TODO Deprecated?
/*************************************************************************
void handle(asio::ip::tcp::socket &socket, asio::io_context &io_context) {
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
  send_request(socket, host, io_context, request_header);
}
*************************************************************************/

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
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor{
      io_context, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), PORT}};
  printf("Listening on port %u\n", PORT);
  while (true) {
    try {
      std::shared_ptr<asio::ip::tcp::socket> socket =
          std::make_shared<asio::ip::tcp::socket>(io_context);
      asio::ip::tcp::endpoint client_endpoint;
      acceptor.accept(*socket, client_endpoint);
      std::cout << MAG << "New socket on port " << client_endpoint.port()
                << RESET << std::endl;
      std::thread{new_handle, socket, std::ref(io_context)}.detach();
      // asio::post(thread_pool,
      // [socket, &thread_pool] { new_handle(socket, thread_pool); });
    } catch (std::exception &e) {
      std::cerr << e.what() << std::endl;
    }
  }
}
