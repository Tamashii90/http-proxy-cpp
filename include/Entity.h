#include <boost/asio.hpp>

struct Entity {
  Entity(boost::asio::io_context &io_context,
         std::shared_ptr<boost::asio::ip::tcp::socket> socket_p);
  void get_message_from_client();

  bool connect_to_server();

  void send_message_to_server();

  void get_message_from_server();

  void send_message_to_client();

  void close();

private:
  boost::asio::io_context &io_context;
  std::shared_ptr<boost::asio::ip::tcp::socket> client_socket;
  boost::asio::ip::tcp::socket server_socket;
  std::string prev_host;
  std::string curr_host;
  std::string message;
};
