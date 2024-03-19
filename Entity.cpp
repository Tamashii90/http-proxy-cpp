// TODO How to handle socket shutdown? Normally and on error?
// TODO Make this multithreaded
#include <boost/asio.hpp>
#include <iostream>

using namespace boost;

#include "Entity.h"
#include "definitions.h"

Entity::Entity(asio::io_context &io_context,
               std::shared_ptr<asio::ip::tcp::socket> socket_p)
    : io_context{io_context}, client_socket{socket_p}, server_socket{
                                                           io_context} {
  get_message_from_client();
}
// void Entity::recv_message(asio::ip::tcp::socket &socket, std::string
// &message, std::function<void()> callback, std::string *host_to_mutate =
// nullptr) { asio::async_read_until( socket, asio::dynamic_buffer(message),
// "\r\n\r\n",
// [&callback](system::error_code &err, std::size_t &header_len) {
// if (err) {
// if (err.value() == asio::error::eof && !header_len) {
// }
// throw system::system_error{err};
// }
//
// callback();
// });
// }

// void Entity::start_accept() {
// asio::ip::tcp::endpoint *client_endpoint =
// new asio::ip::tcp::endpoint{};
// acceptor.async_accept(
// client_socket, *client_endpoint,
// [this, client_endpoint](const system::error_code &error) {
// std::cout << MAG << "New socket on port " << client_endpoint->port()
// << RESET << std::endl;
// get_message_from_client();
// // start_accept();
// });
// }

void Entity::get_message_from_client() {
  std::shared_ptr<asio::steady_timer> timer =
      std::make_shared<asio::steady_timer>(io_context);
  timer->expires_after(std::chrono::seconds(10));
  timer->async_wait([this](const system::error_code &ec) {
    if (ec) {
      std::cerr << ec.what() << std::endl;
      return;
    }
    std::cout << "Timeout reached, cancelling async_read" << std::endl;
    client_socket->cancel();
  });

  asio::async_read_until(
      *client_socket, asio::dynamic_buffer(message), "\r\n\r\n",
      [this, timer](system::error_code err, std::size_t header_len) {
        timer->cancel();
        if (err) {
          if (err.value() == asio::error::operation_aborted) {
            close();
            return;
          }
          if (err.value() == asio::error::eof && !header_len) {
            return;
          }
          throw system::system_error{err};
        }
        // Everything inside this block is exclusive to requests received
        // from the CLIENT
        std::string first_line = message.substr(0, message.find("\r\n"));
        std::istringstream iss{first_line};
        std::string method, url, http_version;
        iss >> method >> url >> http_version;
        // http_version looks like "HTTP/1.1"
        if (stod(http_version.substr(http_version.find("/") + 1)) > 1.1) {
          std::cerr << RED << "HTTP Version Not Supported" << RESET
                    << std::endl;
          return;
        }
        curr_host = parse_field(message, "host");
        const std::string header{message.substr(0, header_len)};
        std::cout << YELLOW << header << RESET << std::endl;

        Body body_type = identify_body(header);
        if (body_type == Body::CONTENT_LENGTH) {
          auto content_length = stoul(parse_field(header, "content-length"));
          asio::read(*client_socket,
                     asio::dynamic_buffer(message, content_length + header_len),
                     err);
        } else if (body_type == Body::CHUNKED) {
          asio::read_until(*client_socket, asio::dynamic_buffer(message),
                           "0\r\n\r\n", err);
        }
        if (err && err.value() != asio::error::eof) {
          throw system::system_error{err};
        }
        if (connect_to_server()) {
          send_message_to_server();
          message.clear();
          get_message_from_server();
          send_message_to_client();
          message.clear();
          prev_host = curr_host;
          get_message_from_client();
        }
      });
}

bool Entity::connect_to_server() {
  if (!server_socket.is_open() || prev_host != curr_host) {
    if (!connect_to_endpoint(io_context, server_socket, curr_host)) {
      return false;
    }
  }
  return true;
}

void Entity::send_message_to_server() {
  asio::write(server_socket, asio::buffer(message));
}

void Entity::get_message_from_server() {
  if (!recv_message(server_socket, message, nullptr)) {
  }
}

void Entity::send_message_to_client() {
  asio::write(*client_socket, asio::buffer(message));
}

void Entity::close() {
  client_socket->close();
  server_socket.close();
  delete this;
}
