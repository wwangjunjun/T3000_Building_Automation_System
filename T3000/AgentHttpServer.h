// AgentHttpServer.h: HTTP REST API 服务器
// 基于 Windows HTTP API (HTTP.sys) 实现高性能 HTTP 服务器
#pragma once

#include "AgentBridge.h"

// Windows HTTP API 头文件
#include <http.h>
#pragma comment(lib, "httpapi.lib")

// ============================================
// HTTP 服务器类
// ============================================
class CAgentHttpServer {
public:
    CAgentHttpServer();
    ~CAgentHttpServer();

    bool Initialize(int port, CAgentBridge* bridge);
    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }

    // 请求处理回调
    typedef std::function<void(const AgentHttpRequest&, AgentHttpResponse&)> RequestHandler;
    void RegisterRoute(const std::string& method, const std::string& path, RequestHandler handler);
    void RegisterCatchAll(RequestHandler handler);

    int GetPort() const { return m_port; }

private:
    int m_port;
    CAgentBridge* m_bridge;
    HTTP_SERVER_HANDLE m_httpServerHandle;
    HTTP_URL_CONTEXT m_urlContext;
    std::atomic<bool> m_running;
    std::thread m_workerThread;

    // 路由表
    struct Route {
        std::string method;
        std::string path;
        RequestHandler handler;
        // 路径参数数量（用于匹配 /api/v1/devices/{id}）
        int paramCount;
    };
    std::vector<Route> m_routes;
    RequestHandler m_catchAllHandler;
    std::mutex m_routesMutex;

    // HTTP API 工作线程
    void WorkerThread();
    bool ProcessRequest(HTTP_REQUEST* pRequest);
    void SendResponse(HTTP_RESPONSE* pResponse, const AgentHttpResponse& response);

    // 路由匹配
    bool MatchRoute(const std::string& method, const std::string& path,
                    RequestHandler& handler, std::map<std::string, std::string>& params);
    std::string NormalizePath(const std::string& path);
};
