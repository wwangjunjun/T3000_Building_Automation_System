// AgentBridgeStandalone.h: 不依赖 MFC 的 AgentBridge 独立版本
// 可在 Linux/Windows 编译，使用标准 C++ 库
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <sstream>
#include <memory>
#include <iostream>
#include <fstream>
#include <ctime>
#include <algorithm>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <http.h>
    #pragma comment(lib, "httpapi.lib")
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "crypt32.lib")
    #pragma comment(lib, "bcrypt.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <poll.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    #define closesocket close
#endif

// ============================================
// JSON 类（纯 C++，不依赖外部库）
// ============================================
class JsonValue {
public:
    enum Type { NULL_TYPE, BOOL_TYPE, INT_TYPE, DOUBLE_TYPE, STRING_TYPE, OBJECT_TYPE, ARRAY_TYPE };

    JsonValue() : type_(NULL_TYPE), int_val_(0), double_val_(0.0), bool_val_(false) {}
    JsonValue(bool v) : type_(BOOL_TYPE), bool_val_(v), int_val_(0), double_val_(0.0) {}
    JsonValue(int v) : type_(INT_TYPE), int_val_(v), double_val_(0.0), bool_val_(false) {}
    JsonValue(double v) : type_(DOUBLE_TYPE), double_val_(v), int_val_(0), bool_val_(false) {}
    JsonValue(const std::string& v) : type_(STRING_TYPE), str_val_(v), int_val_(0), double_val_(0.0), bool_val_(false) {}
    JsonValue(const char* v) : type_(STRING_TYPE), str_val_(v), int_val_(0), double_val_(0.0), bool_val_(false) {}

    static JsonValue Object() { JsonValue v; v.type_ = OBJECT_TYPE; return v; }
    static JsonValue Array() { JsonValue v; v.type_ = ARRAY_TYPE; return v; }

    void Add(const std::string& key, const JsonValue& val) {
        keys_.push_back(key);
        vals_.push_back(val);
    }
    void Add(const JsonValue& val) {
        arr_.push_back(val);
    }

    std::string ToString() const {
        switch (type_) {
            case NULL_TYPE: return "null";
            case BOOL_TYPE: return bool_val_ ? "true" : "false";
            case INT_TYPE: return std::to_string(int_val_);
            case DOUBLE_TYPE: {
                std::ostringstream oss;
                oss.precision(6);
                oss << double_val_;
                return oss.str();
            }
            case STRING_TYPE: return "\"" + Escape(str_val_) + "\"";
            case OBJECT_TYPE: {
                std::string r = "{";
                for (size_t i = 0; i < keys_.size(); i++) {
                    if (i > 0) r += ",";
                    r += "\"" + Escape(keys_[i]) + "\":" + vals_[i].ToString();
                }
                return r + "}";
            }
            case ARRAY_TYPE: {
                std::string r = "[";
                for (size_t i = 0; i < arr_.size(); i++) {
                    if (i > 0) r += ",";
                    r += arr_[i].ToString();
                }
                return r + "]";
            }
        }
        return "null";
    }

    Type Type() const { return type_; }
    int AsInt() const { return int_val_; }
    double AsDouble() const { return double_val_; }
    bool AsBool() const { return bool_val_; }
    const std::string& AsString() const { return str_val_; }

private:
    Type type_;
    int int_val_;
    double double_val_;
    bool bool_val_;
    std::string str_val_;
    std::vector<std::string> keys_;
    std::vector<JsonValue> vals_;
    std::vector<JsonValue> arr_;

    std::string Escape(const std::string& s) const {
        std::string r;
        for (char c : s) {
            switch (c) {
                case '"': r += "\\\""; break;
                case '\\': r += "\\\\"; break;
                case '\n': r += "\\n"; break;
                case '\r': r += "\\r"; break;
                case '\t': r += "\\t"; break;
                default: r += c; break;
            }
        }
        return r;
    }
};

// ============================================
// HTTP 请求/响应
// ============================================
struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
    std::map<std::string, std::string> params;
};

struct HttpResponse {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::string body;
    std::map<std::string, std::string> headers;
};

// ============================================
// 设备/输入输出/告警数据结构
// ============================================
struct DeviceInfo {
    int id;
    std::string name;
    std::string model;
    std::string serialNumber;
    std::string firmwareVersion;
    bool online;
    int bacnetId;
    int modbusId;
};

struct PointInfo {
    int id;
    std::string name;
    double value;
    std::string unit;
    bool writable;
};

struct AlarmInfo {
    int id;
    int deviceId;
    std::string deviceName;
    std::string description;
    int severity;
    bool acknowledged;
    std::string timestamp;
};

// ============================================
// HTTP 服务器（跨平台）
// ============================================
class HttpServer {
public:
    using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

    HttpServer() : port_(8080), running_(false), socket_(INVALID_SOCKET) {}
    ~HttpServer() { Stop(); }

    bool Start(int port, const std::string& bindAddr = "0.0.0.0");
    void Stop();
    bool IsRunning() const { return running_; }

    void RegisterRoute(const std::string& method, const std::string& path, Handler handler);
    void RegisterCatchAll(Handler handler);

    int GetPort() const { return port_; }

private:
    int port_;
    std::string bindAddr_;
    std::atomic<bool> running_;
    SOCKET socket_;
    std::thread worker_;
    std::vector<std::pair<std::string, Handler>> routes_;
    Handler catchAll_;
    std::mutex routesMutex_;

    void Worker();
    bool MatchRoute(const std::string& method, const std::string& path,
                    Handler& handler, std::map<std::string, std::string>& params);
    void SendResponse(SOCKET client, const HttpResponse& response);
};

// ============================================
// WebSocket 服务器（跨平台）
// ============================================
class WebSocketServer {
public:
    WebSocketServer() : port_(8081), running_(false), listenSocket_(INVALID_SOCKET) {}
    ~WebSocketServer() { Stop(); }

    bool Start(int port, const std::string& bindAddr = "0.0.0.0");
    void Stop();
    bool IsRunning() const { return running_; }

    void Broadcast(const std::string& message);
    void BroadcastJson(const JsonValue& json);
    int GetClientCount();

private:
    int port_;
    std::string bindAddr_;
    std::atomic<bool> running_;
    SOCKET listenSocket_;
    std::thread worker_;
    std::mutex clientsMutex_;
    std::vector<SOCKET> clients_;

    void Worker();
    void HandleClient(SOCKET client, const std::string& ip, int port);
    bool Handshake(SOCKET socket, const std::string& key);
    bool SendMessage(SOCKET socket, const std::string& message);
};

// ============================================
// MCP 服务器（跨平台）
// ============================================
class McpServer {
public:
    using ToolHandler = std::function<JsonValue(const JsonValue&)>;

    McpServer() : port_(8082), running_(false), socket_(INVALID_SOCKET) {}
    ~McpServer() { Stop(); }

    bool Start(int port, const std::string& bindAddr = "0.0.0.0");
    void Stop();
    bool IsRunning() const { return running_; }

    void RegisterTool(const std::string& name, const std::string& description,
                      const JsonValue& inputSchema);
    void RegisterToolHandler(const std::string& name, ToolHandler handler);

private:
    int port_;
    std::string bindAddr_;
    std::atomic<bool> running_;
    SOCKET socket_;
    std::thread worker_;
    std::vector<std::pair<std::string, std::string>> tools_;  // name, description
    std::map<std::string, ToolHandler> handlers_;
    std::mutex toolsMutex_;

    void Worker();
    void HandleClient(SOCKET client);
    JsonValue HandleRequest(const std::string& method, const JsonValue& params);
};

// ============================================
// AgentBridge 主类（不依赖 MFC）
// ============================================
class AgentBridge {
public:
    AgentBridge();
    ~AgentBridge();

    bool Initialize();
    bool Start();
    void Stop();
    bool IsRunning() const { return running_; }

    void SetPort(int port) { port_ = port; }
    int GetPort() const { return port_; }
    void SetApiKey(const std::string& key) { apiKey_ = key; }
    std::string GetApiKey() const { return apiKey_; }
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    // 设备数据回调（由外部提供）
    using GetDevicesCallback = std::function<std::vector<DeviceInfo>()>;
    using GetDeviceByIdCallback = std::function<DeviceInfo(int)>;
    using GetInputsCallback = std::function<std::vector<PointInfo>(int)>;
    using GetOutputsCallback = std::function<std::vector<PointInfo>(int)>;
    using WriteOutputCallback = std::function<bool(int, double)>;
    using ReadInputCallback = std::function<double(int)>;
    using GetAlarmsCallback = std::function<std::vector<AlarmInfo>()>;
    using AckAlarmCallback = std::function<bool(int)>;
    using StartScanCallback = std::function<bool(bool)>;

    void SetGetDevicesCallback(GetDevicesCallback cb) { getDevicesCb_ = cb; }
    void SetGetDeviceByIdCallback(GetDeviceByIdCallback cb) { getDeviceByIdCb_ = cb; }
    void SetGetInputsCallback(GetInputsCallback cb) { getInputsCb_ = cb; }
    void SetGetOutputsCallback(GetOutputsCallback cb) { getOutputsCb_ = cb; }
    void SetWriteOutputCallback(WriteOutputCallback cb) { writeOutputCb_ = cb; }
    void SetReadInputCallback(ReadInputCallback cb) { readInputCb_ = cb; }
    void SetGetAlarmsCallback(GetAlarmsCallback cb) { getAlarmsCb_ = cb; }
    void SetAckAlarmCallback(AckAlarmCallback cb) { ackAlarmCb_ = cb; }
    void SetStartScanCallback(StartScanCallback cb) { startScanCb_ = cb; }

    // 日志回调
    using LogCallback = std::function<void(const std::string&)>;
    void SetLogCallback(LogCallback cb) { logCb_ = cb; }

private:
    int port_;
    std::string apiKey_;
    bool enabled_;
    std::atomic<bool> running_;

    HttpServer httpServer_;
    WebSocketServer wsServer_;
    McpServer mcpServer_;

    // 数据回调
    GetDevicesCallback getDevicesCb_;
    GetDeviceByIdCallback getDeviceByIdCb_;
    GetInputsCallback getInputsCb_;
    GetOutputsCallback getOutputsCb_;
    WriteOutputCallback writeOutputCb_;
    ReadInputCallback readInputCb_;
    GetAlarmsCallback getAlarmsCb_;
    AckAlarmCallback ackAlarmCb_;
    StartScanCallback startScanCb_;
    LogCallback logCb_;

    void RegisterApiRoutes();
    bool Authenticate(const HttpRequest& req, HttpResponse& resp);
    void Log(const std::string& msg);
};
