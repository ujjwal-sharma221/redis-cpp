#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void msg_errno(const char *msg) {
  fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

static void die(const char *msg) {
  fprintf(stderr, "[%d] %s\n", errno, msg);
  abort();
}

static void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    die("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;
  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);
  if (errno) {
    die("fcntl error");
  }
}

struct Conn {
  int fd = -1;
  bool want_read = false;
  bool want_write = false;
  bool want_close = false;
  std::vector<uint8_t> incoming;
  std::vector<uint8_t> outgoing;
};

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data,
                       size_t len) {
  buf.insert(buf.end(), data, data + len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
  buf.erase(buf.begin(), buf.begin() + n);
}

static size_t find_crlf(const std::vector<uint8_t> &buf, size_t start_offset) {
  for (size_t i = start_offset; i + 1 < buf.size(); ++i) {
    if (buf[i] == '\r' && buf[i + 1] == '\n') {
      return i;
    }
  }

  return std::string::npos;
}

static int32_t parse_one_request(const std::vector<uint8_t> &buffer,
                                 std::vector<std::string> &out_args) {
  size_t pos = 0;

  if (buffer.size() < 3 || buffer[pos] != '*') {
    return buffer.empty() ? 0 : -1;
  }

  size_t crlf_pos = find_crlf(buffer, pos);
  if (crlf_pos == std::string::npos) {
    return 0;
  }

  std::string len_str(buffer.begin() + pos + 1, buffer.begin() + crlf_pos);
  int array_len = std::stoi(len_str);
  pos = crlf_pos + 2;

  for (int i = 0; i < array_len; ++i) {

    if (pos + 3 > buffer.size() || buffer[pos] != '$') {
      return -1;
    }

    crlf_pos = find_crlf(buffer, pos);
    if (crlf_pos == std::string::npos) {
      return 0;
    }
    std::string bulk_len_str(buffer.begin() + pos + 1,
                             buffer.begin() + crlf_pos);
    int bulk_len = std::stoi(bulk_len_str);
    pos = crlf_pos + 2;

    if (pos + bulk_len + 2 > buffer.size()) {
      return 0;
    }
    out_args.emplace_back(buffer.begin() + pos,
                          buffer.begin() + pos + bulk_len);
    pos += bulk_len + 2;
  }
  return (int32_t)pos;
}

static bool try_one_request(Conn *conn) {
  std::vector<std::string> args;
  int32_t bytes_consumed = parse_one_request(conn->incoming, args);
  if (bytes_consumed == 0) {
    return false;
  }
  if (bytes_consumed < 0) {
    msg("protocol error");
    conn->want_close = true;
    return false;
  }

  fprintf(stderr, "client %d says: ", conn->fd);
  for (const auto &arg : args) {
    fprintf(stderr, "'%s' ", arg.c_str());
  }
  fprintf(stderr, "\n");

  std::string response_str;
  if (!args.empty()) {
    std::string command_name = args[0];
    std::transform(command_name.begin(), command_name.end(),
                   command_name.begin(), ::toupper);
    if (command_name == "PING") {
      response_str = "+PONG\r\n";
    } else if (command_name == "ECHO" && args.size() > 1) {
      response_str =
          "$" + std::to_string(args[1].length()) + "\r\n" + args[1] + "\r\n";
    } else {
      response_str = "-ERR unknown command '" + args[0] + "'\r\n";
    }
  }

  buf_append(conn->outgoing, (const uint8_t *)response_str.data(),
             response_str.length());

  buf_consume(conn->incoming, bytes_consumed);
  return true;
}

static void handle_write(Conn *conn) {
  assert(!conn->outgoing.empty());
  ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
  if (rv < 0 && errno == EAGAIN) {
    return;
  }
  if (rv < 0) {
    msg_errno("write() error");
    conn->want_close = true;
    return;
  }
  buf_consume(conn->outgoing, (size_t)rv);
  if (conn->outgoing.empty()) {
    conn->want_read = true;
    conn->want_write = false;
  }
}

static void handle_read(Conn *conn) {
  uint8_t buf[64 * 1024];
  ssize_t rv = read(conn->fd, buf, sizeof(buf));
  if (rv < 0 && errno == EAGAIN) {
    return;
  }

  if (rv < 0) {
    msg_errno("read() error");
    conn->want_close = true;
    return;
  }

  if (rv == 0) {
    msg(conn->incoming.empty() ? "client closed" : "unexpected EOF");
    conn->want_close = true;
    return;
  }

  buf_append(conn->incoming, buf, (size_t)rv);
  while (try_one_request(conn)) {
  }
  if (!conn->outgoing.empty()) {
    conn->want_read = false;
    conn->want_write = true;
    handle_write(conn);
  }
}

static Conn *handle_accept(int fd) {
  struct sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
  if (connfd < 0) {
    msg_errno("accept() error");
    return NULL;
  }
  uint32_t ip = client_addr.sin_addr.s_addr;
  fprintf(stderr, "new client from %u.%u.%u.%u:%u (fd %d)\n", ip & 255,
          (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
          ntohs(client_addr.sin_port), connfd);
  fd_set_nb(connfd);
  Conn *conn = new Conn();
  conn->fd = connfd;
  conn->want_read = true;
  return conn;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    die("socket()");
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(3000);
  addr.sin_addr.s_addr = ntohl(0);
  int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
  if (rv)
    die("bind()");
  rv = listen(fd, SOMAXCONN);
  if (rv)
    die("listen()");
  fd_set_nb(fd);
  fprintf(stderr, "Server listening on port 3000...\n");
  std::vector<Conn *> fd2conn;
  std::vector<struct pollfd> poll_args;
  while (true) {
    poll_args.clear();
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);
    for (Conn *conn : fd2conn) {
      if (!conn)
        continue;
      struct pollfd pfd = {conn->fd, POLLERR, 0};
      if (conn->want_read)
        pfd.events |= POLLIN;
      if (conn->want_write)
        pfd.events |= POLLOUT;
      poll_args.push_back(pfd);
    }
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
    if (rv < 0) {
      if (errno == EINTR)
        continue;
      die("poll");
    }
    if (poll_args[0].revents) {
      if (Conn *conn = handle_accept(fd)) {
        if (fd2conn.size() <= (size_t)conn->fd) {
          fd2conn.resize(conn->fd + 1);
        }
        fd2conn[conn->fd] = conn;
      }
    }
    for (size_t i = 1; i < poll_args.size(); ++i) {
      uint32_t ready = poll_args[i].revents;
      if (!ready)
        continue;
      Conn *conn = fd2conn[poll_args[i].fd];
      if (ready & POLLIN)
        handle_read(conn);
      if (ready & POLLOUT)
        handle_write(conn);
      if ((ready & (POLLERR | POLLHUP)) || conn->want_close) {
        fprintf(stderr, "Closing connection fd %d\n", conn->fd);
        close(conn->fd);
        fd2conn[conn->fd] = NULL;
        delete conn;
      }
    }
  }
  return 0;
}
