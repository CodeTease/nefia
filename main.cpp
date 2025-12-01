#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <sstream>
#include <map>
#include <functional>
#include <fstream>
#include <algorithm> // For search

// ---------------------------------------------------------
// NEFIA CORE DEFINITIONS (Hard-locked v0.1.0)
// ---------------------------------------------------------

const std::string NEFIA_VERSION = "0.1.0";
const int BUFFER_SIZE = 30720; // 30KB Limit

std::string get_mime_type(std::string path) {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css")  != std::string::npos) return "text/css";
    if (path.find(".js")   != std::string::npos) return "application/javascript";
    return "text/plain";
}

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;  // ?key=val
    std::map<std::string, std::string> headers; // Content-Type, etc.
    std::string body;                          // Raw POST body
    std::map<std::string, std::string> form;   // Parsed form data

    std::string get_header(std::string key) const {
        if (headers.find(key) != headers.end()) return headers.at(key);
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
};

struct Response {
    std::string body;
    int status_code = 200;
    std::string content_type = "text/html";
    std::map<std::string, std::string> headers; // Custom headers

    void set_header(std::string key, std::string val) {
        headers[key] = val;
    }

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

class Nefia {
private:
    int port;
    int server_fd;
    std::map<std::string, Handler> routes;

    std::vector<std::string> split(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    // Helper to parse key=val&key2=val2
    std::map<std::string, std::string> parse_url_encoded(std::string raw) {
        std::map<std::string, std::string> data;
        std::vector<std::string> pairs = split(raw, '&');
        for (const auto& pair : pairs) {
            size_t eq_pos = pair.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = pair.substr(0, eq_pos);
                std::string val = pair.substr(eq_pos + 1);
                // In a real framework, we must URL-decode here (e.g., %20 -> space)
                // For Nefia v0.1, we keep it raw for speed
                data[key] = val;
            }
        }
        return data;
    }

    Request parse_request(const char* buffer) {
        Request req;
        std::string raw_data(buffer);
        
        // 1. Separate Headers and Body
        // HTTP standard: Headers and Body are separated by \r\n\r\n (double CRLF)
        std::string delimiter = "\r\n\r\n";
        size_t split_pos = raw_data.find(delimiter);
        
        std::string header_part;
        if (split_pos != std::string::npos) {
            header_part = raw_data.substr(0, split_pos);
            req.body = raw_data.substr(split_pos + 4); // Skip the delimiter
        } else {
            header_part = raw_data; // No body
        }

        std::stringstream ss(header_part);
        std::string line;

        // 2. Parse Request Line (GET /path HTTP/1.1)
        if (std::getline(ss, line)) {
            // Remove \r if present
            if (!line.empty() && line.back() == '\r') line.pop_back();
            
            std::stringstream line_ss(line);
            std::string full_path;
            line_ss >> req.method >> full_path;

            // Parse Query Params
            size_t q_pos = full_path.find('?');
            if (q_pos != std::string::npos) {
                req.path = full_path.substr(0, q_pos);
                req.query = parse_url_encoded(full_path.substr(q_pos + 1));
            } else {
                req.path = full_path;
            }
        }

        // 3. Parse Headers
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;

            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string val = line.substr(colon_pos + 1);
                // Trim leading space in value
                if (!val.empty() && val[0] == ' ') val.erase(0, 1);
                req.headers[key] = val;
            }
        }

        // 4. Parse Body (if it's a form)
        // Check for Content-Type: application/x-www-form-urlencoded
        // Note: Headers keys might be Case-Sensitive depending on client, usually standard is Capitalized
        if (!req.body.empty()) {
           req.form = parse_url_encoded(req.body);
        }

        return req;
    }

    void handle_client(int client_socket) {
        char buffer[BUFFER_SIZE] = {0};
        read(client_socket, buffer, BUFFER_SIZE);

        Request req = parse_request(buffer);
        Response res;

        if (!req.path.empty()) {
            std::cout << "[Nefia] " << req.method << " " << req.path << std::endl;
        }

        std::string route_key = req.method + ":" + req.path;

        if (routes.find(route_key) != routes.end()) {
            routes[route_key](req, res);
        } else {
            res.status_code = 404;
            res.body = "<h1>404 Not Found</h1>";
        }

        std::stringstream response_stream;
        response_stream << "HTTP/1.1 " << res.status_code << " OK\r\n";
        response_stream << "Content-Type: " << res.content_type << "\r\n";
        response_stream << "Server: Nefia/" << NEFIA_VERSION << " (Teaserverse)\r\n";
        response_stream << "Content-Length: " << res.body.size() << "\r\n";
        
        // Inject Custom Headers
        for(auto const& [key, val] : res.headers) {
            response_stream << key << ": " << val << "\r\n";
        }

        response_stream << "\r\n";
        response_stream << res.body;

        std::string final_response = response_stream.str();
        write(client_socket, final_response.c_str(), final_response.size());
        close(client_socket);
    }

public:
    Nefia(int p) : port(p) {}

    void get(std::string path, Handler handler) { routes["GET:" + path] = handler; }
    void post(std::string path, Handler handler) { routes["POST:" + path] = handler; }

    void listen() {
        struct sockaddr_in address;
        int addrlen = sizeof(address);

        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("Socket failed"); exit(EXIT_FAILURE);
        }
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
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
        std::cout << "🔥 Nefia v" << NEFIA_VERSION << " (Body Parser) Ready." << std::endl;
        std::cout << "👉 http://localhost:" << port << std::endl;
        std::cout << "--------------------------------------" << std::endl;

        while (true) {
            int new_socket;
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                continue;
            }
            std::thread t(&Nefia::handle_client, this, new_socket);
            t.detach();
        }
    }
};
