#include <algorithm>
#include <boost/asio.hpp>
#include <iostream>
#include <string>

#include "Socket.h"
#include "definitions.h"

using namespace boost;

constexpr unsigned PORT = 8000;

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

void start_accept(asio::io_context &io_context,
                  asio::ip::tcp::acceptor &acceptor) {
  acceptor.async_accept(
      // asio::make_strand(io_context),
      [&io_context, &acceptor](const system::error_code &ec,
                               asio::ip::tcp::socket socket) {
        if (ec) {
          puts("Error accepting..");
          throw system::system_error{ec};
        }
        std::cout << MAG << "New socket on port "
                  << socket.remote_endpoint().port() << RESET << std::endl;
        auto new_socket_p =
            std::make_shared<Socket>(io_context, std::move(socket));
        new_socket_p->start();
        start_accept(io_context, acceptor);
      });
}

int main(int argc, char *argv[]) {
  // std::size_t threads_num = 10;
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
