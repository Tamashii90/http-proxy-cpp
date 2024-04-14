#include <boost/asio.hpp>

struct Socket : public std::enable_shared_from_this<Socket> {
  Socket(boost::asio::io_context &io_context,
         boost::asio::ip::tcp::socket &&socket);

  void start();

  void resolve_server(size_t msg_id);

  void connect_to_endpoints(
      size_t msg_id, boost::asio::ip::tcp::resolver::results_type &endpoints);

  void handle_wait(const boost::system::error_code &ec,
                   std::shared_ptr<Socket> self);

  void read_body(boost::asio::ip::tcp::socket &socket,
                 std::string &http_header_plus, const std::string &http_header,
                 std::function<void()> callback);

  void get_message_from_client();

  void send_message_to_server(size_t msg_id);

  void get_message_from_server(size_t msg_id);

  void send_message_to_client(size_t msg_id);

  void close();

 private:
  boost::asio::strand<boost::asio::io_context::executor_type> strand;
  boost::asio::ip::tcp::resolver resolver;
  boost::asio::ip::tcp::socket client_socket;
  boost::asio::ip::tcp::socket server_socket;
  std::chrono::duration<long> timeout;
  boost::asio::steady_timer timer;
  std::string prev_host;
  std::string curr_host;
  std::vector<std::pair<std::string, std::string>> messages;
  bool stopped;
  std::mutex mutex;
};
