#pragma once

#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <sstream>
#include <map>
#include <functional>
#include <fstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

// ---------------------------------------------------------
// CROSS-PLATFORM SOCKET SETUP
// ---------------------------------------------------------
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
    #endif
    using socket_t = SOCKET;
    using socklen_t = int;
    #define CLOSE_SOCKET closesocket
    #define IS_VALID_SOCKET(s) ((s) != INVALID_SOCKET)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    using socket_t = int;
    #define CLOSE_SOCKET close
    #define IS_VALID_SOCKET(s) ((s) >= 0)
#endif

// ---------------------------------------------------------
// NEFIA CORE DEFINITIONS
// ---------------------------------------------------------

const std::string NEFIA_VERSION = "0.1.0";

struct NefiaConfig {
    int buffer_size = 30720; // 30KB Default
    unsigned int thread_pool_size = std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 4;
};

inline std::string get_mime_type(std::string path) {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css")  != std::string::npos) return "text/css";
    if (path.find(".js")   != std::string::npos) return "application/javascript";
    if (path.find(".json") != std::string::npos) return "application/json";
    if (path.find(".png")  != std::string::npos) return "image/png";
    if (path.find(".jpg")  != std::string::npos) return "image/jpeg";
    if (path.find(".jpeg") != std::string::npos) return "image/jpeg";
    if (path.find(".svg")  != std::string::npos) return "image/svg+xml";
    if (path.find(".ico")  != std::string::npos) return "image/x-icon";
    if (path.find(".woff2") != std::string::npos) return "font/woff2";
    return "text/plain";
}

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;  // ?key=val
    std::map<std::string, std::string> headers; // Content-Type, etc.
    std::string body;                          // Raw POST body
    std::map<std::string, std::string> form;   // Parsed form data
    std::map<std::string, std::string> params; // Path parameters (e.g., :id)
    std::map<std::string, std::string> cookies; // Parsed cookies
    std::map<std::string, std::string> json_body; // Parsed JSON body

    std::string get_header(std::string key) const {
        if (headers.find(key) != headers.end()) return headers.at(key);
        return "";
    }

    std::string get_cookie(std::string key) const {
        if (cookies.find(key) != cookies.end()) return cookies.at(key);
        return "";
    }

    std::string get_query(std::string key) const {
        if (query.find(key) != query.end()) return query.at(key);
        return "";
    }

    std::string get_form(std::string key) const {
        if (form.find(key) != form.end()) return form.at(key);
        return "";
    }
    
    std::string get_param(std::string key) const {
        if (params.find(key) != params.end()) return params.at(key);
        return "";
    }
};

struct Response {
    std::string body;
    int status_code = 200;
    std::string content_type = "text/html";
    std::map<std::string, std::string> headers; // Custom headers

    void set_header(std::string key, std::string val) {
        headers[key] = val;
    }

    void set_cookie(std::string key, std::string value, std::string options = "") {
        std::string cookie_str = key + "=" + value;
        if (!options.empty()) {
            cookie_str += "; " + options;
        }
        new_cookies.push_back(cookie_str);
    }
    std::vector<std::string> new_cookies;

    void send(std::string text) {
        body = text;
        status_code = 200;
        content_type = "text/html";
    }

    void json(std::string json_text) {
        body = json_text;
        status_code = 200;
        content_type = "application/json";
    }

    void redirect(std::string url) {
        status_code = 302;
        set_header("Location", url);
        body = "";
    }

    void sendFile(std::string filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            body = buffer.str();
            content_type = get_mime_type(filepath);
            status_code = 200;
        } else {
            status_code = 404;
            body = "<h1>404 File Not Found</h1>";
        }
    }

    void render(std::string filepath, std::map<std::string, std::string> data) {
        std::ifstream file(filepath);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            for (auto const& [key, val] : data) {
                std::string placeholder = "{{" + key + "}}";
                size_t pos = 0;
                while ((pos = content.find(placeholder, pos)) != std::string::npos) {
                    content.replace(pos, placeholder.length(), val);
                    pos += val.length();
                }
            }
            body = content;
            content_type = "text/html";
            status_code = 200;
        } else {
            status_code = 404;
            body = "<h1>404 Template Not Found</h1>";
        }
    }
};

using Handler = std::function<void(const Request&, Response&)>;
using Middleware = std::function<bool(Request&, Response&)>;

class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false) {
        for(size_t i = 0; i < threads; ++i)
            workers.emplace_back(
                [this] {
                    for(;;) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock,
                                [this]{ return this->stop || !this->tasks.empty(); });
                            if(this->stop && this->tasks.empty())
                                return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }
                        task();
                    }
                }
            );
    }

    template<class F, class... Args>
    void enqueue(F&& f, Args&&... args) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop) return; // Don't add to stopped pool
            tasks.emplace(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
            worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

struct DynamicRoute {
    std::string method;
    std::string pattern;
    Handler handler;
};

class Nefia {
private:
    int port;
    socket_t server_fd;
    std::map<std::string, Handler> static_routes;
    std::vector<DynamicRoute> dynamic_routes;
    std::vector<Middleware> middlewares;
    NefiaConfig config;
    std::unique_ptr<ThreadPool> thread_pool;

    std::vector<std::string> split(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            if (!token.empty()) tokens.push_back(token);
        }
        return tokens;
    }

    std::map<std::string, std::string> parse_url_encoded(std::string raw) {
        std::map<std::string, std::string> data;
        std::vector<std::string> pairs = split(raw, '&');
        for (const auto& pair : pairs) {
            size_t eq_pos = pair.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = pair.substr(0, eq_pos);
                std::string val = pair.substr(eq_pos + 1);
                data[key] = val;
            }
        }
        return data;
    }

    std::map<std::string, std::string> parse_json_simple(std::string raw) {
        std::map<std::string, std::string> data;
        // Very basic JSON parser for flat string/number/bool object
        // Example: {"key": "val", "num": 123}

        size_t pos = 0;
        while(pos < raw.size()) {
            size_t quote_start = raw.find('"', pos);
            if(quote_start == std::string::npos) break;
            size_t quote_end = raw.find('"', quote_start + 1);
            if(quote_end == std::string::npos) break;
            
            std::string k = raw.substr(quote_start + 1, quote_end - quote_start - 1);
            
            size_t colon = raw.find(':', quote_end);
            if(colon == std::string::npos) break;
            
            // Find value start
            size_t val_start = colon + 1;
            while(val_start < raw.size() && isspace(raw[val_start])) val_start++;
            if(val_start >= raw.size()) break;

            if(raw[val_start] == '"') {
                // String value
                size_t val_end = raw.find('"', val_start + 1);
                if(val_end == std::string::npos) break;
                data[k] = raw.substr(val_start + 1, val_end - val_start - 1);
                pos = val_end + 1;
            } else {
                // Primitive value (number, bool, null)
                size_t val_end = val_start;
                while(val_end < raw.size() && (isalnum(raw[val_end]) || raw[val_end] == '.' || raw[val_end] == '-')) {
                    val_end++;
                }
                data[k] = raw.substr(val_start, val_end - val_start);
                pos = val_end;
            }
        }
        return data;
    }

    Request parse_request(const char* buffer, size_t length) {
        Request req;
        std::string raw_data(buffer, length);
        
        std::string delimiter = "\r\n\r\n";
        size_t split_pos = raw_data.find(delimiter);
        
        std::string header_part;
        if (split_pos != std::string::npos) {
            header_part = raw_data.substr(0, split_pos);
            req.body = raw_data.substr(split_pos + 4);
        } else {
            header_part = raw_data;
        }

        std::stringstream ss(header_part);
        std::string line;

        if (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::stringstream line_ss(line);
            std::string full_path;
            line_ss >> req.method >> full_path;

            size_t q_pos = full_path.find('?');
            if (q_pos != std::string::npos) {
                req.path = full_path.substr(0, q_pos);
                req.query = parse_url_encoded(full_path.substr(q_pos + 1));
            } else {
                req.path = full_path;
            }
        }

        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string val = line.substr(colon_pos + 1);
                if (!val.empty() && val[0] == ' ') val.erase(0, 1);
                req.headers[key] = val;

                if (key == "Cookie") {
                    std::stringstream cookie_ss(val);
                    std::string segment;
                    while (std::getline(cookie_ss, segment, ';')) {
                        size_t eq = segment.find('=');
                        if (eq != std::string::npos) {
                            std::string c_key = segment.substr(0, eq);
                            std::string c_val = segment.substr(eq + 1);
                            while (!c_key.empty() && c_key[0] == ' ') c_key.erase(0, 1);
                            req.cookies[c_key] = c_val;
                        }
                    }
                }
            }
        }

        if (!req.body.empty()) {
           if (req.get_header("Content-Type").find("application/json") != std::string::npos) {
               req.json_body = parse_json_simple(req.body);
           } else {
               req.form = parse_url_encoded(req.body);
           }
        }
        return req;
    }

    bool match_dynamic_route(const std::string& pattern, const std::string& path, std::map<std::string, std::string>& params) {
        std::vector<std::string> pat_parts = split(pattern, '/');
        std::vector<std::string> path_parts = split(path, '/');

        if (pat_parts.size() != path_parts.size()) return false;

        for (size_t i = 0; i < pat_parts.size(); ++i) {
            if (pat_parts[i].front() == ':') {
                params[pat_parts[i].substr(1)] = path_parts[i];
            } else if (pat_parts[i] != path_parts[i]) {
                return false;
            }
        }
        return true;
    }

    void handle_client(socket_t client_socket) {
        // Set Receive Timeout (e.g., 5 seconds) to prevent blocking indefinitely
        #ifdef _WIN32
        DWORD timeout = 5000;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        #else
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        #endif

        std::vector<char> buffer(config.buffer_size, 0);

        while (true) {
            // Check if socket is still valid/connected might be needed but recv handles it
            int bytes_read = recv(client_socket, buffer.data(), config.buffer_size, 0);
            if (bytes_read <= 0) {
                // Connection closed or timeout/error
                break;
            }

            Request req = parse_request(buffer.data(), static_cast<size_t>(bytes_read));
            Response res;

            if (!req.path.empty()) {
                std::cout << "[Nefia] " << req.method << " " << req.path << std::endl;
            }

            // Run Middleware
            bool continue_processing = true;
            for (auto& mw : middlewares) {
                if (!mw(req, res)) {
                    continue_processing = false;
                    break;
                }
            }

            if (continue_processing) {
                std::string route_key = req.method + ":" + req.path;
                bool route_found = false;

                // 1. Check Static Routes
                if (static_routes.find(route_key) != static_routes.end()) {
                    static_routes[route_key](req, res);
                    route_found = true;
                } 
                // 2. Check Dynamic Routes
                else {
                    for (const auto& dr : dynamic_routes) {
                        if (dr.method == req.method) {
                            std::map<std::string, std::string> params;
                            if (match_dynamic_route(dr.pattern, req.path, params)) {
                                req.params = params;
                                dr.handler(req, res);
                                route_found = true;
                                break;
                            }
                        }
                    }
                }

                if (!route_found) {
                    res.status_code = 404;
                    res.body = "<h1>404 Not Found</h1>";
                }
            }

            // Check connection header from request to decide if we should close
            bool keep_alive = true;
            std::string conn_header = req.get_header("Connection");
            // HTTP 1.1 defaults to keep-alive. HTTP 1.0 defaults to close.
            // Simplified logic: close if explicitly requested.
            if (conn_header == "close") {
                keep_alive = false;
            }

            std::stringstream response_stream;
            response_stream << "HTTP/1.1 " << res.status_code << " OK\r\n";
            response_stream << "Content-Type: " << res.content_type << "\r\n";
            response_stream << "Server: Nefia/" << NEFIA_VERSION << " (Teaserverse)\r\n";
            response_stream << "Content-Length: " << res.body.size() << "\r\n";
            
            if (!keep_alive) {
                response_stream << "Connection: close\r\n";
            } else {
                response_stream << "Connection: keep-alive\r\n";
            }
            
            for(auto const& [key, val] : res.headers) {
                response_stream << key << ": " << val << "\r\n";
            }
            for(const auto& cookie : res.new_cookies) {
                response_stream << "Set-Cookie: " << cookie << "\r\n";
            }

            response_stream << "\r\n";
            response_stream << res.body;

            std::string final_response = response_stream.str();
            int send_result = send(client_socket, final_response.c_str(), final_response.size(), 0);
            
            if (send_result < 0 || !keep_alive) {
                break;
            }
        }
        CLOSE_SOCKET(client_socket);
    }

public:
    Nefia(int p, NefiaConfig cfg = {}) : port(p), config(cfg) {
        #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
            exit(EXIT_FAILURE);
        }
        #endif
        thread_pool = std::make_unique<ThreadPool>(config.thread_pool_size);
    }

    ~Nefia() {
        #ifdef _WIN32
        WSACleanup();
        #endif
    }

    void use(Middleware mw) {
        middlewares.push_back(mw);
    }

    void get(std::string path, Handler handler) { 
        if (path.find(':') != std::string::npos) {
            dynamic_routes.push_back({"GET", path, handler});
        } else {
            static_routes["GET:" + path] = handler; 
        }
    }

    void post(std::string path, Handler handler) { 
        if (path.find(':') != std::string::npos) {
            dynamic_routes.push_back({"POST", path, handler});
        } else {
            static_routes["POST:" + path] = handler; 
        }
    }

    void listen() {
        struct sockaddr_in address;
        int addrlen = sizeof(address);

        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("Socket failed"); exit(EXIT_FAILURE);
        }
        int opt = 1;
        #ifdef _WIN32
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        #else
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        #endif
        
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (::bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("Bind failed"); exit(EXIT_FAILURE);
        }
        if (::listen(server_fd, 10) < 0) {
            perror("Listen failed"); exit(EXIT_FAILURE);
        }

        std::cout << "--------------------------------------" << std::endl;
        std::cout << "🔥 Nefia v" << NEFIA_VERSION << " (ThreadPool & Routing) Ready." << std::endl;
        std::cout << "👉 http://localhost:" << port << std::endl;
        std::cout << "--------------------------------------" << std::endl;

        while (true) {
            socket_t new_socket;
            new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (!IS_VALID_SOCKET(new_socket)) {
                continue;
            }
            thread_pool->enqueue([this, new_socket] {
                this->handle_client(new_socket);
            });
        }
    }
};
