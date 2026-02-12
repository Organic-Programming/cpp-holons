#include "../include/holons/holons.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>

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

  // --- default URI ---
  assert(holons::kDefaultURI == "tcp://:9090");
  ++passed;

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
