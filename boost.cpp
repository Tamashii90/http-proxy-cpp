#include <algorithm>
#include <boost/asio.hpp>
#include <iostream>
#include <string>

#include "Socket.h"
#include "utils.h"

using namespace boost;

constexpr unsigned PORT = 8000;

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
        std::make_shared<Socket>(io_context, std::move(socket))->start();
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
