#include "nefia.hpp"
#include <iostream>

int main() {
    // 1. Initialize with Config
    NefiaConfig config;
    config.buffer_size = 4096; // 4KB buffer
    config.thread_pool_size = 4; // 4 worker threads
    
    Nefia app(8080, config);
    
    // 2. Middleware Example: Logger
    app.use([](Request& req, Response& res) -> bool {
        std::cout << "[Middleware] Requesting: " << req.path << std::endl;
        return true; // Continue
    });

    // 3. Middleware Example: Auth (Simple check)
    app.use([](Request& req, Response& res) -> bool {
        if (req.path == "/secret") {
            if (req.get_header("Authorization") != "secret_token") {
                res.status_code = 401;
                res.body = "Unauthorized";
                return false; // Stop processing
            }
        }
        return true;
    });
    
    // 4. Static Route
    app.get("/", [](const Request& req, Response& res) {
        res.send("<h1>Hello from Nefia v0.1!</h1>");
    });
    
    // 5. Dynamic Route (Path Parameters)
    app.get("/user/:id", [](const Request& req, Response& res) {
        std::string user_id = req.get_param("id");
        res.send("User ID: " + user_id);
    });

    app.get("/post/:postId/comment/:commentId", [](const Request& req, Response& res) {
        std::string pid = req.get_param("postId");
        std::string cid = req.get_param("commentId");
        res.send("Post: " + pid + ", Comment: " + cid);
    });
    
    // 6. Secret Route (Protected by Middleware)
    app.get("/secret", [](const Request& req, Response& res) {
        res.send("Welcome to the secret area!");
    });
    
    app.listen();
    return 0;
}
