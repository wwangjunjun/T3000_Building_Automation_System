// AgentWebSocket.h: WebSocket 实时事件推送
#pragma once

#include "AgentBridge.h"
#include <deque>

// ============================================
// WebSocket 客户端连接
// ============================================
class CAgentWebSocketClient {
public:
    CAgentWebSocketClient() : m_socket(INVALID_SOCKET) {}
    ~CAgentWebSocketClient() {}

    SOCKET m_socket;
    std::string m_remoteIp;
    int m_remotePort;
    bool m_connected;
    time_t m_connectTime;
};

// ============================================
// WebSocket 服务器类
// ============================================
class CAgentWebSocketServer {
public:
    CAgentWebSocketServer();
    ~CAgentWebSocketServer();

    bool Initialize(int port, CAgentBridge* bridge);
    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }

    // 广播消息给所有客户端
    void Broadcast(const std::string& message);
    void BroadcastJson(const CAgentJson& json);

    // 获取连接数
    int GetClientCount();

private:
    int m_port;
    CAgentBridge* m_bridge;
    std::atomic<bool> m_running;
    std::thread m_workerThread;
    SOCKET m_listenSocket;

    std::mutex m_clientsMutex;
    std::vector<CAgentWebSocketClient*> m_clients;

    // 事件队列
    std::mutex m_eventQueueMutex;
    std::deque<std::string> m_eventQueue;

    void WorkerThread();
    void HandleClient(SOCKET clientSocket, const std::string& ip, int port);
    bool WebSocketHandshake(SOCKET socket, const std::string& key);
    bool SendMessage(SOCKET socket, const std::string& message);
    void ProcessEventQueue();
    void RemoveDisconnectedClients();
};
