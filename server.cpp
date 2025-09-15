#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace std;

void die(const string &msg) {
  cerr << "Error: " << msg << ": " << strerror(errno) << endl;
  exit(1);
}

void handleClient(int client_socket) {
  cout << "Client connected, handeling connection" << endl;
  vector<char> buffer(1024);

  ssize_t bytes_read = read(client_socket, buffer.data(), buffer.size() - 1);
  if (bytes_read < 0) {
    cerr << "read() error: " << strerror(errno) << endl;
    return;
  }

  buffer[bytes_read] = '\0';
  cout << "Client says:" << buffer.data() << endl;

  string res = "world";
  write(client_socket, res.c_str(), res.length());
  cout << "Send response world to the client" << endl;
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

  cout << "server is listening on 3000" << endl;

  while (true) {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);

    int clientFd =
        accept(serverSocket, (struct sockaddr *)&client_addr, &addrlen);

    if (clientFd < 0) {
      cerr << "accept() error: " << strerror(errno) << endl;
      continue;
    }

    handleClient(clientFd);

    close(clientFd);
    cout << "Client connection closed. Waiting for new connection..." << endl;
  }

  // unreacheable code
  close(serverSocket);
  return 0;
}
