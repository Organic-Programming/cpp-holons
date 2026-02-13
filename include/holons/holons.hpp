#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <tuple>
#include <unistd.h>
#include <variant>
#include <vector>

namespace holons {

/// Default transport URI when --listen is omitted.
constexpr std::string_view kDefaultURI = "tcp://:9090";

/// Extract the scheme from a transport URI.
inline std::string scheme(std::string_view uri) {
  auto pos = uri.find("://");
  return pos != std::string_view::npos ? std::string(uri.substr(0, pos))
                                       : std::string(uri);
}

/// Parse --listen or --port from command-line args.
inline std::string parse_flags(const std::vector<std::string> &args) {
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--listen" && i + 1 < args.size())
      return args[i + 1];
    if (args[i] == "--port" && i + 1 < args.size())
      return "tcp://:" + args[i + 1];
  }
  return std::string(kDefaultURI);
}

/// Parsed transport URI.
struct parsed_uri {
  std::string raw;
  std::string scheme;
  std::string host;
  int port = 0;
  std::string path;
  bool secure = false;
};

struct tcp_listener {
  int fd = -1;
  std::string host;
  int port = 0;
};

struct unix_listener {
  int fd = -1;
  std::string path;
};

struct stdio_listener {
  std::string address = "stdio://";
  bool consumed = false;
};

struct mem_listener {
  std::string address = "mem://";
  int server_fd = -1;
  int client_fd = -1;
  bool server_consumed = false;
  bool client_consumed = false;
};

struct ws_listener {
  std::string host;
  int port = 0;
  std::string path;
  bool secure = false;
};

using listener =
    std::variant<tcp_listener, unix_listener, stdio_listener, mem_listener,
                 ws_listener>;

struct connection {
  int read_fd = -1;
  int write_fd = -1;
  std::string scheme;
  bool owns_read_fd = true;
  bool owns_write_fd = true;
};

inline std::tuple<std::string, int> split_host_port(const std::string &addr,
                                                     int default_port) {
  if (addr.empty())
    return {"0.0.0.0", default_port};

  auto pos = addr.rfind(':');
  if (pos == std::string::npos)
    return {addr, default_port};

  std::string host = addr.substr(0, pos);
  if (host.empty())
    host = "0.0.0.0";
  std::string port_text = addr.substr(pos + 1);
  int port = port_text.empty() ? default_port : std::stoi(port_text);
  return {host, port};
}

inline parsed_uri parse_uri(const std::string &uri) {
  std::string s = scheme(uri);

  if (s == "tcp") {
    if (uri.rfind("tcp://", 0) != 0)
      throw std::invalid_argument("invalid tcp URI: " + uri);
    auto [host, port] = split_host_port(uri.substr(6), 9090);
    return {uri, "tcp", host, port, "", false};
  }

  if (s == "unix") {
    if (uri.rfind("unix://", 0) != 0)
      throw std::invalid_argument("invalid unix URI: " + uri);
    auto path = uri.substr(7);
    if (path.empty())
      throw std::invalid_argument("invalid unix URI: " + uri);
    return {uri, "unix", "", 0, path, false};
  }

  if (s == "stdio") {
    return {"stdio://", "stdio", "", 0, "", false};
  }

  if (s == "mem") {
    std::string raw = uri.rfind("mem://", 0) == 0 ? uri : "mem://";
    std::string name = raw.size() > 6 ? raw.substr(6) : "";
    return {raw, "mem", "", 0, name, false};
  }

  if (s == "ws" || s == "wss") {
    bool secure = s == "wss";
    std::string prefix = secure ? "wss://" : "ws://";
    if (uri.rfind(prefix, 0) != 0)
      throw std::invalid_argument("invalid ws URI: " + uri);

    std::string trimmed = uri.substr(prefix.size());
    auto slash = trimmed.find('/');
    std::string addr = slash == std::string::npos ? trimmed : trimmed.substr(0, slash);
    std::string path = slash == std::string::npos ? "/grpc" : trimmed.substr(slash);
    if (path.empty())
      path = "/grpc";

    auto [host, port] = split_host_port(addr, secure ? 443 : 80);
    return {uri, s, host, port, path, secure};
  }

  throw std::invalid_argument("unsupported transport URI: " + uri);
}

inline listener listen(const std::string &uri) {
  auto parsed = parse_uri(uri);

  if (parsed.scheme == "tcp") {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      throw std::runtime_error("socket() failed");

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(parsed.port));
    if (parsed.host == "0.0.0.0") {
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, parsed.host.c_str(), &addr.sin_addr) != 1) {
      ::close(fd);
      throw std::runtime_error("invalid tcp host: " + parsed.host);
    }

    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      ::close(fd);
      throw std::runtime_error("bind() failed");
    }
    if (::listen(fd, 16) < 0) {
      ::close(fd);
      throw std::runtime_error("listen() failed");
    }
    return tcp_listener{fd, parsed.host, parsed.port};
  }

  if (parsed.scheme == "unix") {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
      throw std::runtime_error("socket() failed");

    ::unlink(parsed.path.c_str());
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", parsed.path.c_str());

    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      ::close(fd);
      throw std::runtime_error("bind(unix) failed");
    }
    if (::listen(fd, 16) < 0) {
      ::close(fd);
      throw std::runtime_error("listen(unix) failed");
    }
    return unix_listener{fd, parsed.path};
  }

  if (parsed.scheme == "stdio")
    return stdio_listener{parsed.raw, false};
  if (parsed.scheme == "mem") {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
      throw std::runtime_error("mem socketpair() failed");
    }
    return mem_listener{parsed.raw, fds[0], fds[1], false, false};
  }
  if (parsed.scheme == "ws" || parsed.scheme == "wss")
    return ws_listener{parsed.host, parsed.port, parsed.path, parsed.secure};

  throw std::invalid_argument("unsupported transport URI: " + uri);
}

/// Accept one connection from a listener.
/// - tcp/unix: OS socket accept
/// - stdio: single connection over stdin/stdout
/// - mem: server side of in-process pair
inline connection accept(listener &lis) {
  if (auto *tcp = std::get_if<tcp_listener>(&lis)) {
    int fd = ::accept(tcp->fd, nullptr, nullptr);
    if (fd < 0) {
      throw std::runtime_error("accept(tcp) failed: " +
                               std::string(std::strerror(errno)));
    }
    return connection{fd, fd, "tcp", true, true};
  }

  if (auto *unix_lis = std::get_if<unix_listener>(&lis)) {
    int fd = ::accept(unix_lis->fd, nullptr, nullptr);
    if (fd < 0) {
      throw std::runtime_error("accept(unix) failed: " +
                               std::string(std::strerror(errno)));
    }
    return connection{fd, fd, "unix", true, true};
  }

  if (auto *stdio = std::get_if<stdio_listener>(&lis)) {
    if (stdio->consumed) {
      throw std::runtime_error("stdio:// accepts exactly one connection");
    }
    stdio->consumed = true;
    return connection{STDIN_FILENO, STDOUT_FILENO, "stdio", false, false};
  }

  if (auto *mem = std::get_if<mem_listener>(&lis)) {
    if (mem->server_consumed || mem->server_fd < 0) {
      throw std::runtime_error("mem:// server side already consumed");
    }
    mem->server_consumed = true;
    int fd = mem->server_fd;
    mem->server_fd = -1;
    return connection{fd, fd, "mem", true, true};
  }

  if (std::holds_alternative<ws_listener>(lis)) {
    throw std::runtime_error(
        "ws/wss runtime accept is unsupported (metadata-only listener)");
  }

  throw std::runtime_error("listener variant cannot accept");
}

/// Dial the client side of a mem:// listener.
inline connection mem_dial(listener &lis) {
  auto *mem = std::get_if<mem_listener>(&lis);
  if (mem == nullptr) {
    throw std::invalid_argument("mem_dial() requires mem:// listener");
  }
  if (mem->client_consumed || mem->client_fd < 0) {
    throw std::runtime_error("mem:// client side already consumed");
  }
  mem->client_consumed = true;
  int fd = mem->client_fd;
  mem->client_fd = -1;
  return connection{fd, fd, "mem", true, true};
}

inline ssize_t conn_read(const connection &conn, void *buf, size_t n) {
  return ::read(conn.read_fd, buf, n);
}

inline ssize_t conn_write(const connection &conn, const void *buf, size_t n) {
  return ::write(conn.write_fd, buf, n);
}

inline void close_connection(connection &conn) {
  if (conn.owns_read_fd && conn.read_fd >= 0) {
    ::close(conn.read_fd);
    conn.read_fd = -1;
  }
  if (conn.owns_write_fd && conn.write_fd >= 0 &&
      conn.write_fd != conn.read_fd) {
    ::close(conn.write_fd);
    conn.write_fd = -1;
  }
}

inline void close_listener(listener &lis) {
  if (auto *tcp = std::get_if<tcp_listener>(&lis)) {
    if (tcp->fd >= 0) {
      ::close(tcp->fd);
      tcp->fd = -1;
    }
    return;
  }
  if (auto *unix_lis = std::get_if<unix_listener>(&lis)) {
    if (unix_lis->fd >= 0) {
      ::close(unix_lis->fd);
      unix_lis->fd = -1;
    }
    if (!unix_lis->path.empty())
      ::unlink(unix_lis->path.c_str());
    return;
  }
  if (auto *mem = std::get_if<mem_listener>(&lis)) {
    if (mem->server_fd >= 0) {
      ::close(mem->server_fd);
      mem->server_fd = -1;
    }
    if (mem->client_fd >= 0) {
      ::close(mem->client_fd);
      mem->client_fd = -1;
    }
  }
}

/// Parsed holon identity from HOLON.md.
struct HolonIdentity {
  std::string uuid;
  std::string given_name;
  std::string family_name;
  std::string motto;
  std::string composer;
  std::string clade;
  std::string status;
  std::string born;
  std::string lang;
};

/// Extract a YAML value from a simple key: "value" line.
/// Handles both quoted and unquoted values.
inline std::string yaml_value(const std::string &line) {
  auto colon = line.find(':');
  if (colon == std::string::npos)
    return "";
  auto val = line.substr(colon + 1);
  // Trim leading whitespace
  auto start = val.find_first_not_of(" \t");
  if (start == std::string::npos)
    return "";
  val = val.substr(start);
  // Strip quotes
  if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
    val = val.substr(1, val.size() - 2);
  return val;
}

/// Parse a HOLON.md file (simplified — no YAML library dependency).
inline HolonIdentity parse_holon(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("cannot open: " + path);

  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());

  if (text.substr(0, 3) != "---")
    throw std::runtime_error(path + ": missing YAML frontmatter");

  auto end_pos = text.find("---", 3);
  if (end_pos == std::string::npos)
    throw std::runtime_error(path + ": unterminated frontmatter");

  auto frontmatter = text.substr(3, end_pos - 3);
  HolonIdentity id;

  // Simple line-by-line parsing
  std::istringstream ss(frontmatter);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.find("uuid:") == 0)
      id.uuid = yaml_value(line);
    else if (line.find("given_name:") == 0)
      id.given_name = yaml_value(line);
    else if (line.find("family_name:") == 0)
      id.family_name = yaml_value(line);
    else if (line.find("motto:") == 0)
      id.motto = yaml_value(line);
    else if (line.find("composer:") == 0)
      id.composer = yaml_value(line);
    else if (line.find("clade:") == 0)
      id.clade = yaml_value(line);
    else if (line.find("status:") == 0)
      id.status = yaml_value(line);
    else if (line.find("born:") == 0)
      id.born = yaml_value(line);
    else if (line.find("lang:") == 0)
      id.lang = yaml_value(line);
  }
  return id;
}

} // namespace holons
