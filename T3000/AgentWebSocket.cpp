// AgentWebSocket.cpp: WebSocket 实时事件推送实现
#include "stdafx.h"
#include "AgentWebSocket.h"
#include <ws2tcpip.h>
#include <shlwapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shlwapi.lib")

#include <cstring>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

// ============================================
// CAgentWebSocketServer 实现
// ============================================

CAgentWebSocketServer::CAgentWebSocketServer()
    : m_port(8081)
    , m_bridge(nullptr)
    , m_running(false)
    , m_listenSocket(INVALID_SOCKET)
{
}

CAgentWebSocketServer::~CAgentWebSocketServer() {
    Stop();
}

bool CAgentWebSocketServer::Initialize(int port, CAgentBridge* bridge) {
    m_port = port;
    m_bridge = bridge;
    return true;
}

bool CAgentWebSocketServer::Start() {
    if (m_running) return true;

    // 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        if (m_bridge) m_bridge->LogError(_T("AgentBridge: WebSocket WSAStartup failed"));
        return false;
    }

    // 创建监听套接字
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        if (m_bridge) m_bridge->LogError(_T("AgentBridge: WebSocket socket creation failed"));
        WSACleanup();
        return false;
    }

    // 设置地址重用
    int optVal = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optVal, sizeof(optVal));

    // 绑定
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(m_port);

    if (bind(m_listenSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        if (m_bridge) {
            CString msg;
            msg.Format(_T("AgentBridge: WebSocket bind failed (port %d)"), m_port);
            m_bridge->LogError(msg);
        }
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    // 监听
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        if (m_bridge) m_bridge->LogError(_T("AgentBridge: WebSocket listen failed"));
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    // 设置为非阻塞模式
    u_long mode = 1;
    ioctlsocket(m_listenSocket, FIONBIO, &mode);

    m_running = true;
    m_workerThread = std::thread(&CAgentWebSocketServer::WorkerThread, this);

    if (m_bridge) {
        CString msg;
        msg.Format(_T("AgentBridge WebSocket server started on port %d"), m_port);
        m_bridge->Log(msg);
    }

    return true;
}

void CAgentWebSocketServer::Stop() {
    if (!m_running) return;

    m_running = false;

    // 关闭监听套接字
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    // 等待工作线程退出
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto* client : m_clients) {
            if (client->m_socket != INVALID_SOCKET) {
                // 发送关闭帧
                char closeFrame[6] = { (char)0x88, 0x00 };  // Close frame, no payload
                send(client->m_socket, closeFrame, 2, 0);
                closesocket(client->m_socket);
            }
            delete client;
        }
        m_clients.clear();
    }

    WSACleanup();

    if (m_bridge) {
        m_bridge->Log(_T("AgentBridge WebSocket server stopped"));
    }
}

void CAgentWebSocketServer::WorkerThread() {
    while (m_running) {
        // 接受新连接
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(m_listenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);

        if (clientSocket != INVALID_SOCKET) {
            char ipStr[64] = { 0 };
            inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
            int port = ntohs(clientAddr.sin_port);

            // 在新线程中处理客户端
            std::thread(&CAgentWebSocketServer::HandleClient, this, clientSocket,
                       std::string(ipStr), port).detach();
        }

        // 处理事件队列
        ProcessEventQueue();

        // 清理断开的连接
        RemoveDisconnectedClients();

        Sleep(50);
    }
}

void CAgentWebSocketServer::HandleClient(SOCKET clientSocket, const std::string& ip, int port) {
    char buffer[8192];
    memset(buffer, 0, sizeof(buffer));

    // 接收 HTTP 升级请求
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        closesocket(clientSocket);
        return;
    }

    buffer[bytesReceived] = '\0';
    std::string request(buffer, bytesReceived);

    // 验证是 WebSocket 升级请求
    if (request.find("Upgrade: websocket") == std::string::npos &&
        request.find("Upgrade: WebSocket") == std::string::npos) {
        // 不是 WebSocket 请求，发送 400
        const char* response = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(clientSocket, response, strlen(response), 0);
        closesocket(clientSocket);
        return;
    }

    // 提取 Sec-WebSocket-Key
    std::string wsKey;
    std::istringstream stream(request);
    std::string line;
    while (std::getline(stream, line) && line != "\r") {
        if (line.find("Sec-WebSocket-Key:") != std::string::npos) {
            wsKey = line.substr(line.find(":") + 1);
            // 去除空格
            wsKey.erase(0, wsKey.find_first_not_of(" \t"));
            wsKey.erase(wsKey.find_last_not_of(" \t\r\n") + 1);
            break;
        }
    }

    if (wsKey.empty()) {
        const char* response = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(clientSocket, response, strlen(response), 0);
        closesocket(clientSocket);
        return;
    }

    // 发送 WebSocket 握手响应
    if (!WebSocketHandshake(clientSocket, wsKey)) {
        closesocket(clientSocket);
        return;
    }

    // 添加客户端
    auto* client = new CAgentWebSocketClient();
    client->m_socket = clientSocket;
    client->m_remoteIp = ip;
    client->m_remotePort = port;
    client->m_connected = true;
    client->m_connectTime = time(NULL);

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_clients.push_back(client);
    }

    if (m_bridge) {
        CString msg;
        msg.Format(_T("AgentBridge WebSocket client connected: %s:%d"), CString(ip.c_str()), port);
        m_bridge->Log(msg);
    }

    // 发送欢迎消息
    CAgentJson welcome;
    welcome.SetObject();
    welcome.Add("type", CAgentJson("welcome"));
    welcome.Add("message", CAgentJson("Connected to T3000 AgentBridge"));
    welcome.Add("timestamp", CAgentJson(std::to_string(time(NULL))));
    welcome.Add("clientCount", CAgentJson((int)m_clients.size()));
    BroadcastJson(welcome);

    // 接收消息循环
    while (m_running && client->m_connected) {
        memset(buffer, 0, sizeof(buffer));
        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived <= 0) {
            client->m_connected = false;
            break;
        }

        // 解析 WebSocket 帧
        if (bytesReceived >= 2) {
            unsigned char opcode = buffer[0] & 0x0F;
            unsigned char payloadLen = buffer[1] & 0x7F;
            int headerSize = 2;

            // 处理扩展长度
            if (payloadLen == 126) {
                headerSize = 4;
            } else if (payloadLen == 127) {
                headerSize = 10;
            }

            // 检查掩码
            bool masked = (buffer[1] & 0x80) != 0;
            if (masked) headerSize += 4;

            if (bytesReceived >= headerSize) {
                // 提取 payload
                std::string payload(buffer + headerSize, bytesReceived - headerSize);

                // 解掩码
                if (masked && bytesReceived >= headerSize) {
                    unsigned char* maskingKey = (unsigned char*)(buffer + headerSize - 4);
                    for (size_t i = 0; i < payload.size(); i++) {
                        payload[i] ^= maskingKey[i % 4];
                    }
                }

                // 处理帧类型
                switch (opcode) {
                    case 0x01: // Text frame
                    case 0x02: // Binary frame
                        // 回显消息（可在此处添加消息处理逻辑）
                        SendMessage(clientSocket, payload);
                        break;
                    case 0x08: // Close frame
                        client->m_connected = false;
                        {
                            char closeFrame[2] = { (char)0x88, 0x00 };
                            send(clientSocket, closeFrame, 2, 0);
                        }
                        break;
                    case 0x09: // Ping
                        // 回复 Pong
                        {
                            char pongFrame[2] = { (char)0x8A, 0x00 };
                            send(clientSocket, pongFrame, 2, 0);
                        }
                        break;
                }
            }
        }
    }

    // 清理
    client->m_connected = false;
    closesocket(clientSocket);

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_clients.erase(
            std::remove(m_clients.begin(), m_clients.end(), client),
            m_clients.end()
        );
    }

    delete client;

    if (m_bridge) {
        CString msg;
        msg.Format(_T("AgentBridge WebSocket client disconnected: %s:%d"), CString(ip.c_str()), port);
        m_bridge->Log(msg);
    }
}

bool CAgentWebSocketServer::WebSocketHandshake(SOCKET socket, const std::string& key) {
    // 计算 Sec-WebSocket-Accept
    std::string magicGuid = "258EAFA5-E914-47DA-95CA-5AB0AAB0C0D5";
    std::string acceptInput = key + magicGuid;

    unsigned char sha1Result[20];
    SHA1((const unsigned char*)acceptInput.c_str(), acceptInput.size(), sha1Result);

    // Base64 编码
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, sha1Result, 20);
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string acceptKey(bptr->data, bptr->length - 1);  // 去除末尾换行

    BIO_free_all(b64);

    // 发送握手响应
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "\r\n";

    int sent = send(socket, response.c_str(), (int)response.size(), 0);
    return sent == (int)response.size();
}

bool CAgentWebSocketServer::SendMessage(SOCKET socket, const std::string& message) {
    if (message.empty()) return false;

    std::vector<char> frame;
    size_t len = message.size();

    // 帧头部
    frame.push_back((char)0x81);  // FIN + Text

    if (len <= 125) {
        frame.push_back((char)len);
    } else if (len <= 65535) {
        frame.push_back((char)126);
        frame.push_back((char)(len >> 8));
        frame.push_back((char)(len & 0xFF));
    } else {
        frame.push_back((char)127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((char)((len >> (i * 8)) & 0xFF));
        }
    }

    // 添加 payload
    frame.insert(frame.end(), message.begin(), message.end());

    int sent = send(socket, frame.data(), (int)frame.size(), 0);
    return sent == (int)frame.size();
}

void CAgentWebSocketServer::Broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    for (auto* client : m_clients) {
        if (client->m_connected) {
            SendMessage(client->m_socket, message);
        }
    }
}

void CAgentWebSocketServer::BroadcastJson(const CAgentJson& json) {
    Broadcast(json.ToString());
}

int CAgentWebSocketServer::GetClientCount() {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    int count = 0;
    for (auto* client : m_clients) {
        if (client->m_connected) count++;
    }
    return count;
}

void CAgentWebSocketServer::ProcessEventQueue() {
    std::lock_guard<std::mutex> lock(m_eventQueueMutex);
    while (!m_eventQueue.empty()) {
        std::string event = m_eventQueue.front();
        m_eventQueue.pop_front();
        Broadcast(event);
    }
}

void CAgentWebSocketServer::RemoveDisconnectedClients() {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    m_clients.erase(
        std::remove_if(m_clients.begin(), m_clients.end(),
            [](CAgentWebSocketClient* client) {
                return !client->m_connected;
            }),
        m_clients.end()
    );
}
