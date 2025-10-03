#include <arpa/inet.h>
#include <cerrno>
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

  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    die("socket()");
  }

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(3000);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(client_fd, (const struct sockaddr *)&server_addr,
              sizeof(server_addr)) < 0) {
    die("connect()");
  }
  cout << "Successfully connected to the server. Type 'quit' to exit." << endl;

  while (true) {
    cout << "> ";
    string line;
    getline(cin, line);

    if (line.empty()) {
      continue;
    }
    if (line == "quit") {
      break;
    }

    write(client_fd, line.c_str(), line.length());

    vector<char> buffer(4096);
    ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
    if (bytes_read <= 0) {
      cout << "Server closed the connection or an error occurred." << endl;
      break;
    }

    buffer[bytes_read] = '\0';
    cout << "Server response: " << buffer.data() << endl;
  }

  close(client_fd);
  cout << "Connection closed." << endl;

  return 0;
}
