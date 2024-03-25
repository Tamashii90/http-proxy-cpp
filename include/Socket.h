#include <boost/asio.hpp>
#include <unordered_set>

struct Socket : public std::enable_shared_from_this<Socket> {
  Socket(boost::asio::io_context &io_context,
         boost::asio::ip::tcp::socket &&socket,
         std::unordered_set<std::shared_ptr<Socket>> &conn_pool);

  void start();

  void resolve_server();

  void
  connect_to_endpoints(boost::asio::ip::tcp::resolver::results_type &endpoints);

  void handle_wait(const boost::system::error_code &ec);

  void read_body(boost::asio::ip::tcp::socket &socket, std::string &buffer,
                 const std::string &header, std::size_t header_len,
                 std::function<void()> callback);

  void get_message_from_client();

  void send_message_to_server();

  void get_message_from_server();

  void send_message_to_client();

  void close();

private:
  std::unordered_set<std::shared_ptr<Socket>> &conn_pool;
  boost::asio::strand<boost::asio::io_context::executor_type> strand;
  boost::asio::ip::tcp::resolver resolver;
  boost::asio::ip::tcp::socket client_socket;
  boost::asio::ip::tcp::socket server_socket;
  boost::asio::steady_timer timer;
  std::chrono::duration<long> timeout;
  std::string prev_host;
  std::string curr_host;
  std::string request;
  std::string reply;
};
