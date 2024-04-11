#include <boost/asio.hpp>
#include <chrono>
#include <iostream>
#include <unordered_set>

using namespace boost;

#include "Socket.h"
#include "definitions.h"

Socket::Socket(asio::io_context &io_context, asio::ip::tcp::socket &&socket)
    : strand{asio::make_strand(io_context)}, resolver{strand},
      client_socket{std::move(socket)}, server_socket{strand},
      timeout{std::chrono::seconds(15)}, timer{strand, timeout}, stopped{
                                                                     false} {}

void Socket::start() {
  auto self(shared_from_this());
  timer.async_wait(
      std::bind(&Socket::handle_wait, this, asio::placeholders::error, self));
  get_message_from_client();
}

void Socket::handle_wait(const system::error_code &ec,
                         std::shared_ptr<Socket> self) {
  if (stopped) {
    return;
  }
  if (!ec) {
    try {
      std::cout << "Socket timed out on port "
                << client_socket.remote_endpoint().port() << std::endl;
      close();
    } catch (const system::system_error &e) {
      std::cerr << e.what() << "\n"
                << "POINTERS: " << shared_from_this().use_count() << std::endl;
    }
  } else if (ec.value() == asio::error::operation_aborted) {
    // If cancelled, restart the timeout duration
    timer.async_wait(
        std::bind(&Socket::handle_wait, this, asio::placeholders::error, self));
  } else {
    std::cerr << ec.what() << std::endl;
  }
}

void Socket::get_message_from_client() {
  auto self(shared_from_this());
  mutex.lock();
  messages.emplace_back(std::make_pair("", ""));
  size_t msg_id = messages.size() - 1;
  mutex.unlock();
  std::string &request = messages[msg_id].first;
  // timer.expires_after(timeout);
  asio::async_read_until(
      client_socket, asio::dynamic_buffer(request), "\r\n\r\n",
      [this, self, &request, msg_id](const system::error_code &ec,
                                     std::size_t header_len) {
        timer.cancel();
        if (stopped) {
          return;
        }
        if (ec) {
          if (ec.value() == asio::error::operation_aborted) {
            puts("kansol client");
            return;
          }
          if (ec.value() == asio::error::eof && !header_len) {
            puts("connection closed by client");
            close();
            return;
          }
          puts("OOPS CLIENT");
          throw system::system_error{ec};
        }
        std::string first_line = request.substr(0, request.find("\r\n"));
        std::istringstream iss{first_line};
        std::string method, url, http_version;
        iss >> method >> url >> http_version;
        // http_version looks like "HTTP/1.1"
        if (stod(http_version.substr(http_version.find("/") + 1)) > 1.1) {
          std::cerr << RED << "HTTP Version Not Supported" << RESET
                    << std::endl;
          return;
        }
        const std::string header{request.substr(0, header_len)};
        std::cout << YELLOW << client_socket.remote_endpoint().port() << "\n"
                  << header << RESET << std::endl;
        curr_host = parse_field(request, "host");
        read_body(client_socket, request, header,
                  [self, this, msg_id] { resolve_server(msg_id); });
      });
}

void Socket::read_body(asio::ip::tcp::socket &socket,
                       std::string &http_header_plus,
                       const std::string &http_header,
                       std::function<void()> callback) {
  auto self(shared_from_this());
  // I need to make an empty buffer because the
  // header might have something like "Age: 0/r/n/r/n"
  // causing read_until to unluckily read 0 bits
  std::shared_ptr<std::string> http_body{std::make_shared<std::string>("")};
  if (stopped) {
    puts("body STOPPUH");
    return;
  }
  Body body_type = identify_body(http_header);
  if (body_type == Body::NONE) {
    callback();
    return;
  }
  auto shared_handler = [self, callback, http_body, &http_header_plus](
                            const system::error_code &ec, std::size_t bytes) {
    http_header_plus.append(*http_body);
    if (!ec) {
      callback();
    } else if (ec.value() == asio::error::eof) {
      puts("body EOF");
    } else if (ec.value() == asio::error::operation_aborted) {
      puts("body ABORRR");
    } else {
      throw system::system_error{ec};
    }
  };
  if (body_type == Body::CONTENT_LENGTH) {
    auto content_length = stoul(parse_field(http_header, "content-length"));
    size_t header_plus_len = http_header_plus.size();
    size_t header_len = http_header.size();
    asio::async_read(
        socket,
        asio::dynamic_buffer(*http_body,
                             content_length - (header_plus_len - header_len)),
        shared_handler);
  } else if (body_type == Body::CHUNKED) {
    // might have already read the whole body
    if (http_header_plus.find("0\r\n\r\n", http_header.size()) !=
        std::string::npos) {
      callback();
    } else {
      asio::async_read_until(socket, asio::dynamic_buffer(*http_body),
                             "0\r\n\r\n", shared_handler);
    }
  }
}

void Socket::resolve_server(size_t msg_id) {
  auto self(shared_from_this());
  if (server_socket.is_open() && prev_host == curr_host) {
    send_message_to_server(msg_id);
    return;
  }
  resolver.async_resolve(
      curr_host, "http",
      [self, this, msg_id](const system::error_code &ec,
                           asio::ip::tcp::resolver::results_type endpoints) {
        if (stopped) {
          return;
        }
        if (ec) {
          std::cout << RED << ec.message() << ". "
                    << "Host: [" << curr_host << "] " << RESET << std::endl;
          close();
        } else {
          connect_to_endpoints(msg_id, endpoints);
        }
      });
}

void Socket::connect_to_endpoints(
    size_t msg_id, asio::ip::tcp::resolver::results_type &endpoints) {
  auto self(shared_from_this());
  asio::async_connect(
      server_socket, endpoints,
      [self, this, msg_id](const system::error_code &ec,
                           const asio::ip::tcp::endpoint &endpoint) {
        if (stopped) {
          return;
        }
        if (ec) {
          // std::cout << RED << ec.message() << " "
          // << client_socket.remote_endpoint().port() << RESET
          // << std::endl;
          close();
        } else {
          send_message_to_server(msg_id);
        }
      });
}

void Socket::send_message_to_server(size_t msg_id) {
  auto self(shared_from_this());
  std::string &msg = messages[msg_id].first;
  asio::async_write(server_socket, asio::buffer(msg),
                    [self, this, msg_id](const system::error_code ec,
                                         const std::size_t bytes) {
                      if (stopped) {
                        puts("Server:LET ME GOO");
                        return;
                      }
                      if (ec) {
                        puts("BLA");
                        throw boost::system::system_error{ec};
                      }
                      get_message_from_server(msg_id);
                    });
}

void Socket::get_message_from_server(size_t msg_id) {
  auto self(shared_from_this());
  std::string &reply = messages[msg_id].second;
  asio::async_read_until(
      server_socket, asio::dynamic_buffer(reply), "\r\n\r\n",
      [self, this, msg_id, &reply](system::error_code ec,
                                   std::size_t header_len) {
        if (stopped) {
          puts("Get server: STOPPUH");
          return;
        }
        if (ec) {
          if (ec.value() == asio::error::operation_aborted) {
            puts("kansol server");
            return;
          }
          if (ec.value() == asio::error::eof && !header_len) {
            puts("connection closed by server");
            return;
          }
          if (ec.value() == asio::error::connection_reset) {
            std::cerr << ec.message() << std::endl;
            close();
            return;
          }
          puts("OOPS SERVER");
          throw system::system_error{ec};
        }
        const std::string header{reply.substr(0, header_len)};
        std::cout << GREEN << header << RESET << std::endl;
        read_body(server_socket, reply, header,
                  [self, this, msg_id] { send_message_to_client(msg_id); });
      });
}

void Socket::send_message_to_client(size_t msg_id) {
  auto self(shared_from_this());
  std::string &msg = messages[msg_id].second;
  asio::async_write(client_socket, asio::buffer(msg),
                    [self, this, msg_id, &msg](const system::error_code &ec,
                                               std::size_t bytes) {
                      if (stopped) {
                        return;
                      }
                      if (ec) {
                        puts("BLE");
                        throw boost::system::system_error{ec};
                      }
                      prev_host = curr_host;
                      get_message_from_client();
                    });
}

// TODO Do I need mutexes?
void Socket::close() {
  // auto self(shared_from_this());
  // printf("POINTERS: %ld\n", self.use_count());
  mutex.lock();
  if (stopped) {
    puts("NZEEE");
    mutex.unlock();
    return;
  }
  stopped = true;
  mutex.unlock();
  timer.cancel();
  if (server_socket.is_open()) {
    // server_socket.shutdown(asio::socket_base::shutdown_both);
    server_socket.cancel();
  }
  if (client_socket.is_open()) {
    // client_socket.shutdown(asio::socket_base::shutdown_both);
    client_socket.cancel();
  }
  // server_socket.close();
  // client_socket.close();
}
