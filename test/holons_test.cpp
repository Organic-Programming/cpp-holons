#include "../include/holons/holons.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/socket.h>

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

  // --- listen tcp/ws/mem/stdio ---
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
    assert(ntohs(addr.sin_port) > 0);
    ++passed;
    holons::close_listener(lis);
  }

  {
    auto stdio_lis = holons::listen("stdio://");
    assert(std::holds_alternative<holons::stdio_listener>(stdio_lis));
    ++passed;

    auto mem_lis = holons::listen("mem://");
    assert(std::holds_alternative<holons::mem_listener>(mem_lis));
    ++passed;

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
    auto tmp = std::tmpnam(nullptr);
    std::string path = std::string(tmp) + ".md";
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
    auto tmp = std::tmpnam(nullptr);
    std::string path = std::string(tmp) + ".md";
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
