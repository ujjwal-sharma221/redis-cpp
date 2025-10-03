#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace std;

struct Conn {
  int fd = -1;

  bool want_read = false;
  bool want_write = false;
  bool want_close = false;

  vector<uint8_t> incoming;
  vector<uint8_t> outgoing;
};

void die(const string &msg) {
  cerr << "Error: " << msg << ": " << strerror(errno) << endl;
  exit(1);
}

void setFdNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    die("fcntl(F_GETFL)");
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    die("fcntl(F_SETFL)");
  }
}

void handleClientWrite(Conn *conn) {
  if (conn->outgoing.empty()) {
    conn->want_write = false;
    return;
  }

  ssize_t bytes_written =
      write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
  if (bytes_written < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    cout << "write() error on client " << conn->fd << ": " << strerror(errno)
         << endl;
    conn->want_close = true;
    return;
  }

  if ((size_t)bytes_written == conn->outgoing.size()) {
    conn->outgoing.clear();
    conn->want_write = false;
  } else {
    conn->outgoing.erase(conn->outgoing.begin(),
                         conn->outgoing.begin() + bytes_written);
  }
}

void handleClientRead(Conn *conn) {
  char buffer[4096];
  ssize_t bytes_read = read(conn->fd, buffer, sizeof(buffer));

  if (bytes_read <= 0) {
    if (bytes_read == 0) {
      cout << "Client " << conn->fd << " disconnected." << endl;
    } else {
      cout << "read() error on client " << conn->fd << ": " << strerror(errno)
           << endl;
    }
    conn->want_close = true;
    return;
  }

  cout << "Client " << conn->fd << " says: " << string(buffer, bytes_read)
       << endl;

  string response = "world";
  conn->outgoing.insert(conn->outgoing.end(), response.begin(), response.end());
  conn->want_write = true;
}

int main() {
  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket == -1) {
    die("socket()");
  }

  int val = 1;
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(3000);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (::bind(serverSocket, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
    die("bind()");
  }

  if (listen(serverSocket, SOMAXCONN) < 0) {
    die("listen()");
  }

  setFdNonBlocking(serverSocket);

  vector<Conn *> connections;
  vector<struct pollfd> pollfds;

  pollfds.push_back({serverSocket, POLLIN, 0});

  cout << "Server is listening on port 3000..." << endl;

  while (true) {
    if (poll(pollfds.data(), pollfds.size(), -1) < 0) {
      die("poll()");
    }

    for (size_t i = 0; i < pollfds.size(); i++) {
      if (pollfds[i].revents == 0) {
        continue;
      }

      if (pollfds[i].fd == serverSocket) {
        while (true) {
          struct sockaddr_in client_addr = {};
          socklen_t client_addr_len = sizeof(client_addr);
          int client_fd = accept(serverSocket, (struct sockaddr *)&client_addr,
                                 &client_addr_len);

          if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            cout << "accept() error: " << strerror(errno) << endl;
            break;
          }

          setFdNonBlocking(client_fd);
          Conn *new_conn = new Conn();
          new_conn->fd = client_fd;
          connections.push_back(new_conn);
          pollfds.push_back({client_fd, POLLIN, 0});
          cout << "New connection from fd: " << client_fd << endl;
        }
      } else {
        Conn *conn = nullptr;
        for (Conn *c : connections) {
          if (c->fd == pollfds[i].fd) {
            conn = c;
            break;
          }
        }

        if (conn) {
          if (pollfds[i].revents & POLLIN) {
            handleClientRead(conn);
          }
          if (pollfds[i].revents & POLLOUT) {
            handleClientWrite(conn);
          }

          pollfds[i].events = POLLIN;
          if (conn->want_write) {
            pollfds[i].events |= POLLOUT;
          }
        }
      }
      for (size_t i = connections.size(); i > 0; --i) {
        Conn *conn = connections[i - 1];
        if (conn->want_close) {
          int fd_to_close = conn->fd;
          connections.erase(connections.begin() + i - 1);
          for (size_t j = 0; j < pollfds.size(); ++j) {
            if (pollfds[j].fd == fd_to_close) {
              pollfds.erase(pollfds.begin() + j);
              break;
            }
          }
          close(fd_to_close);
          delete conn;
        }
      }
    }
  }

  return 0;
}
