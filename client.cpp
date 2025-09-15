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

int main() {
  int clientSocket = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(3000);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
  int rv = connect(clientSocket, (const struct sockaddr *)&addr, sizeof(addr));

  if (rv)
    die("connect()");
}
