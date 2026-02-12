#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
