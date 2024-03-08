#include <boost/asio.hpp>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>

constexpr unsigned PORT = 8000;

using std::string;
using namespace boost;

void handle(asio::ip::tcp::socket &socket) {
  std::string response;
  system::error_code ec;
  asio::read_until(socket, asio::dynamic_buffer(response), "\r\n\r\n");
  if (ec && ec.value() != asio::error::eof) {
    throw boost::system::system_error{ec};
  }
  std::cout << response << std::endl;
}

std::string request(std::string host, asio::io_context &io_context) {
  std::stringstream request_stream;
  request_stream << "GET / HTTP/1.1\r\n"
                    "Host: "
                 << host
                 << "\r\n"
                    "Accept: text/html\r\n"
                    "Accept-Language: en-us\r\n"
                    "Accept-Encoding: identity\r\n"
                    "Connection: close\r\n\r\n";
  const auto request = request_stream.str();

  asio::ip::tcp::resolver resolver{io_context};
  const auto endpoints = resolver.resolve(host, "http");
  asio::ip::tcp::socket socket{io_context};
  const auto connected_endpoint = asio::connect(socket, endpoints);

  asio::write(socket, asio::buffer(request));
  std::string response;
  system::error_code ec;
  asio::read(socket, asio::dynamic_buffer(response), ec);
  if (ec && ec.value() != asio::error::eof) {
    throw boost::system::system_error{ec};
  }
  return response;
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
      handle(socket);
    }
  } catch (std::exception e) {
    std::cerr << e.what() << std::endl;
  }
  // try {
  // const auto response = request("www.google.com", io_context);
  // std::cout << response << "\n";
  // }
  //
  // catch (system::system_error &se) {
  // std::cerr << "Error: " << se.what() << std::endl;
  // }
}
