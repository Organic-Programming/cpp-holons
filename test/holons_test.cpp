#include "../include/holons/holons.hpp"

#include <arpa/inet.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

int connect_tcp(const std::string &host, int port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return -1;
  }

  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }

  return fd;
}

std::string make_temp_markdown_path() {
  char tmpl[] = "/tmp/holons_cpp_test_XXXXXX";
  int fd = ::mkstemp(tmpl);
  assert(fd >= 0);
  ::close(fd);
  std::string path = std::string(tmpl) + ".md";
  std::remove(tmpl);
  return path;
}

} // namespace

int main() {
  int passed = 0;

  // --- scheme ---
  assert(holons::scheme("tcp://:9090") == "tcp");
  ++passed;
  assert(holons::scheme("unix:///tmp/x.sock") == "unix");
  ++passed;
  assert(holons::scheme("stdio://") == "stdio");
  ++passed;
  assert(holons::scheme("mem://") == "mem");
  ++passed;
  assert(holons::scheme("ws://host:8080") == "ws");
  ++passed;
  assert(holons::scheme("wss://host:443") == "wss");
  ++passed;

  // --- default URI ---
  assert(holons::kDefaultURI == "tcp://:9090");
  ++passed;

  // --- parse_uri ---
  {
    auto parsed = holons::parse_uri("wss://example.com:8443");
    assert(parsed.scheme == "wss");
    ++passed;
    assert(parsed.host == "example.com");
    ++passed;
    assert(parsed.port == 8443);
    ++passed;
    assert(parsed.path == "/grpc");
    ++passed;
    assert(parsed.secure);
    ++passed;
  }

  // --- listen tcp + runtime accept/read ---
  {
    auto lis = holons::listen("tcp://127.0.0.1:0");
    auto *tcp = std::get_if<holons::tcp_listener>(&lis);
    assert(tcp != nullptr);
    ++passed;

    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int rc = ::getsockname(tcp->fd, reinterpret_cast<sockaddr *>(&addr), &len);
    assert(rc == 0);
    ++passed;

    int port = ntohs(addr.sin_port);
    assert(port > 0);
    ++passed;

    int cfd = connect_tcp("127.0.0.1", port);
    assert(cfd >= 0);
    ++passed;

    auto server_conn = holons::accept(lis);
    assert(server_conn.scheme == "tcp");
    ++passed;

    const char *msg = "ping";
    auto wrote = ::write(cfd, msg, 4);
    assert(wrote == 4);
    ++passed;

    char buf[8] = {0};
    auto read_n = holons::conn_read(server_conn, buf, sizeof(buf));
    assert(read_n == 4);
    ++passed;
    assert(std::string(buf, 4) == "ping");
    ++passed;

    holons::close_connection(server_conn);
    ::close(cfd);
    holons::close_listener(lis);
  }

  // --- listen stdio/mem/ws ---
  {
    auto stdio_lis = holons::listen("stdio://");
    assert(std::holds_alternative<holons::stdio_listener>(stdio_lis));
    ++passed;

    auto stdio_conn = holons::accept(stdio_lis);
    assert(stdio_conn.scheme == "stdio");
    ++passed;
    holons::close_connection(stdio_conn);

    try {
      (void)holons::accept(stdio_lis);
      assert(false && "stdio second accept should throw");
    } catch (const std::runtime_error &) {
      ++passed;
    }

    auto mem_lis = holons::listen("mem://unit");
    assert(std::holds_alternative<holons::mem_listener>(mem_lis));
    ++passed;

    auto mem_client = holons::mem_dial(mem_lis);
    auto mem_server = holons::accept(mem_lis);
    assert(mem_client.scheme == "mem");
    ++passed;
    assert(mem_server.scheme == "mem");
    ++passed;

    const char *msg = "mem";
    auto mem_wrote = holons::conn_write(mem_client, msg, 3);
    assert(mem_wrote == 3);
    ++passed;

    char mem_buf[8] = {0};
    auto mem_read = holons::conn_read(mem_server, mem_buf, sizeof(mem_buf));
    assert(mem_read == 3);
    ++passed;
    assert(std::string(mem_buf, 3) == "mem");
    ++passed;

    holons::close_connection(mem_server);
    holons::close_connection(mem_client);
    holons::close_listener(mem_lis);

    auto ws_lis = holons::listen("ws://127.0.0.1:8080/holon");
    auto *ws = std::get_if<holons::ws_listener>(&ws_lis);
    assert(ws != nullptr);
    ++passed;
    assert(ws->host == "127.0.0.1");
    ++passed;
    assert(ws->port == 8080);
    ++passed;
    assert(ws->path == "/holon");
    ++passed;
    assert(!ws->secure);
    ++passed;

    try {
      (void)holons::accept(ws_lis);
      assert(false && "ws accept should throw");
    } catch (const std::runtime_error &) {
      ++passed;
    }
  }

  // --- unsupported URI ---
  try {
    (void)holons::listen("ftp://host");
    assert(false && "should have thrown");
  } catch (const std::invalid_argument &) {
    ++passed;
  }

  // --- parse_flags ---
  assert(holons::parse_flags({"--listen", "tcp://:8080"}) == "tcp://:8080");
  ++passed;
  assert(holons::parse_flags({"--port", "3000"}) == "tcp://:3000");
  ++passed;
  assert(holons::parse_flags({}) == "tcp://:9090");
  ++passed;

  // --- yaml_value ---
  assert(holons::yaml_value("uuid: \"abc-123\"") == "abc-123");
  ++passed;
  assert(holons::yaml_value("lang: rust") == "rust");
  ++passed;

  // --- parse_holon ---
  {
    std::string path = make_temp_markdown_path();
    {
      std::ofstream f(path);
      f << "---\nuuid: \"abc-123\"\ngiven_name: \"test\"\n"
        << "family_name: \"Test\"\nlang: \"cpp\"\n---\n# test\n";
    }
    auto id = holons::parse_holon(path);
    assert(id.uuid == "abc-123");
    ++passed;
    assert(id.given_name == "test");
    ++passed;
    assert(id.lang == "cpp");
    ++passed;
    std::remove(path.c_str());
  }

  // --- parse_holon missing frontmatter ---
  {
    std::string path = make_temp_markdown_path();
    {
      std::ofstream f(path);
      f << "# No frontmatter\n";
    }
    try {
      holons::parse_holon(path);
      assert(false && "should have thrown");
    } catch (const std::runtime_error &e) {
      assert(std::string(e.what()).find("frontmatter") != std::string::npos);
      ++passed;
    }
    std::remove(path.c_str());
  }

  std::printf("%d passed, 0 failed\n", passed);
  return 0;
}
