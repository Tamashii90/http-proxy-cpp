#include <boost/asio.hpp>
#include <iostream>
#include <unordered_set>

using namespace boost;

#include "Socket.h"
#include "definitions.h"

Socket::Socket(asio::io_context &io_context, asio::ip::tcp::socket &&socket,
               std::unordered_set<std::shared_ptr<Socket>> &conn_pool)
    : strand{asio::make_strand(io_context)}, resolver{strand},
      client_socket{std::move(socket)}, server_socket{strand},
      timer{strand, asio::steady_timer::time_point::max()},
      timeout{std::chrono::seconds(15)}, conn_pool{conn_pool} {}

void Socket::start() {
  timer.async_wait(
      std::bind(&Socket::handle_wait, this, asio::placeholders::error));
  get_message_from_client();
}

void Socket::handle_wait(const system::error_code &ec) {
  if (!client_socket.is_open()) {
    return;
  }
  if (!ec) {
    std::cout << "Socket timed out on port "
              << client_socket.remote_endpoint().port() << std::endl;
    close();
  } else if (ec.value() == asio::error::operation_aborted) {
    // If cancelled restart the timeout duration
    timer.expires_at(timer.expiry() + timeout);
    timer.async_wait(
        std::bind(&Socket::handle_wait, this, asio::placeholders::error));
  }
}

void Socket::get_message_from_client() {
  request.clear();
  timer.expires_after(timeout);
  asio::async_read_until(
      client_socket, asio::dynamic_buffer(request), "\r\n\r\n",
      [this](system::error_code ec, std::size_t header_len) {
        timer.cancel();
        if (!client_socket.is_open()) {
          return;
        }
        if (ec) {
          if (ec.value() == asio::error::operation_aborted) {
            return;
          }
          if (ec.value() == asio::error::eof && !header_len) {
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
        curr_host = parse_field(request, "host");

        const std::string header{request.substr(0, header_len)};
        std::cout << YELLOW << header << RESET << std::endl;
        read_body(client_socket, request, header, header_len,
                  [this] { resolve_server(); });
      });
}

void Socket::read_body(asio::ip::tcp::socket &socket, std::string &buffer,
                       const std::string &header, std::size_t header_len,
                       std::function<void()> callback) {
  Body body_type = identify_body(header);
  if (body_type == Body::NONE) {
    callback();
    return;
  }
  auto shared_handler = [callback](const system::error_code &ec,
                                   std::size_t bytes) {
    {
      if (!ec) {
        callback();
      } else if (ec.value() != asio::error::eof &&
                 ec.value() != asio::error::operation_aborted) {
        throw system::system_error{ec};
      }
    }
  };
  if (body_type == Body::CONTENT_LENGTH) {
    auto content_length = stoul(parse_field(header, "content-length"));
    asio::async_read(socket,
                     asio::dynamic_buffer(buffer, content_length + header_len),
                     shared_handler);
  } else if (body_type == Body::CHUNKED) {
    asio::async_read_until(socket, asio::dynamic_buffer(buffer), "0\r\n\r\n",
                           shared_handler);
  }
}

void Socket::resolve_server() {
  if (server_socket.is_open() && prev_host == curr_host) {
    return;
  }
  resolver.async_resolve(
      curr_host, "http",
      [this](const system::error_code &ec,
             asio::ip::tcp::resolver::results_type endpoints) {
        if (ec) {
          std::cout << RED << ec.message() << ". "
                    << "Host: [" << curr_host << "]" << RESET << std::endl;
          close();
        } else {
          connect_to_endpoints(endpoints);
        }
      });
}

void Socket::connect_to_endpoints(
    asio::ip::tcp::resolver::results_type &endpoints) {
  asio::async_connect(server_socket, endpoints,
                      [this](const system::error_code &ec,
                             const asio::ip::tcp::endpoint &endpoint) {
                        if (ec) {
                          std::cout << RED << ec.message() << RESET
                                    << std::endl;
                          close();
                        } else {
                          send_message_to_server();
                        }
                      });
}

void Socket::send_message_to_server() {
  asio::async_write(
      server_socket, asio::buffer(request),
      [this](const system::error_code ec, const std::size_t bytes) {
        if (!server_socket.is_open()) {
          // If timer ran out before this handler got the chance
          return;
        }
        if (ec) {
          puts("BLA");
          throw boost::system::system_error{ec};
        }
        get_message_from_server();
      });
}

void Socket::get_message_from_server() {
  reply.clear();
  asio::async_read_until(
      server_socket, asio::dynamic_buffer(reply), "\r\n\r\n",
      [this](system::error_code err, std::size_t header_len) {
        if (!server_socket.is_open()) {
          return;
        }
        if (err) {
          if (err.value() == asio::error::operation_aborted) {
            return;
          }
          if (err.value() == asio::error::eof && !header_len) {
            return;
          }
          puts("OOPS SERVER");
          throw system::system_error{err};
        }
        const std::string header{reply.substr(0, header_len)};
        std::cout << GREEN << header << RESET << std::endl;
        auto callback = [this] { send_message_to_client(); };
        read_body(server_socket, reply, header, header_len,
                  [this] { send_message_to_client(); });
      });
}

void Socket::send_message_to_client() {
  asio::async_write(
      client_socket, asio::buffer(reply),
      [this](const system::error_code ec, const std::size_t bytes) {
        if (!client_socket.is_open()) {
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

void Socket::close() {
  // client_socket.cancel();
  // server_socket.cancel();
  client_socket.close();
  server_socket.close();
  timer.cancel();
  // conn_pool.erase(shared_from_this());
}
