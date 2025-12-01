# Nefia

**Nefia** is a simple web framework built with C++.

> ⚠️ **WARNING:** Nefia is a core engine for educational and performance testing purposes. It is not designed for large-scale production use.

## Usage

Ensure you run on Linux/macOS or WSL.

1. **Compile:** `g++ -o nefia main.cpp -pthread`

2. **Run:** `./nefia`

3. Try access `http://localhost:8080` in your browser. 

## Example Usage

```cpp
#include "main.cpp" // Assuming Nefia core is here

int main() {
    Nefia app(8080);

    app.get("/", [](const Request& req, Response& res) {
        res.sendFile("index.html");
    });

    app.listen();
    return 0;
}
```
