#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static int32_t write_all(int fd, const char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = write(fd, buf, n);
    if (rv <= 0) {
      std::cerr << "write() error: " << strerror(errno) << std::endl;
      return -1; // Error
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv <= 0) {
      std::cerr << "read() error or EOF: " << strerror(errno) << std::endl;
      return -1;
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t read_line(int fd, std::string &out_line) {
  out_line.clear();
  char c;
  while (true) {
    ssize_t rv = read(fd, &c, 1);
    if (rv <= 0) {
      std::cerr << "read() error or EOF in read_line: " << strerror(errno)
                << std::endl;
      return -1;
    }
    if (c == '\n' && !out_line.empty() && out_line.back() == '\r') {
      out_line.pop_back(); // Remove the '\r'
      return 0;
    }
    out_line += c;
  }
}

static bool format_as_resp(const std::string &line, std::string &out_resp) {
  std::stringstream ss(line);
  std::string word;
  std::vector<std::string> args;
  while (ss >> word) {
    args.push_back(word);
  }
  if (args.empty()) {
    return false;
  }
  out_resp = "*" + std::to_string(args.size()) + "\r\n";
  for (const std::string &arg : args) {
    out_resp += "$" + std::to_string(arg.length()) + "\r\n";
    out_resp += arg + "\r\n";
  }
  return true;
}

static int32_t read_one_response(int fd) {
  char first_byte;
  if (read_full(fd, &first_byte, 1) != 0) {
    return -1;
  }
  std::string line;
  switch (first_byte) {
  case '+':
  case '-':
    if (read_line(fd, line) != 0)
      return -1;
    std::cout << line << std::endl;
    break;
  case '$': {
    if (read_line(fd, line) != 0)
      return -1;
    int32_t len = std::stoi(line);
    if (len == -1) {
      std::cout << "(nil)" << std::endl;
      break;
    }
    std::vector<char> data(len + 2);
    if (read_full(fd, data.data(), data.size()) != 0)
      return -1;
    std::cout << std::string(data.data(), len) << std::endl;
    break;
  }
  default:
    std::cerr << "Unknown response type: " << first_byte << std::endl;
    return -1;
  }
  return 0;
}

void print_help() {
  std::cout
      << "Client Commands:\n"
      << "  help              - Show this message.\n"
      << "  quit              - Exit the client.\n"
      << "  begin             - Start a pipeline. Commands will be buffered.\n"
      << "  exec              - Execute the buffered pipeline.\n"
      << "Any other text is treated as a Redis command (e.g., 'ping', 'echo "
         "hello').\n";
}

int main() {
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    std::cerr << "socket() error: " << strerror(errno) << std::endl;
    return 1;
  }
  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(3000);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (connect(client_fd, (const struct sockaddr *)&server_addr,
              sizeof(server_addr)) < 0) {
    std::cerr << "connect() error: " << strerror(errno) << std::endl;
    return 1;
  }
  std::cout << "Connected to the server. Type 'help' for commands."
            << std::endl;
  bool in_pipeline = false;
  std::string pipeline_buffer;
  int pipeline_cmd_count = 0;
  while (true) {
    std::cout << (in_pipeline ? "pipe> " : "> ");
    std::string line;
    if (!std::getline(std::cin, line)) {
      break;
    }
    if (line.empty())
      continue;

    if (line == "quit")
      break;
    if (line == "help") {
      print_help();
      continue;
    }
    if (line == "begin") {
      in_pipeline = true;
      pipeline_buffer.clear();
      pipeline_cmd_count = 0;
      std::cout << "Pipeline mode started. Type 'exec' to send." << std::endl;
      continue;
    }

    if (in_pipeline) {
      if (line == "exec") {
        if (pipeline_buffer.empty()) {
          std::cout << "Pipeline is empty." << std::endl;
        } else {

          if (write_all(client_fd, pipeline_buffer.data(),
                        pipeline_buffer.size()) != 0) {
            break;
          }

          std::cout << "--- Pipeline Responses ---" << std::endl;
          for (int i = 0; i < pipeline_cmd_count; ++i) {
            if (read_one_response(client_fd) != 0) {
              goto L_DONE;
            }
          }
          std::cout << "------------------------" << std::endl;
        }
        in_pipeline = false;
        pipeline_buffer.clear();
        pipeline_cmd_count = 0;
      } else {

        std::string resp_cmd;
        if (format_as_resp(line, resp_cmd)) {
          pipeline_buffer.append(resp_cmd);
          pipeline_cmd_count++;
          std::cout << "QUEUED" << std::endl;
        } else {
          std::cout << "Invalid command." << std::endl;
        }
      }
    }

    else {
      std::string resp_cmd;
      if (!format_as_resp(line, resp_cmd)) {
        std::cout << "Invalid command." << std::endl;
        continue;
      }

      if (write_all(client_fd, resp_cmd.data(), resp_cmd.size()) != 0) {
        break; // Error
      }

      if (read_one_response(client_fd) != 0) {
        break; // Error
      }
    }
  }
L_DONE:
  close(client_fd);
  std::cout << "Connection closed." << std::endl;
  return 0;
}
