// AgentHttpServer.cpp: HTTP REST API 服务器实现
#include "stdafx.h"
#include "AgentHttpServer.h"
#include <sstream>
#include <algorithm>

// ============================================
// CAgentHttpServer 实现
// ============================================

CAgentHttpServer::CAgentHttpServer()
    : m_port(AGENTBRIDGE_DEFAULT_PORT)
    , m_bridge(nullptr)
    , m_httpServerHandle(NULL)
    , m_urlContext{0}
    , m_running(false)
    , m_catchAllHandler(nullptr)
{
}

CAgentHttpServer::~CAgentHttpServer() {
    Stop();
}

bool CAgentHttpServer::Initialize(int port, CAgentBridge* bridge) {
    m_port = port;
    m_bridge = bridge;

    // 初始化 HTTP API
    HTTPAPI_VERSION version = HTTP_API_VERSION_2;
    HRESULT hr = HttpInitialize(version, HTTP_INITIALIZE_SERVER, NULL);
    if (hr != NO_ERROR) {
        if (bridge) bridge->LogError(_T("AgentBridge: HttpInitialize failed"));
        return false;
    }

    // 创建服务器句柄
    hr = HttpCreateServer(version, NULL, 0, &m_httpServerHandle);
    if (hr != NO_ERROR) {
        HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
        if (bridge) bridge->LogError(_T("AgentBridge: HttpCreateServer failed"));
        return false;
    }

    return true;
}

bool CAgentHttpServer::Start() {
    if (m_running) return true;

    // 注册 URL
    std::wstring url = L"http://+:" + std::to_wstring(m_port) + L"/";
    HTTP_URL_CONTEXT context = { 0, (PVOID)this };

    HRESULT hr = HttpSetUrlVirtualHost(m_httpServerHandle,
        CStringW(url).GetString(),
        HttpUrlVirtualHostActionSet,
        NULL, 0,
        &context);

    if (hr != NO_ERROR) {
        // 尝试简单注册
        hr = HttpRegisterUrl(m_httpServerHandle, CStringW(url).GetString(), NULL);
        if (hr != NO_ERROR) {
            if (m_bridge) {
                CString msg;
                msg.Format(_T("AgentBridge: HttpRegisterUrl failed (0x%08X)"), hr);
                m_bridge->LogError(msg);
            }
            return false;
        }
    }

    m_running = true;
    m_workerThread = std::thread(&CAgentHttpServer::WorkerThread, this);

    if (m_bridge) {
        CString msg;
        msg.Format(_T("AgentBridge HTTP server started on port %d"), m_port);
        m_bridge->Log(msg);
    }

    return true;
}

void CAgentHttpServer::Stop() {
    if (!m_running) return;

    m_running = false;

    // 关闭所有挂起的请求
    if (m_httpServerHandle) {
        HttpReceiveRequests(m_httpServerHandle, 1000, NULL, NULL);
    }

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // 清理
    if (m_httpServerHandle) {
        HttpCloseServer(m_httpServerHandle);
        m_httpServerHandle = NULL;
    }
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);

    if (m_bridge) {
        m_bridge->Log(_T("AgentBridge HTTP server stopped"));
    }
}

void CAgentHttpServer::WorkerThread() {
    char buffer[65536];
    memset(buffer, 0, sizeof(buffer));

    while (m_running) {
        PHTTP_REQUEST pRequest = reinterpret_cast<PHTTP_REQUEST>(buffer);
        ULONG bytesRead = 0;

        // 接收请求
        HRESULT hr = HttpReceiveHttpRequest(
            m_httpServerHandle,
            HTTP_RECEIVE_REQUEST_CONTENT_OPTIONAL,
            0,
            pRequest,
            sizeof(buffer) - sizeof(HTTP_REQUEST),
            &bytesRead,
            NULL
        );

        if (hr == NO_ERROR) {
            ProcessRequest(pRequest);
        } else if (hr != ERROR_IGNORE) {
            // 非致命错误，继续
            if (hr != ERROR_OPERATION_ABORTED && m_running) {
                Sleep(100);
            }
        }
    }
}

bool CAgentHttpServer::ProcessRequest(HTTP_REQUEST* pRequest) {
    // 构建请求对象
    AgentHttpRequest request;
    AgentHttpResponse response;

    // 解析方法
    switch (pRequest->Verb) {
        case HttpVerbGET:     request.method = "GET"; break;
        case HttpVerbPOST:    request.method = "POST"; break;
        case HttpVerbPUT:     request.method = "PUT"; break;
        case HttpVerbDELETE:  request.method = "DELETE"; break;
        case HttpVerbHEAD:    request.method = "HEAD"; break;
        case HttpVerbOPTIONS: request.method = "OPTIONS"; break;
        default:              request.method = "UNKNOWN"; break;
    }

    // 解析 URL
    std::wstring urlPath(pRequest->pRawUrl ? pRequest->pRawUrl : pRequest->CookedUrl.pDecodedPath);
    std::wstring urlQuery = pRequest->CookedUrl.pDecodedQuery ? pRequest->CookedUrl.pDecodedQuery : L"";

    request.path = CW2A(urlPath.c_str(), CP_UTF8);
    request.query = CW2A(urlQuery.c_str(), CP_UTF8);

    // 解析头部
    for (USHORT i = 0; i < pRequest->Headers.KnownHeaderCount; i++) {
        if (pRequest->Headers.pKnownHeaders[i].pRawValue) {
            std::string value = CW2A(pRequest->Headers.pKnownHeaders[i].pRawValue, CP_UTF8);
            // 映射已知头部名称
            const wchar_t* headerNames[] = {
                L"Cache-Control", L"Connection", L"Date", L"Keep-Alive",
                L"Pragma", L"Trailer", L"Transfer-Encoding", L"Upgrade",
                L"Via", L"Warning", L"Allow", L"Content-Length",
                L"Content-Type", L"Content-Encoding", L"Content-Language",
                L"Content-Location", L"Content-MD5", L"Content-Range",
                L"Expires", L"Last-Modified", L"Accept", L"Accept-Charset",
                L"Accept-Encoding", L"Accept-Language", L"Authorization",
                L"Cookie", L"Expect", L"From", L"Host", L"If-Match",
                L"If-Modified-Since", L"If-None-Match", L"If-Range",
                L"If-Unmodified-Since", L"Max-Forwards", L"Proxy-Authorization",
                L"Range", L"Referer", L"TE", L"Translate", L"User-Agent",
                L"Accept-Ranges", L"Age", L"ETag", L"Location",
                L"Proxy-Authenticate", L"Retry-After", L"Server",
                L"Set-Cookie", L"Vary", L"WWW-Authenticate", L"Access-Control-Allow-Origin",
                L"Access-Control-Allow-Methods", L"Access-Control-Allow-Headers",
                L"Access-Control-Max-Age", L"Access-Control-Request-Method",
                L"Access-Control-Request-Headers"
            };
            if (i < sizeof(headerNames) / sizeof(headerNames[0])) {
                request.headers[headerNames[i]] = value;
            }
        }
    }

    // 解析 Body
    if (pRequest->Flags & HTTP_REQUEST_HAS_ENTITY_BODY) {
        // 读取内容（简化处理）
        // 实际应用中需要循环读取所有分段
    }

    // 路由匹配和处理
    RequestHandler handler = nullptr;
    std::map<std::string, std::string> params;

    {
        std::lock_guard<std::mutex> lock(m_routesMutex);
        if (MatchRoute(request.method, request.path, handler, params)) {
            request.params = params;
            handler(request, response);
        } else if (m_catchAllHandler) {
            m_catchAllHandler(request, response);
        } else {
            response.statusCode = 404;
            response.body = "{\"error\":\"Not Found\",\"path\":\"" + request.path + "\"}";
        }
    }

    // 添加 CORS 头
    response.headers["Access-Control-Allow-Origin"] = "*";
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, X-API-Key, Authorization";

    // 发送响应
    HTTP_RESPONSE httpResponse;
    memset(&httpResponse, 0, sizeof(httpResponse));

    // 状态码
    httpResponse.StatusCode = (USHORT)response.statusCode;
    httpResponse.Reason = (response.statusCode == 200) ? "OK" :
                          (response.statusCode == 404) ? "Not Found" :
                          (response.statusCode == 401) ? "Unauthorized" :
                          (response.statusCode == 500) ? "Internal Server Error" : "Error";
    httpResponse.ReasonLength = (USHORT)strlen(httpResponse.Reason);

    // 头部
    std::vector<HTTP_RESPONSE_HEADER> respHeaders;
    respHeaders.push_back({ L"Content-Type", CW2A(response.contentType.c_str()) });
    respHeaders.push_back({ L"Server", "T3000-AgentBridge" });

    for (auto& kv : response.headers) {
        respHeaders.push_back({ CW2A(kv.first.c_str()), CW2A(kv.second.c_str()) });
    }

    httpResponse.Headers.Count = (USHORT)respHeaders.size();
    httpResponse.Headers.pKnownHeaders = NULL;
    httpResponse.Headers.pUnknownHeaders = respHeaders.data();

    // Body
    HTTP_DATA_CHUNK dataChunk;
    if (!response.body.empty()) {
        HTTP_SET_NULL_BUFFER_LENGTH(httpResponse);
        DataChunkInit(&dataChunk, response.body.c_str(), (ULONG)response.body.size());
        httpResponse.pEntityChunks = &dataChunk;
        httpResponse.EntityChunkCount = 1;
    }

    ULONG bytesSent = 0;
    HttpSendHttpResponse(m_httpServerHandle, pRequest->RequestId, 0, &httpResponse, NULL, &bytesSent, NULL, 0, NULL, NULL);

    return true;
}

void CAgentHttpServer::RegisterRoute(const std::string& method, const std::string& path, RequestHandler handler) {
    std::lock_guard<std::mutex> lock(m_routesMutex);

    Route route;
    route.method = method;
    route.path = NormalizePath(path);
    route.handler = handler;

    // 计算路径参数数量（{xxx} 标记）
    route.paramCount = 0;
    std::string p = route.path;
    size_t pos = 0;
    while ((pos = p.find('{', pos)) != std::string::npos) {
        route.paramCount++;
        pos = p.find('}', pos);
        if (pos != std::string::npos) pos++;
    }

    m_routes.push_back(route);
}

void CAgentHttpServer::RegisterCatchAll(RequestHandler handler) {
    std::lock_guard<std::mutex> lock(m_routesMutex);
    m_catchAllHandler = handler;
}

bool CAgentHttpServer::MatchRoute(const std::string& method, const std::string& path,
                                   RequestHandler& handler, std::map<std::string, std::string>& params) {
    std::string normalizedPath = NormalizePath(path);
    std::vector<std::string> pathParts;

    // 分割路径
    std::stringstream ss(normalizedPath);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) pathParts.push_back(part);
    }

    std::lock_guard<std::mutex> lock(m_routesMutex);

    for (auto& route : m_routes) {
        // 检查方法
        if (route.method != "*" && route.method != method) continue;

        // 分割路由路径
        std::vector<std::string> routeParts;
        std::stringstream rss(route.path);
        std::string rpart;
        while (std::getline(rss, rpart, '/')) {
            if (!rpart.empty()) routeParts.push_back(rpart);
        }

        if (routeParts.size() != pathParts.size()) continue;

        bool match = true;
        params.clear();
        int paramIdx = 0;

        for (size_t i = 0; i < routeParts.size(); i++) {
            if (routeParts[i].size() >= 2 && routeParts[i][0] == '{' && routeParts[i].back() == '}') {
                // 路径参数
                std::string paramName = routeParts[i].substr(1, routeParts[i].size() - 2);
                params[paramName] = pathParts[i];
                paramIdx++;
            } else if (routeParts[i] != pathParts[i]) {
                match = false;
                break;
            }
        }

        if (match) {
            handler = route.handler;
            return true;
        }
    }

    return false;
}

std::string CAgentHttpServer::NormalizePath(const std::string& path) {
    std::string result = path;
    // 移除查询字符串
    size_t qpos = result.find('?');
    if (qpos != std::string::npos) {
        result = result.substr(0, qpos);
    }
    // 确保以 / 开头
    if (!result.empty() && result[0] != '/') {
        result = "/" + result;
    }
    // 移除末尾 /（除了根路径）
    if (result.size() > 1 && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

// ============================================
// CAgentJson 实现
// ============================================
std::string CAgentJson::EscapeJson(const std::string& s) const {
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

std::string CAgentJson::ToString() const {
    std::string result;
    switch (m_type) {
        case TYPE_NULL:
            result = "null";
            break;
        case TYPE_BOOL:
            result = m_boolVal ? "true" : "false";
            break;
        case TYPE_INT:
            result = std::to_string(m_intVal);
            break;
        case TYPE_DOUBLE: {
            std::ostringstream oss;
            oss.precision(6);
            oss << m_doubleVal;
            result = oss.str();
            break;
        }
        case TYPE_STRING:
            result = "\"" + EscapeJson(m_strVal) + "\"";
            break;
        case TYPE_OBJECT: {
            result = "{";
            for (size_t i = 0; i < m_objectKeys.size(); i++) {
                if (i > 0) result += ",";
                result += "\"" + EscapeJson(m_objectKeys[i]) + "\":" + m_objectVals[i].ToString();
            }
            result += "}";
            break;
        }
        case TYPE_ARRAY: {
            result = "[";
            for (size_t i = 0; i < m_arrayVals.size(); i++) {
                if (i > 0) result += ",";
                result += m_arrayVals[i].ToString();
            }
            result += "]";
            break;
        }
    }
    return result;
}

// 便捷函数：构建 JSON 响应
static CAgentJson JsonSuccess(const CAgentJson& data = CAgentJson()) {
    CAgentJson result;
    result.SetObject();
    result.Add("success", CAgentJson(true));
    result.Add("data", data);
    return result;
}

static CAgentJson JsonError(const std::string& message, int code = 400) {
    CAgentJson result;
    result.SetObject();
    result.Add("success", CAgentJson(false));
    result.Add("error", CAgentJson(message));
    result.Add("code", CAgentJson(code));
    return result;
}
