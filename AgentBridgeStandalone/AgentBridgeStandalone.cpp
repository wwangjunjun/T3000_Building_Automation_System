// AgentBridgeStandalone.cpp: 不依赖 MFC 的 AgentBridge 实现
#include "AgentBridgeStandalone.h"
#include <cstring>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32")
#else
    #include <openssl/sha.h>
    #include <openssl/bio.h>
    #include <openssl/evp.h>
    #include <base64.h>
#endif

// ============================================
// HttpServer 实现
// ============================================
bool HttpServer::Start(int port, const std::string& bindAddr) {
    port_ = port;
    bindAddr_ = bindAddr;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr(bindAddr_.c_str());

    if (bind(socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(socket_);
        return false;
    }

    if (listen(socket_, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(socket_);
        return false;
    }

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(socket_, FIONBIO, &mode);
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
#endif

    running_ = true;
    worker_ = std::thread(&HttpServer::Worker, this);
    return true;
}

void HttpServer::Stop() {
    running_ = false;
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (worker_.joinable()) worker_.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

void HttpServer::Worker() {
    char buffer[65536];
    while (running_) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        SOCKET client = accept(socket_, (sockaddr*)&clientAddr, &addrLen);

        if (client != INVALID_SOCKET) {
            // 接收请求
            int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
            if (bytes > 0) {
                buffer[bytes] = '\0';

                HttpRequest request;
                HttpResponse response;

                // 解析请求行
                std::string req(buffer);
                size_t firstLineEnd = req.find("\r\n");
                if (firstLineEnd != std::string::npos) {
                    std::string firstLine = req.substr(0, firstLineEnd);
                    std::istringstream iss(firstLine);
                    iss >> request.method >> request.path;

                    // 解析查询字符串
                    size_t qpos = request.path.find('?');
                    if (qpos != std::string::npos) {
                        request.query = request.path.substr(qpos + 1);
                        request.path = request.path.substr(0, qpos);
                    }
                }

                // 解析 Body
                size_t bodyPos = req.find("\r\n\r\n");
                if (bodyPos != std::string::npos) {
                    request.body = req.substr(bodyPos + 4);
                }

                // 路由匹配
                Handler handler = nullptr;
                std::map<std::string, std::string> params;
                {
                    std::lock_guard<std::mutex> lock(routesMutex_);
                    for (auto& route : routes_) {
                        // 简单路径匹配
                        if (route.first == request.method + " " + request.path) {
                            handler = route.second;
                            break;
                        }
                    }
                    if (!handler && catchAll_) {
                        handler = catchAll_;
                    }
                }

                if (handler) {
                    handler(request, response);
                } else {
                    response.statusCode = 404;
                    response.body = "{\"error\":\"Not Found\"}";
                }

                // 添加 CORS 头
                response.headers["Access-Control-Allow-Origin"] = "*";
                response.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
                response.headers["Access-Control-Allow-Headers"] = "Content-Type, X-API-Key";

                SendResponse(client, response);
            }
            closesocket(client);
        }

#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
}

void HttpServer::SendResponse(SOCKET client, const HttpResponse& response) {
    std::string statusLine = "HTTP/1.1 " + std::to_string(response.statusCode) + " ";
    switch (response.statusCode) {
        case 200: statusLine += "OK"; break;
        case 400: statusLine += "Bad Request"; break;
        case 401: statusLine += "Unauthorized"; break;
        case 404: statusLine += "Not Found"; break;
        case 500: statusLine += "Internal Server Error"; break;
        default: statusLine += "Error"; break;
    }
    statusLine += "\r\n";

    std::string headers = statusLine;
    headers += "Content-Type: " + response.contentType + "\r\n";
    headers += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    for (auto& kv : response.headers) {
        headers += kv.first + ": " + kv.second + "\r\n";
    }
    headers += "\r\n";

    send(client, headers.c_str(), (int)headers.size(), 0);
    if (!response.body.empty()) {
        send(client, response.body.c_str(), (int)response.body.size(), 0);
    }
}

void HttpServer::RegisterRoute(const std::string& method, const std::string& path, Handler handler) {
    std::lock_guard<std::mutex> lock(routesMutex_);
    routes_.push_back({method + " " + path, handler});
}

void HttpServer::RegisterCatchAll(Handler handler) {
    std::lock_guard<std::mutex> lock(routesMutex_);
    catchAll_ = handler;
}

// ============================================
// WebSocketServer 实现
// ============================================
bool WebSocketServer::Start(int port, const std::string& bindAddr) {
    port_ = port;
    bindAddr_ = bindAddr;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr(bindAddr_.c_str());

    if (bind(listenSocket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listenSocket_);
        return false;
    }

    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSocket_);
        return false;
    }

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(listenSocket_, FIONBIO, &mode);
#else
    int flags = fcntl(listenSocket_, F_GETFL, 0);
    fcntl(listenSocket_, F_SETFL, flags | O_NONBLOCK);
#endif

    running_ = true;
    worker_ = std::thread(&WebSocketServer::Worker, this);
    return true;
}

void WebSocketServer::Stop() {
    running_ = false;
    if (listenSocket_ != INVALID_SOCKET) closesocket(listenSocket_);
    if (worker_.joinable()) worker_.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

void WebSocketServer::Worker() {
    while (running_) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        SOCKET client = accept(listenSocket_, (sockaddr*)&clientAddr, &addrLen);

        if (client != INVALID_SOCKET) {
            char ipStr[64] = {0};
#ifdef _WIN32
            inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
#else
            inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
#endif
            HandleClient(client, ipStr, ntohs(clientAddr.sin_port));
        }

#ifdef _WIN32
        Sleep(50);
#else
        usleep(50000);
#endif
    }
}

void WebSocketServer::HandleClient(SOCKET client, const std::string& ip, int port) {
    char buffer[8192];
    int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) { closesocket(client); return; }
    buffer[bytes] = '\0';

    std::string request(buffer);
    if (request.find("Upgrade: websocket") == std::string::npos &&
        request.find("Upgrade: WebSocket") == std::string::npos) {
        const char* resp = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client, resp, strlen(resp), 0);
        closesocket(client);
        return;
    }

    // 提取 Sec-WebSocket-Key
    std::string wsKey;
    std::istringstream stream(request);
    std::string line;
    while (std::getline(stream, line) && line != "\r") {
        if (line.find("Sec-WebSocket-Key:") != std::string::npos) {
            wsKey = line.substr(line.find(":") + 1);
            wsKey.erase(0, wsKey.find_first_not_of(" \t"));
            wsKey.erase(wsKey.find_last_not_of(" \t\r\n") + 1);
            break;
        }
    }

    if (wsKey.empty()) { closesocket(client); return; }

    // 计算 Accept Key
    std::string magic = "258EAFA5-E914-47DA-95CA-5AB0AAB0C0D5";
    std::string acceptInput = wsKey + magic;

#ifdef _WIN32
    // Windows 简易 SHA1（实际应使用 BCrypt）
    unsigned char sha1[20];
    // 简化处理，实际应使用 Windows Crypto API
    memset(sha1, 0, 20);
#else
    unsigned char sha1[20];
    SHA1((const unsigned char*)acceptInput.c_str(), acceptInput.size(), sha1);
#endif

    // Base64 编码
    std::string acceptKey;
#ifdef _WIN32
    // Windows 简化处理
    acceptKey = "dGhlIHNhbXBsZSBub25jZQ==";
#else
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, sha1, 20);
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    acceptKey = std::string(bptr->data, bptr->length - 1);
    BIO_free_all(b64);
#endif

    // 发送握手响应
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n";
    send(client, response.c_str(), (int)response.size(), 0);

    // 添加到客户端列表
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.push_back(client);
    }

    // 发送欢迎消息
    JsonValue welcome = JsonValue::Object();
    welcome.Add("type", JsonValue("welcome"));
    welcome.Add("message", JsonValue("Connected to AgentBridge"));
    Broadcast(welcome.ToString());

    // 接收循环
    while (running_) {
        memset(buffer, 0, sizeof(buffer));
        bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;

        // 解析 WebSocket 帧并回显
        if (bytes >= 2) {
            unsigned char opcode = buffer[0] & 0x0F;
            if (opcode == 0x01 || opcode == 0x02) {
                SendMessage(client, std::string(buffer + 2, bytes - 2));
            } else if (opcode == 0x08) {
                break;  // Close
            } else if (opcode == 0x09) {
                char pong[2] = {(char)0x8A, 0x00};
                send(client, pong, 2, 0);
            }
        }
    }

    // 清理
    closesocket(client);
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
    }
}

bool WebSocketServer::SendMessage(SOCKET socket, const std::string& message) {
    if (message.empty()) return false;
    std::vector<char> frame;
    size_t len = message.size();
    frame.push_back((char)0x81);  // FIN + Text
    if (len <= 125) {
        frame.push_back((char)len);
    } else if (len <= 65535) {
        frame.push_back((char)126);
        frame.push_back((char)(len >> 8));
        frame.push_back((char)(len & 0xFF));
    }
    frame.insert(frame.end(), message.begin(), message.end());
    return send(socket, frame.data(), (int)frame.size(), 0) == (int)frame.size();
}

void WebSocketServer::Broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto client : clients_) {
        SendMessage(client, message);
    }
}

void WebSocketServer::BroadcastJson(const JsonValue& json) {
    Broadcast(json.ToString());
}

int WebSocketServer::GetClientCount() {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return (int)clients_.size();
}

// ============================================
// McpServer 实现
// ============================================
bool McpServer::Start(int port, const std::string& bindAddr) {
    port_ = port;
    bindAddr_ = bindAddr;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr(bindAddr_.c_str());

    if (bind(socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(socket_);
        return false;
    }

    if (listen(socket_, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(socket_);
        return false;
    }

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(socket_, FIONBIO, &mode);
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
#endif

    running_ = true;
    worker_ = std::thread(&McpServer::Worker, this);
    return true;
}

void McpServer::Stop() {
    running_ = false;
    if (socket_ != INVALID_SOCKET) closesocket(socket_);
    if (worker_.joinable()) worker_.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

void McpServer::Worker() {
    while (running_) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        SOCKET client = accept(socket_, (sockaddr*)&clientAddr, &addrLen);

        if (client != INVALID_SOCKET) {
            std::thread(&McpServer::HandleClient, this, client).detach();
        }

#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
}

void McpServer::HandleClient(SOCKET client) {
    char buffer[65536];
    std::string accumulated;

#ifdef _WIN32
    u_long mode = 0;
    ioctlsocket(client, FIONBIO, &mode);
#else
    int flags = fcntl(client, F_GETFL, 0);
    fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
#endif

    while (running_) {
        int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        accumulated += buffer;

        size_t pos;
        while ((pos = accumulated.find('\n')) != std::string::npos) {
            std::string line = accumulated.substr(0, pos);
            accumulated = accumulated.substr(pos + 1);
            if (line.empty() || line == "\r") continue;

            // 简易 JSON-RPC 解析
            std::string method, id;
            size_t methodPos = line.find("\"method\"");
            if (methodPos != std::string::npos) {
                size_t colonPos = line.find(':', methodPos);
                size_t quotePos = line.find('"', colonPos);
                size_t endQuote = line.find('"', quotePos + 1);
                if (endQuote != std::string::npos) {
                    method = line.substr(quotePos + 1, endQuote - quotePos - 1);
                }
            }

            size_t idPos = line.find("\"id\"");
            if (idPos != std::string::npos) {
                size_t colonPos = line.find(':', idPos);
                size_t start = line.find_first_of("\"{[0123456789tfn", colonPos);
                if (start != std::string::npos) {
                    size_t end = line.find_first_of(",}\n\r", start);
                    if (end != std::string::npos) {
                        id = line.substr(start, end - start);
                    }
                }
            }

            // 处理请求
            JsonValue result = HandleRequest(method, JsonValue::Object());

            // 发送响应
            std::string response = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result.ToString() + "}\n";
            send(client, response.c_str(), (int)response.size(), 0);
        }
    }
    closesocket(client);
}

JsonValue McpServer::HandleRequest(const std::string& method, const JsonValue& params) {
    std::lock_guard<std::mutex> lock(toolsMutex_);

    if (method == "tools/list") {
        JsonValue tools = JsonValue::Array();
        for (auto& tool : tools_) {
            JsonValue t = JsonValue::Object();
            t.Add("name", JsonValue(tool.first));
            t.Add("description", JsonValue(tool.second));
            tools.Add(t);
        }
        JsonValue result = JsonValue::Object();
        result.Add("tools", tools);
        return result;
    }

    if (method == "ping") {
        return JsonValue::Object();
    }

    if (method == "initialize") {
        JsonValue result = JsonValue::Object();
        result.Add("protocolVersion", JsonValue("2024-11-05"));
        JsonValue serverInfo = JsonValue::Object();
        serverInfo.Add("name", JsonValue("AgentBridge"));
        serverInfo.Add("version", JsonValue("1.0.0"));
        result.Add("serverInfo", serverInfo);
        return result;
    }

    auto it = handlers_.find(method);
    if (it != handlers_.end()) {
        return it->second(params);
    }

    JsonValue error = JsonValue::Object();
    error.Add("code", JsonValue(-32601));
    error.Add("message", JsonValue("Method not found: " + method));
    return error;
}

void McpServer::RegisterTool(const std::string& name, const std::string& description, const JsonValue& inputSchema) {
    std::lock_guard<std::mutex> lock(toolsMutex_);
    tools_.push_back({name, description});
}

void McpServer::RegisterToolHandler(const std::string& name, ToolHandler handler) {
    std::lock_guard<std::mutex> lock(toolsMutex_);
    handlers_[name] = handler;
}

// ============================================
// AgentBridge 实现
// ============================================
AgentBridge::AgentBridge()
    : port_(8080), enabled_(false), running_(false) {}

AgentBridge::~AgentBridge() { Stop(); }

bool AgentBridge::Initialize() {
    RegisterApiRoutes();
    Log("AgentBridge initialized");
    return true;
}

bool AgentBridge::Start() {
    if (running_ || !enabled_) return false;

    bool ok = true;
    if (!httpServer_.Start(port_)) { Log("HTTP server start failed"); ok = false; }
    if (!wsServer_.Start(port_ + 1)) { Log("WebSocket server start failed"); ok = false; }
    if (!mcpServer_.Start(port_ + 2)) { Log("MCP server start failed"); ok = false; }

    if (ok) {
        running_ = true;
        Log("AgentBridge started");
    }
    return ok;
}

void AgentBridge::Stop() {
    running_ = false;
    httpServer_.Stop();
    wsServer_.Stop();
    mcpServer_.Stop();
    Log("AgentBridge stopped");
}

bool AgentBridge::Authenticate(const HttpRequest& req, HttpResponse& resp) {
    if (apiKey_.empty()) return true;

    auto it = req.headers.find("X-API-Key");
    if (it == req.headers.end()) {
        it = req.headers.find("Authorization");
        if (it != req.headers.end() && it->second.substr(0, 7) == "Bearer ") {
            if (it->second.substr(7) == apiKey_) return true;
        }
        resp.statusCode = 401;
        resp.body = "{\"error\":\"Missing API key\"}";
        return false;
    }

    if (it->second != apiKey_) {
        resp.statusCode = 401;
        resp.body = "{\"error\":\"Invalid API key\"}";
        return false;
    }
    return true;
}

void AgentBridge::RegisterApiRoutes() {
    auto auth = [this](const HttpRequest& req, HttpResponse& resp) -> bool {
        if (req.method == "OPTIONS" || req.path == "/") return true;
        return Authenticate(req, resp);
    };

    // GET /
    httpServer_.RegisterRoute("GET", "/", [this](const HttpRequest&, HttpResponse& resp) {
        JsonValue info = JsonValue::Object();
        info.Add("name", JsonValue("AgentBridge"));
        info.Add("version", JsonValue("1.0.0"));
        resp.body = info.ToString();
    });

    // GET /api/v1/system/info
    httpServer_.RegisterRoute("GET", "/api/v1/system/info", [this, auth](const HttpRequest& req, HttpResponse& resp) {
        if (!auth(req, resp)) return;
        JsonValue info = JsonValue::Object();
        info.Add("name", JsonValue("T3000 AgentBridge"));
        info.Add("version", JsonValue("1.0.0"));
        info.Add("http_port", JsonValue(port_));
        info.Add("websocket_port", JsonValue(port_ + 1));
        info.Add("mcp_port", JsonValue(port_ + 2));
        resp.body = JsonValue::Object();
        // 简化处理
        resp.body = "{\"success\":true,\"data\":" + info.ToString() + "}";
    });

    // GET /api/v1/devices
    httpServer_.RegisterRoute("GET", "/api/v1/devices", [this, auth](const HttpRequest& req, HttpResponse& resp) {
        if (!auth(req, resp)) return;
        JsonValue devices = JsonValue::Array();
        if (getDevicesCb_) {
            for (auto& dev : getDevicesCb_()) {
                JsonValue d = JsonValue::Object();
                d.Add("id", JsonValue(dev.id));
                d.Add("name", JsonValue(dev.name));
                d.Add("model", JsonValue(dev.model));
                d.Add("online", JsonValue(dev.online));
                devices.Add(d);
            }
        }
        resp.body = "{\"success\":true,\"data\":" + devices.ToString() + "}";
    });

    // GET /api/v1/inputs/{id}
    httpServer_.RegisterRoute("GET", "/api/v1/inputs/{id}", [this, auth](const HttpRequest& req, HttpResponse& resp) {
        if (!auth(req, resp)) return;
        int id = 0;
        try { id = std::stoi(req.params.at("id")); } catch (...) {
            resp.statusCode = 400;
            resp.body = "{\"error\":\"Invalid input ID\"}";
            return;
        }
        double value = readInputCb_ ? readInputCb_(id) : 0.0;
        resp.body = "{\"success\":true,\"data\":{\"input_id\":" + std::to_string(id) + ",\"value\":" + std::to_string(value) + "}}";
    });

    // POST /api/v1/outputs/{id}/write
    httpServer_.RegisterRoute("POST", "/api/v1/outputs/{id}/write", [this, auth](const HttpRequest& req, HttpResponse& resp) {
        if (!auth(req, resp)) return;
        int id = 0;
        try { id = std::stoi(req.params.at("id")); } catch (...) {
            resp.statusCode = 400;
            resp.body = "{\"error\":\"Invalid output ID\"}";
            return;
        }
        double value = 0;
        try {
            size_t pos = req.body.find("\"value\"");
            if (pos != std::string::npos) {
                size_t colonPos = req.body.find(':', pos);
                std::string valStr = req.body.substr(colonPos + 1);
                size_t start = valStr.find_first_of("-0123456789.");
                if (start != std::string::npos) {
                    size_t end = valStr.find_first_of(",} \t\r\n", start);
                    value = std::stod(valStr.substr(start, end != std::string::npos ? end - start : std::string::npos));
                }
            }
        } catch (...) {
            resp.statusCode = 400;
            resp.body = "{\"error\":\"Invalid value\"}";
            return;
        }
        bool success = writeOutputCb_ ? writeOutputCb_(id, value) : false;
        resp.body = "{\"success\":" + std::string(success ? "true" : "false") + ",\"output_id\":" + std::to_string(id) + ",\"value\":" + std::to_string(value) + "}";
    });

    // GET /api/v1/alarms
    httpServer_.RegisterRoute("GET", "/api/v1/alarms", [this, auth](const HttpRequest& req, HttpResponse& resp) {
        if (!auth(req, resp)) return;
        JsonValue alarms = JsonValue::Array();
        if (getAlarmsCb_) {
            for (auto& alarm : getAlarmsCb_()) {
                JsonValue a = JsonValue::Object();
                a.Add("id", JsonValue(alarm.id));
                a.Add("description", JsonValue(alarm.description));
                a.Add("acknowledged", JsonValue(alarm.acknowledged));
                alarms.Add(a);
            }
        }
        resp.body = "{\"success\":true,\"data\":" + alarms.ToString() + "}";
    });

    // POST /api/v1/system/scan
    httpServer_.RegisterRoute("POST", "/api/v1/system/scan", [this, auth](const HttpRequest& req, HttpResponse& resp) {
        if (!auth(req, resp)) return;
        bool fullScan = req.body.find("\"full_scan\":true") != std::string::npos;
        bool success = startScanCb_ ? startScanCb_(fullScan) : false;
        resp.body = "{\"success\":" + std::string(success ? "true" : "false") + ",\"scan_type\":\"" + std::string(fullScan ? "full" : "quick") + "\"}";
    });

    // OPTIONS
    httpServer_.RegisterRoute("OPTIONS", "*", [](const HttpRequest&, HttpResponse& resp) {
        resp.statusCode = 204;
    });
}

void AgentBridge::Log(const std::string& msg) {
    if (logCb_) logCb_(msg);
    else std::cout << "[AgentBridge] " << msg << std::endl;
}
