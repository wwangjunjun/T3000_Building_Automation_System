// AgentMcpServer.h: MCP (Model Context Protocol) 服务器
// 为 AI 智能体提供标准化的工具调用接口
#pragma once

#include "AgentBridge.h"

// ============================================
// MCP 协议数据结构
// ============================================

// JSON-RPC 2.0 消息
struct McpJsonRpcMessage {
    std::string jsonrpc;      // "2.0"
    std::string method;       // 方法名
    std::string id;           // 请求 ID
    CAgentJson params;        // 参数
    CAgentJson result;        // 结果
    CAgentJson error;         // 错误
    bool isResponse;          // 是否为响应

    McpJsonRpcMessage() : isResponse(false) {
        jsonrpc = "2.0";
    }
};

// MCP 工具定义
struct McpTool {
    std::string name;
    std::string description;
    CAgentJson inputSchema;  // JSON Schema 格式的参数定义
};

// ============================================
// MCP 服务器类
// ============================================
class CAgentMcpServer {
public:
    CAgentMcpServer();
    ~CAgentMcpServer();

    bool Initialize(int port, CAgentBridge* bridge);
    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }

    // 注册 MCP 工具
    void RegisterTool(const std::string& name, const std::string& description,
                      const CAgentJson& inputSchema);

    // 工具调用处理函数
    typedef std::function<CAgentJson(const CAgentJson&)> ToolHandler;
    void RegisterToolHandler(const std::string& name, ToolHandler handler);

    // 获取工具列表
    std::vector<McpTool> GetTools();

private:
    int m_port;
    CAgentBridge* m_bridge;
    std::atomic<bool> m_running;
    std::thread m_workerThread;
    SOCKET m_listenSocket;

    std::vector<McpTool> m_tools;
    std::map<std::string, ToolHandler> m_toolHandlers;
    std::mutex m_toolsMutex;

    void WorkerThread();
    void HandleClient(SOCKET clientSocket);
    McpJsonRpcMessage ParseMessage(const std::string& rawMessage);
    std::string FormatResponse(const McpJsonRpcMessage& response);
    CAgentJson HandleRequest(const McpJsonRpcMessage& request);

    // 内置工具处理
    CAgentJson HandleListTools(const CAgentJson& params);
    CAgentJson HandleCallTool(const CAgentJson& params);
    CAgentJson HandleInitialize(const CAgentJson& params);
    CAgentJson HandlePing(const CAgentJson& params);
};
