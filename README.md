# Nefia

**Nefia** is a lightweight, high-performance web framework built with C++ (Header-only).

> ⚠️ **WARNING:** Nefia is a core engine for educational and performance testing purposes. It is not designed for large-scale production use.

## Features

- **Header-only Library:** Easy to include (`#include "nefia.hpp"`).
- **Dynamic Routing:** Supports path parameters (e.g., `/user/:id`).
- **Middleware System:** Easy interception for logging, auth, etc.
- **Thread Pool:** Efficient connection handling with a configurable thread pool.
- **Cross-Platform:** Works on Linux, macOS, and Windows.

## Usage

### 1. Installation

Simply download `nefia.hpp` and include it in your project.

### 2. Compilation

**Linux / macOS:**
```bash
g++ main.cpp -o nefia -pthread
```

**Windows (MinGW):**
You must explicitly link the Winsock library (`ws2_32`):
```bash
g++ main.cpp -o nefia -lws2_32
```

**Windows (Visual Studio):**
The library automatically links `ws2_32.lib` using `#pragma comment`.
```cmd
cl /EHsc main.cpp /Fe:nefia.exe
```

### 3. Example Code

```cpp
#include "nefia.hpp"
#include <iostream>

int main() {
    // 1. Config (Optional)
    NefiaConfig config;
    config.thread_pool_size = 4;
    Nefia app(8080, config);

    // 2. Middleware
    app.use([](Request& req, Response& res) -> bool {
        std::cout << "Request: " << req.path << std::endl;
        return true; // Continue
    });

    // 3. Static Route
    app.get("/", [](const Request& req, Response& res) {
        res.send("<h1>Hello Nefia!</h1>");
    });

    // 4. Dynamic Route
    app.get("/user/:id", [](const Request& req, Response& res) {
        res.send("User ID: " + req.get_param("id"));
    });

    // 5. Start Server
    app.listen();
    return 0;
}
```
