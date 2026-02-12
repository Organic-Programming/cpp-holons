---
# Cartouche v1
title: "cpp-holons — C++ SDK for Organic Programming"
author:
  name: "B. ALTER"
created: 2026-02-12
access:
  humans: true
  agents: false
status: draft
---
# cpp-holons

**C++ SDK for Organic Programming** — header-only, zero dependencies.

## Build & Test

```bash
clang++ -std=c++20 -I include test/holons_test.cpp -o test_runner && ./test_runner
```

## API surface

| Symbol | Description |
|--------|-------------|
| `holons::scheme(uri)` | Extract transport scheme |
| `holons::parse_uri(uri)` | Parse transport URI into normalized fields |
| `holons::listen(uri)` | Create listener variant (`tcp_listener`, `unix_listener`, `stdio_listener`, `mem_listener`, `ws_listener`) |
| `holons::close_listener(listener)` | Close and cleanup listener resources |
| `holons::parse_flags(args)` | CLI arg extraction |
| `holons::parse_holon(path)` | HOLON.md YAML parser |
| `holons::kDefaultURI` | Default transport URI |

## Transport support

| Scheme | Support |
|--------|---------|
| `tcp://<host>:<port>` | Bound socket (`tcp_listener`) |
| `unix://<path>` | Bound UNIX socket (`unix_listener`) |
| `stdio://` | Listener marker (`stdio_listener`) |
| `mem://` | Listener marker (`mem_listener`) |
| `ws://<host>:<port>` | Listener metadata (`ws_listener`) |
| `wss://<host>:<port>` | Listener metadata (`ws_listener`) |
