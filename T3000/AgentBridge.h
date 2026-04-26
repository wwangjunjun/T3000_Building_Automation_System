// AgentBridge.h: 智能体对接模块头文件
// T3000 AI Agent Integration Bridge
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <sstream>

// 前置声明
class CAgentHttpServer;
class CAgentWebSocketServer;
class CAgentMcpServer;

// ============================================
// 配置常量
// ============================================
#define AGENTBRIDGE_VERSION         _T("1.0.0")
#define AGENTBRIDGE_DEFAULT_PORT    8080
#define AGENTBRIDGE_MAX_CONNECTIONS 64
#define AGENTBRIDGE_API_PREFIX      _T("/api/v1")

// API 密钥配置
#define AGENTBRIDGE_API_KEY_HEADER  _T("X-API-Key")
#define AGENTBRIDGE_DEFAULT_API_KEY _T("t3000-agent-key-change-me")

// ============================================
// 数据类型定义
// ============================================

// 设备类型枚举
enum AgentDeviceType {
    DEVICE_TYPE_THERMOSTAT = 0,     // 温控器
    DEVICE_TYPE_IO_MODULE,          // IO 模块
    DEVICE_TYPE_SENSOR,             // 传感器
    DEVICE_TYPE_ACTUATOR,           // 执行器
    DEVICE_TYPE_CONTROLLER,         // 控制器
    DEVICE_TYPE_LIGHTING,           // 照明控制器
    DEVICE_TYPE_AIR_QUALITY,        // 空气质量
    DEVICE_TYPE_UNKNOWN
};

// 输入/输出类型
enum AgentPointType {
    POINT_TYPE_ANALOG_INPUT = 0,
    POINT_TYPE_ANALOG_OUTPUT,
    POINT_TYPE_BINARY_INPUT,
    POINT_TYPE_BINARY_OUTPUT,
    POINT_TYPE_MULTI_STATE_INPUT,
    POINT_TYPE_MULTI_STATE_OUTPUT
};

// 告警严重级别
enum AgentAlarmSeverity {
    ALARM_SEVERITY_NORMAL = 0,
    ALARM_SEVERITY_WARNING,
    ALARM_SEVERITY_CRITICAL,
    ALARM_SEVERITY_EMERGENCY
};

// ============================================
// JSON 辅助类（轻量级，不依赖外部库）
// ============================================
class CAgentJson {
public:
    CAgentJson() : m_type(TYPE_NULL) {}
    CAgentJson(bool val) : m_type(TYPE_BOOL), m_boolVal(val) {}
    CAgentJson(int val) : m_type(TYPE_INT), m_intVal(val) {}
    CAgentJson(double val) : m_type(TYPE_DOUBLE), m_doubleVal(val) {}
    CAgentJson(const std::string& val) : m_type(TYPE_STRING), m_strVal(val) {}
    CAgentJson(const CString& val) : m_type(TYPE_STRING), m_strVal(CStringA(val)) {}

    enum Type { TYPE_NULL, TYPE_BOOL, TYPE_INT, TYPE_DOUBLE, TYPE_STRING, TYPE_OBJECT, TYPE_ARRAY };

    void SetObject() { m_type = TYPE_OBJECT; }
    void SetArray() { m_type = TYPE_ARRAY; }

    void Add(const std::string& key, const CAgentJson& val) {
        m_objectKeys.push_back(key);
        m_objectVals.push_back(val);
    }
    void Add(const CAgentJson& val) {
        m_arrayVals.push_back(val);
    }

    std::string ToString() const;

private:
    Type m_type;
    bool m_boolVal;
    int m_intVal;
    double m_doubleVal;
    std::string m_strVal;
    std::vector<std::string> m_objectKeys;
    std::vector<CAgentJson> m_objectVals;
    std::vector<CAgentJson> m_arrayVals;

    std::string EscapeJson(const std::string& s) const;
};

// ============================================
// HTTP 请求/响应结构
// ============================================
struct AgentHttpRequest {
    std::string method;       // GET, POST, PUT, DELETE
    std::string path;         // /api/v1/devices
    std::string query;        // ?page=1&limit=10
    std::map<std::string, std::string> headers;
    std::string body;
    std::map<std::string, std::string> params;  // URL 路径参数
};

struct AgentHttpResponse {
    int statusCode;           // 200, 404, 500
    std::string contentType;  // application/json
    std::string body;
    std::map<std::string, std::string> headers;

    AgentHttpResponse() : statusCode(200), contentType("application/json") {}
};

// ============================================
// 设备信息结构
// ============================================
struct AgentDeviceInfo {
    int deviceId;
    std::string name;
    std::string model;
    std::string serialNumber;
    std::string firmwareVersion;
    std::string hardwareVersion;
    AgentDeviceType deviceType;
    bool online;
    std::string ipAddress;
    int bacnetId;
    int modbusId;
};

// ============================================
// 输入/输出点信息
// ============================================
struct AgentPointInfo {
    int pointId;
    std::string name;
    AgentPointType pointType;
    double currentValue;
    std::string unit;
    bool writable;
    int deviceId;
};

// ============================================
// 告警信息
// ============================================
struct AgentAlarmInfo {
    int alarmId;
    int deviceId;
    std::string deviceName;
    std::string description;
    AgentAlarmSeverity severity;
    bool acknowledged;
    std::string timestamp;
};

// ============================================
// 事件类型
// ============================================
enum AgentEventType {
    EVENT_DEVICE_ONLINE = 0,
    EVENT_DEVICE_OFFLINE,
    EVENT_INPUT_CHANGED,
    EVENT_OUTPUT_CHANGED,
    EVENT_ALARM_ACTIVE,
    EVENT_ALARM_CLEARED,
    EVENT_ALARM_ACKNOWLEDGED,
    EVENT_SYSTEM_SCAN_COMPLETE,
    EVENT_SCHEDULE_CHANGED,
    EVENT_RULE_TRIGGERED  // 新增：规则触发事件
};

// ============================================
// 告警规则
// ============================================
enum AgentRuleOperator {
    RULE_OP_GREATER = 0,      // >
    RULE_OP_LESS,             // <
    RULE_OP_EQUAL,            // ==
    RULE_OP_NOT_EQUAL,        // !=
    RULE_OP_GREATER_EQUAL,    // >=
    RULE_OP_LESS_EQUAL        // <=
};

struct AgentAlarmRule {
    int ruleId;
    std::string name;
    std::string description;
    int deviceId;
    int pointId;
    AgentRuleOperator operator_;
    double threshold;
    bool enabled;
    std::string action;  // "notify", "webhook", "log"
    std::string webhookUrl;  // 如果 action 是 webhook
    std::string lastTriggered;
    int triggerCount;
};

// ============================================
// Webhook 配置
// ============================================
struct AgentWebhookConfig {
    int webhookId;
    std::string name;
    std::string url;
    std::string secret;  // HMAC 签名密钥
    std::vector<std::string> events;  // 订阅的事件类型
    bool enabled;
    int successCount;
    int failCount;
    std::string lastSent;
    std::string lastError;
};

// ============================================
// 智能体桥接器主类
// ============================================
class CAgentBridge {
public:
    CAgentBridge();
    ~CAgentBridge();

    // 初始化和启动
    bool Initialize();
    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }

    // 配置
    void SetPort(int port) { m_port = port; }
    int GetPort() const { return m_port; }
    void SetApiKey(const CString& key) { m_apiKey = key; }
    CString GetApiKey() const { return m_apiKey; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    void AddAllowedIp(const CString& ip) { m_allowedIps.push_back(ip); }
    bool IsIpAllowed(const CString& ip) const;

    // 认证
    bool Authenticate(const AgentHttpRequest& request, AgentHttpResponse& response);

    // 获取子模块
    CAgentHttpServer* GetHttpServer() { return m_httpServer; }
    CAgentWebSocketServer* GetWebSocketServer() { return m_webSocketServer; }
    CAgentMcpServer* GetMcpServer() { return m_mcpServer; }

    // 事件推送
    void PushEvent(AgentEventType type, const CAgentJson& data);
    void PushDeviceEvent(bool online, const AgentDeviceInfo& device);
    void PushAlarmEvent(const AgentAlarmInfo& alarm, bool active);
    void PushPointEvent(int deviceId, const AgentPointInfo& point);

    // 设备数据访问（与 T3000 实际数据对接）
    std::vector<AgentDeviceInfo> GetDevices();
    AgentDeviceInfo GetDeviceById(int deviceId);
    std::vector<AgentPointInfo> GetDeviceInputs(int deviceId);
    std::vector<AgentPointInfo> GetDeviceOutputs(int deviceId);
    bool WriteOutput(int outputId, double value);
    double ReadInput(int inputId);
    std::vector<AgentAlarmInfo> GetAlarms();
    bool AcknowledgeAlarm(int alarmId);

    // 系统操作
    bool StartScan(bool fullScan);
    CAgentJson GetSystemInfo();

    // 告警规则管理
    bool AddAlarmRule(const AgentAlarmRule& rule);
    bool RemoveAlarmRule(int ruleId);
    bool UpdateAlarmRule(const AgentAlarmRule& rule);
    std::vector<AgentAlarmRule> GetAlarmRules();
    bool EvaluateRules();
    bool SendWebhook(const std::string& url, const CAgentJson& data);

    // Webhook 管理
    bool AddWebhook(const AgentWebhookConfig& webhook);
    bool RemoveWebhook(int webhookId);
    bool UpdateWebhook(const AgentWebhookConfig& webhook);
    std::vector<AgentWebhookConfig> GetWebhooks();
    bool SendEventToWebhooks(AgentEventType eventType, const CAgentJson& data);

    // 日志
    void Log(const CString& message);
    void LogError(const CString& message);

    // 获取 MainFrm 指针（用于访问 m_product）
    void SetMainFrame(void* pMainFrame) { m_pMainFrame = pMainFrame; }
    void* GetMainFrame() const { return m_pMainFrame; }

private:
    int m_port;
    CString m_apiKey;
    bool m_enabled;
    std::atomic<bool> m_running;
    std::vector<CString> m_allowedIps;
    void* m_pMainFrame;  // MainFrm 指针

    CAgentHttpServer* m_httpServer;
    CAgentWebSocketServer* m_webSocketServer;
    CAgentMcpServer* m_mcpServer;

    std::mutex m_eventMutex;
    std::vector<std::function<void(const CAgentJson&)>> m_eventListeners;

    // 告警规则
    std::mutex m_rulesMutex;
    std::vector<AgentAlarmRule> m_alarmRules;
    int m_nextRuleId;

    // Webhook
    std::mutex m_webhooksMutex;
    std::vector<AgentWebhookConfig> m_webhooks;
    int m_nextWebhookId;

    void RegisterApiRoutes();
    void OnRequest(const AgentHttpRequest& request, AgentHttpResponse& response);
};

// 全局访问
extern CAgentBridge g_AgentBridge;
