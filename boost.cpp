#include <algorithm>
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <unordered_set>

#include "Socket.h"
#include "definitions.h"

using namespace boost;

constexpr unsigned PORT = 8000;

std::unordered_set<std::shared_ptr<Socket>> conn_pool;

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

bool recv_message(asio::ip::tcp::socket &socket, std::string &message,
                  std::string *host_to_mutate = nullptr) {
  system::error_code err;
  auto header_len =
      asio::read_until(socket, asio::dynamic_buffer(message), "\r\n\r\n", err);
  if (err) {
    if (err.value() == asio::error::eof && !header_len) {
      return false;
    }
    puts("THIEF");
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

// TODO Delete later
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

void start_accept(asio::io_context &io_context,
                  asio::ip::tcp::acceptor &acceptor) {
  acceptor.async_accept(
      asio::make_strand(io_context),
      [&io_context, &acceptor](const system::error_code &ec,
                               asio::ip::tcp::socket socket) {
        if (ec) {
          puts("Error accepting..");
          throw system::system_error{ec};
        }
        std::cout << MAG << "New socket on port "
                  << socket.remote_endpoint().port() << RESET << std::endl;
        // auto new_entity =
        // std::make_shared<Entity>(io_context, std::move(socket), conn_pool);
        // new_entity->start();
        // conn_pool.insert(std::move(new_entity));
        (new Socket(io_context, std::move(socket), conn_pool))->start();
        start_accept(io_context, acceptor);
      });
}

int main(int argc, char *argv[]) {
  // std::size_t threads_num = 1;
  std::size_t threads_num = std::thread::hardware_concurrency();
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor{
      io_context, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), PORT}};
  try {
    start_accept(io_context, acceptor);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < threads_num; ++i) {
      threads.emplace_back([&io_context] { io_context.run(); });
    }

    printf("Listening on port %u\n", PORT);

    for (std::size_t i = 0; i < threads.size(); ++i) {
      threads[i].join();
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}
