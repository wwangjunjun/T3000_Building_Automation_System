// AgentBridge.cpp: 智能体对接模块主实现
// 与 T3000 实际数据层对接
#include "stdafx.h"
#include "AgentBridge.h"
#include "AgentHttpServer.h"
#include "AgentWebSocketServer.h"
#include "AgentMcpServer.h"
#include <ctime>

// T3000 头文件（用于数据层对接）
#include "MainFrm.h"
#include "global_function.h"
#include "global_variable_extern.h"
#include "global_define.h"

// ============================================
// 全局实例
// ============================================
CAgentBridge g_AgentBridge;

// ============================================
// CAgentBridge 实现
// ============================================

CAgentBridge::CAgentBridge()
    : m_port(AGENTBRIDGE_DEFAULT_PORT)
    , m_apiKey(AGENTBRIDGE_DEFAULT_API_KEY)
    , m_enabled(false)
    , m_running(false)
    , m_pMainFrame(nullptr)
    , m_httpServer(nullptr)
    , m_webSocketServer(nullptr)
    , m_mcpServer(nullptr)
    , m_nextRuleId(1)
    , m_nextWebhookId(1)
{
}

CAgentBridge::~CAgentBridge() {
    Stop();
}

bool CAgentBridge::Initialize() {
    // 创建子模块
    m_httpServer = new CAgentHttpServer();
    m_webSocketServer = new CAgentWebSocketServer();
    m_mcpServer = new CAgentMcpServer();

    // 初始化
    if (!m_httpServer->Initialize(m_port, this)) {
        LogError(_T("AgentBridge: HTTP server initialization failed"));
        return false;
    }

    if (!m_webSocketServer->Initialize(m_port + 1, this)) {
        LogError(_T("AgentBridge: WebSocket server initialization failed"));
        return false;
    }

    if (!m_mcpServer->Initialize(m_port + 2, this)) {
        LogError(_T("AgentBridge: MCP server initialization failed"));
        return false;
    }

    // 注册 API 路由
    RegisterApiRoutes();

    Log(_T("AgentBridge initialized successfully"));
    return true;
}

bool CAgentBridge::Start() {
    if (m_running) return true;
    if (!m_enabled) return false;

    bool success = true;

    if (!m_httpServer->Start()) {
        LogError(_T("AgentBridge: Failed to start HTTP server"));
        success = false;
    }

    if (!m_webSocketServer->Start()) {
        LogError(_T("AgentBridge: Failed to start WebSocket server"));
        success = false;
    }

    if (!m_mcpServer->Start()) {
        LogError(_T("AgentBridge: Failed to start MCP server"));
        success = false;
    }

    if (success) {
        m_running = true;
        CString msg;
        msg.Format(_T("AgentBridge started (HTTP:%d, WS:%d, MCP:%d)"),
                   m_port, m_port + 1, m_port + 2);
        Log(msg);
    }

    return success;
}

void CAgentBridge::Stop() {
    if (!m_running) return;

    m_running = false;

    if (m_httpServer) m_httpServer->Stop();
    if (m_webSocketServer) m_webSocketServer->Stop();
    if (m_mcpServer) m_mcpServer->Stop();

    Log(_T("AgentBridge stopped"));
}

// ============================================
// 认证中间件
// ============================================
bool CAgentBridge::Authenticate(const AgentHttpRequest& request, AgentHttpResponse& response) {
    // 如果未配置 API Key，则跳过认证
    if (m_apiKey.IsEmpty()) return true;

    // 检查 X-API-Key 头部
    auto it = request.headers.find(AGENTBRIDGE_API_KEY_HEADER);
    if (it == request.headers.end()) {
        // 也检查 Authorization 头部
        it = request.headers.find("Authorization");
        if (it != request.headers.end()) {
            std::string authVal = it->second;
            // 支持 "Bearer <key>" 格式
            if (authVal.substr(0, 7) == "Bearer ") {
                authVal = authVal.substr(7);
            }
            if (authVal == CStringA(m_apiKey)) return true;
        }
        response.statusCode = 401;
        response.body = JsonError("Missing API key. Use X-API-Key header or Authorization: Bearer <key>").ToString();
        return false;
    }

    if (it->second != CStringA(m_apiKey)) {
        response.statusCode = 401;
        response.body = JsonError("Invalid API key").ToString();
        return false;
    }

    return true;
}

bool CAgentBridge::IsIpAllowed(const CString& ip) const {
    if (m_allowedIps.empty()) return true;  // 空列表表示允许所有

    for (auto& allowedIp : m_allowedIps) {
        if (allowedIp == ip) return true;
        // 支持通配符
        if (allowedIp.Find(L"*") >= 0) {
            CString pattern = allowedIp;
            pattern.Replace(L"*", L"");
            if (ip.Find(pattern) == 0) return true;
        }
    }
    return false;
}

// ============================================
// API 路由注册
// ============================================
void CAgentBridge::RegisterApiRoutes() {
    if (!m_httpServer) return;

    // 认证中间件 - 所有 API 请求先经过认证
    auto authMiddleware = [this](const AgentHttpRequest& req, AgentHttpResponse& resp) -> bool {
        // OPTIONS 预检请求跳过认证
        if (req.method == "OPTIONS") return true;
        // 根路径跳过认证
        if (req.path == "/") return true;
        // 执行认证
        return Authenticate(req, resp);
    };

    // ============================
    // 设备管理 API
    // ============================

    // GET /api/v1/devices
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/devices",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            auto devices = GetDevices();

            CAgentJson deviceList;
            deviceList.SetArray();

            for (auto& dev : devices) {
                CAgentJson devJson;
                devJson.SetObject();
                devJson.Add("id", CAgentJson(dev.deviceId));
                devJson.Add("name", CAgentJson(dev.name));
                devJson.Add("model", CAgentJson(dev.model));
                devJson.Add("serial_number", CAgentJson(dev.serialNumber));
                devJson.Add("firmware_version", CAgentJson(dev.firmwareVersion));
                devJson.Add("online", CAgentJson(dev.online));
                devJson.Add("bacnet_id", CAgentJson(dev.bacnetId));
                devJson.Add("modbus_id", CAgentJson(dev.modbusId));
                deviceList.Add(devJson);
            }

            resp.body = JsonSuccess(deviceList).ToString();
        });

    // GET /api/v1/devices/{id}
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/devices/{id}",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            int deviceId = 0;
            try {
                deviceId = std::stoi(req.params.at("id"));
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid device ID").ToString();
                return;
            }

            auto device = GetDeviceById(deviceId);
            auto inputs = GetDeviceInputs(deviceId);
            auto outputs = GetDeviceOutputs(deviceId);

            CAgentJson result;
            result.SetObject();

            CAgentJson devJson;
            devJson.SetObject();
            devJson.Add("id", CAgentJson(device.deviceId));
            devJson.Add("name", CAgentJson(device.name));
            devJson.Add("model", CAgentJson(device.model));
            devJson.Add("serial_number", CAgentJson(device.serialNumber));
            devJson.Add("firmware_version", CAgentJson(device.firmwareVersion));
            devJson.Add("hardware_version", CAgentJson(device.hardwareVersion));
            devJson.Add("online", CAgentJson(device.online));
            devJson.Add("bacnet_id", CAgentJson(device.bacnetId));
            devJson.Add("modbus_id", CAgentJson(device.modbusId));

            // 添加输入点
            CAgentJson inputList;
            inputList.SetArray();
            for (auto& inp : inputs) {
                CAgentJson inpJson;
                inpJson.SetObject();
                inpJson.Add("id", CAgentJson(inp.pointId));
                inpJson.Add("name", CAgentJson(inp.name));
                inpJson.Add("type", CAgentJson((int)inp.pointType));
                inpJson.Add("value", CAgentJson(inp.currentValue));
                inpJson.Add("unit", CAgentJson(inp.unit));
                inputList.Add(inpJson);
            }
            devJson.Add("inputs", inputList);

            // 添加输出点
            CAgentJson outputList;
            outputList.SetArray();
            for (auto& out : outputs) {
                CAgentJson outJson;
                outJson.SetObject();
                outJson.Add("id", CAgentJson(out.pointId));
                outJson.Add("name", CAgentJson(out.name));
                outJson.Add("type", CAgentJson((int)out.pointType));
                outJson.Add("value", CAgentJson(out.currentValue));
                outJson.Add("writable", CAgentJson(out.writable));
                outputList.Add(outJson);
            }
            devJson.Add("outputs", outputList);

            resp.body = JsonSuccess(devJson).ToString();
        });

    // ============================
    // 设备控制 API
    // ============================

    // POST /api/v1/outputs/{id}/write
    m_httpServer->RegisterRoute("POST", AGENTBRIDGE_API_PREFIX "/outputs/{id}/write",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            int outputId = 0;
            try {
                outputId = std::stoi(req.params.at("id"));
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid output ID").ToString();
                return;
            }

            // 解析请求体中的值
            double value = 0;
            try {
                size_t valPos = req.body.find("\"value\"");
                if (valPos != std::string::npos) {
                    size_t colonPos = req.body.find(':', valPos);
                    if (colonPos != std::string::npos) {
                        std::string valStr = req.body.substr(colonPos + 1);
                        size_t start = valStr.find_first_of("-0123456789.");
                        if (start != std::string::npos) {
                            size_t end = valStr.find_first_of(",} \t\r\n", start);
                            if (end != std::string::npos) {
                                valStr = valStr.substr(start, end - start);
                            } else {
                                valStr = valStr.substr(start);
                            }
                            value = std::stod(valStr);
                        }
                    }
                }
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid value in request body").ToString();
                return;
            }

            bool success = WriteOutput(outputId, value);

            CAgentJson result;
            result.SetObject();
            result.Add("success", CAgentJson(success));
            result.Add("output_id", CAgentJson(outputId));
            result.Add("value", CAgentJson(value));

            resp.body = result.ToString();

            // 推送事件
            if (success) {
                AgentPointInfo point;
                point.pointId = outputId;
                point.currentValue = value;
                PushPointEvent(0, point);
            }
        });

    // GET /api/v1/inputs/{id}
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/inputs/{id}",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            int inputId = 0;
            try {
                inputId = std::stoi(req.params.at("id"));
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid input ID").ToString();
                return;
            }

            double value = ReadInput(inputId);

            CAgentJson result;
            result.SetObject();
            result.Add("input_id", CAgentJson(inputId));
            result.Add("value", CAgentJson(value));

            time_t now = time(NULL);
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&now));
            result.Add("timestamp", CAgentJson(std::string(timeStr)));

            resp.body = JsonSuccess(result).ToString();
        });

    // ============================
    // 告警 API
    // ============================

    // GET /api/v1/alarms
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/alarms",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            auto alarms = GetAlarms();

            CAgentJson alarmList;
            alarmList.SetArray();

            for (auto& alarm : alarms) {
                CAgentJson alarmJson;
                alarmJson.SetObject();
                alarmJson.Add("id", CAgentJson(alarm.alarmId));
                alarmJson.Add("device_id", CAgentJson(alarm.deviceId));
                alarmJson.Add("device_name", CAgentJson(alarm.deviceName));
                alarmJson.Add("description", CAgentJson(alarm.description));
                alarmJson.Add("severity", CAgentJson((int)alarm.severity));
                alarmJson.Add("acknowledged", CAgentJson(alarm.acknowledged));
                alarmJson.Add("timestamp", CAgentJson(alarm.timestamp));
                alarmList.Add(alarmJson);
            }

            resp.body = JsonSuccess(alarmList).ToString();
        });

    // POST /api/v1/alarms/{id}/ack
    m_httpServer->RegisterRoute("POST", AGENTBRIDGE_API_PREFIX "/alarms/{id}/ack",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            int alarmId = 0;
            try {
                alarmId = std::stoi(req.params.at("id"));
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid alarm ID").ToString();
                return;
            }

            bool success = AcknowledgeAlarm(alarmId);

            CAgentJson result;
            result.SetObject();
            result.Add("success", CAgentJson(success));
            result.Add("alarm_id", CAgentJson(alarmId));

            resp.body = result.ToString();
        });

    // ============================
    // 系统 API
    // ============================

    // GET /api/v1/system/info
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/system/info",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;
            resp.body = JsonSuccess(GetSystemInfo()).ToString();
        });

    // GET /api/v1/system/status
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/system/status",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            CAgentJson status;
            status.SetObject();
            status.Add("running", CAgentJson(m_running));
            status.Add("http_port", CAgentJson(m_port));
            status.Add("websocket_port", CAgentJson(m_port + 1));
            status.Add("mcp_port", CAgentJson(m_port + 2));
            status.Add("websocket_clients", CAgentJson(m_webSocketServer ? m_webSocketServer->GetClientCount() : 0));
            status.Add("api_key_configured", CAgentJson(!m_apiKey.IsEmpty()));

            resp.body = JsonSuccess(status).ToString();
        });

    // POST /api/v1/system/scan
    m_httpServer->RegisterRoute("POST", AGENTBRIDGE_API_PREFIX "/system/scan",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            bool fullScan = false;
            if (req.body.find("\"full_scan\":true") != std::string::npos ||
                req.body.find("\"full_scan\": true") != std::string::npos) {
                fullScan = true;
            }

            bool success = StartScan(fullScan);

            CAgentJson result;
            result.SetObject();
            result.Add("success", CAgentJson(success));
            result.Add("scan_type", CAgentJson(fullScan ? "full" : "quick"));

            resp.body = result.ToString();
        });

    // ============================
    // 批量操作 API
    // ============================

    // POST /api/v1/batch/read
    m_httpServer->RegisterRoute("POST", AGENTBRIDGE_API_PREFIX "/batch/read",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            // 解析请求体中的输入 ID 列表
            std::vector<int> inputIds;
            try {
                size_t pos = req.body.find("\"input_ids\"");
                if (pos != std::string::npos) {
                    size_t bracketStart = req.body.find('[', pos);
                    size_t bracketEnd = req.body.find(']', bracketStart);
                    if (bracketStart != std::string::npos && bracketEnd != std::string::npos) {
                        std::string arrayStr = req.body.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
                        // 解析逗号分隔的数字
                        std::stringstream ss(arrayStr);
                        std::string token;
                        while (std::getline(ss, token, ',')) {
                            // 去除引号和空格
                            size_t start = token.find_first_of("-0123456789");
                            if (start != std::string::npos) {
                                size_t end = token.find_first_not_of("-0123456789", start);
                                if (end != std::string::npos) {
                                    inputIds.push_back(std::stoi(token.substr(start, end - start)));
                                } else {
                                    inputIds.push_back(std::stoi(token.substr(start)));
                                }
                            }
                        }
                    }
                }
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid input_ids format").ToString();
                return;
            }

            // 批量读取输入值
            CAgentJson results;
            results.SetArray();
            time_t now = time(NULL);
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&now));

            for (int inputId : inputIds) {
                double value = ReadInput(inputId);
                CAgentJson item;
                item.SetObject();
                item.Add("input_id", CAgentJson(inputId));
                item.Add("value", CAgentJson(value));
                item.Add("timestamp", CAgentJson(std::string(timeStr)));
                results.Add(item);
            }

            resp.body = JsonSuccess(results).ToString();
        });

    // POST /api/v1/batch/write
    m_httpServer->RegisterRoute("POST", AGENTBRIDGE_API_PREFIX "/batch/write",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            // 解析请求体中的写入列表
            std::vector<std::pair<int, double>> writes;
            try {
                size_t pos = req.body.find("\"writes\"");
                if (pos != std::string::npos) {
                    size_t bracketStart = req.body.find('[', pos);
                    size_t bracketEnd = req.body.find(']', bracketStart);
                    if (bracketStart != std::string::npos && bracketEnd != std::string::npos) {
                        std::string arrayStr = req.body.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
                        // 解析每个写入对象
                        size_t objStart = 0;
                        while ((objStart = arrayStr.find('{', objStart)) != std::string::npos) {
                            size_t objEnd = arrayStr.find('}', objStart);
                            if (objEnd == std::string::npos) break;
                            
                            std::string objStr = arrayStr.substr(objStart, objEnd - objStart + 1);
                            
                            // 解析 output_id
                            int outputId = 0;
                            size_t idPos = objStr.find("\"output_id\"");
                            if (idPos != std::string::npos) {
                                size_t colonPos = objStr.find(':', idPos);
                                if (colonPos != std::string::npos) {
                                    std::string idStr = objStr.substr(colonPos + 1);
                                    size_t start = idStr.find_first_of("-0123456789");
                                    if (start != std::string::npos) {
                                        size_t end = idStr.find_first_not_of("-0123456789", start);
                                        outputId = std::stoi(idStr.substr(start, end != std::string::npos ? end - start : std::string::npos));
                                    }
                                }
                            }
                            
                            // 解析 value
                            double value = 0;
                            size_t valPos = objStr.find("\"value\"");
                            if (valPos != std::string::npos) {
                                size_t colonPos = objStr.find(':', valPos);
                                if (colonPos != std::string::npos) {
                                    std::string valStr = objStr.substr(colonPos + 1);
                                    size_t start = valStr.find_first_of("-0123456789.");
                                    if (start != std::string::npos) {
                                        size_t end = valStr.find_first_of(",} \\t\\r\\n", start);
                                        if (end != std::string::npos) {
                                            value = std::stod(valStr.substr(start, end - start));
                                        } else {
                                            value = std::stod(valStr.substr(start));
                                        }
                                    }
                                }
                            }
                            
                            writes.push_back({outputId, value});
                            objStart = objEnd + 1;
                        }
                    }
                }
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid writes format").ToString();
                return;
            }

            // 批量写入输出值
            CAgentJson results;
            results.SetArray();
            int successCount = 0;
            int failCount = 0;

            for (auto& write : writes) {
                bool success = WriteOutput(write.first, write.second);
                if (success) successCount++; else failCount++;
                
                CAgentJson item;
                item.SetObject();
                item.Add("output_id", CAgentJson(write.first));
                item.Add("value", CAgentJson(write.second));
                item.Add("success", CAgentJson(success));
                results.Add(item);
                
                // 推送事件
                if (success) {
                    AgentPointInfo point;
                    point.pointId = write.first;
                    point.currentValue = write.second;
                    PushPointEvent(0, point);
                }
            }

            CAgentJson response;
            response.SetObject();
            response.Add("results", results);
            response.Add("success_count", CAgentJson(successCount));
            response.Add("fail_count", CAgentJson(failCount));
            response.Add("total", CAgentJson((int)writes.size()));

            resp.body = JsonSuccess(response).ToString();
        });

    // ============================
    // 数据导出 API
    // ============================

    // GET /api/v1/export/devices
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/export/devices",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            // 检查是否请求 CSV 格式
            bool csvFormat = req.query.find("format=csv") != std::string::npos;

            auto devices = GetDevices();

            if (csvFormat) {
                // CSV 格式
                resp.contentType = "text/csv";
                std::string csv = "ID,Name,Model,Serial Number,Firmware Version,Online,BACnet ID,Modbus ID\n";
                for (auto& dev : devices) {
                    csv += std::to_string(dev.deviceId) + ",";
                    csv += dev.name + ",";
                    csv += dev.model + ",";
                    csv += dev.serialNumber + ",";
                    csv += dev.firmwareVersion + ",";
                    csv += (dev.online ? "是" : "否") + ",";
                    csv += std::to_string(dev.bacnetId) + ",";
                    csv += std::to_string(dev.modbusId) + "\n";
                }
                resp.body = csv;
            } else {
                // JSON 格式
                CAgentJson deviceList;
                deviceList.SetArray();
                for (auto& dev : devices) {
                    CAgentJson devJson;
                    devJson.SetObject();
                    devJson.Add("id", CAgentJson(dev.deviceId));
                    devJson.Add("name", CAgentJson(dev.name));
                    devJson.Add("model", CAgentJson(dev.model));
                    devJson.Add("serial_number", CAgentJson(dev.serialNumber));
                    devJson.Add("firmware_version", CAgentJson(dev.firmwareVersion));
                    devJson.Add("online", CAgentJson(dev.online));
                    devJson.Add("bacnet_id", CAgentJson(dev.bacnetId));
                    devJson.Add("modbus_id", CAgentJson(dev.modbusId));
                    deviceList.Add(devJson);
                }
                resp.body = JsonSuccess(deviceList).ToString();
            }
        });

    // GET /api/v1/export/alarms
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/export/alarms",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            bool csvFormat = req.query.find("format=csv") != std::string::npos;
            auto alarms = GetAlarms();

            if (csvFormat) {
                resp.contentType = "text/csv";
                std::string csv = "ID,Device ID,Device Name,Description,Severity,Acknowledged,Timestamp\n";
                for (auto& alarm : alarms) {
                    csv += std::to_string(alarm.alarmId) + ",";
                    csv += std::to_string(alarm.deviceId) + ",";
                    csv += alarm.deviceName + ",";
                    csv += alarm.description + ",";
                    csv += std::to_string((int)alarm.severity) + ",";
                    csv += (alarm.acknowledged ? "是" : "否") + ",";
                    csv += alarm.timestamp + "\n";
                }
                resp.body = csv;
            } else {
                CAgentJson alarmList;
                alarmList.SetArray();
                for (auto& alarm : alarms) {
                    CAgentJson alarmJson;
                    alarmJson.SetObject();
                    alarmJson.Add("id", CAgentJson(alarm.alarmId));
                    alarmJson.Add("device_id", CAgentJson(alarm.deviceId));
                    alarmJson.Add("device_name", CAgentJson(alarm.deviceName));
                    alarmJson.Add("description", CAgentJson(alarm.description));
                    alarmJson.Add("severity", CAgentJson((int)alarm.severity));
                    alarmJson.Add("acknowledged", CAgentJson(alarm.acknowledged));
                    alarmJson.Add("timestamp", CAgentJson(alarm.timestamp));
                    alarmList.Add(alarmJson);
                }
                resp.body = JsonSuccess(alarmList).ToString();
            }
        });

    // ============================
    // 告警规则 API
    // ============================

    // GET /api/v1/rules
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/rules",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            auto rules = GetAlarmRules();
            CAgentJson ruleList;
            ruleList.SetArray();

            for (auto& rule : rules) {
                CAgentJson ruleJson;
                ruleJson.SetObject();
                ruleJson.Add("id", CAgentJson(rule.ruleId));
                ruleJson.Add("name", CAgentJson(rule.name));
                ruleJson.Add("description", CAgentJson(rule.description));
                ruleJson.Add("device_id", CAgentJson(rule.deviceId));
                ruleJson.Add("point_id", CAgentJson(rule.pointId));
                ruleJson.Add("operator", CAgentJson((int)rule.operator_));
                ruleJson.Add("threshold", CAgentJson(rule.threshold));
                ruleJson.Add("enabled", CAgentJson(rule.enabled));
                ruleJson.Add("action", CAgentJson(rule.action));
                ruleJson.Add("webhook_url", CAgentJson(rule.webhookUrl));
                ruleJson.Add("last_triggered", CAgentJson(rule.lastTriggered));
                ruleJson.Add("trigger_count", CAgentJson(rule.triggerCount));
                ruleList.Add(ruleJson);
            }

            resp.body = JsonSuccess(ruleList).ToString();
        });

    // POST /api/v1/rules
    m_httpServer->RegisterRoute("POST", AGENTBRIDGE_API_PREFIX "/rules",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            AgentAlarmRule rule;
            rule.enabled = true;
            rule.triggerCount = 0;

            // 解析请求体
            try {
                // 解析 name
                size_t pos = req.body.find("\"name\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        size_t quoteStart = req.body.find('"', colonPos + 1);
                        size_t quoteEnd = req.body.find('"', quoteStart + 1);
                        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                            rule.name = req.body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        }
                    }
                }

                // 解析 device_id
                pos = req.body.find("\"device_id\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        std::string valStr = req.body.substr(colonPos + 1);
                        size_t start = valStr.find_first_of("-0123456789");
                        if (start != std::string::npos) {
                            size_t end = valStr.find_first_not_of("-0123456789", start);
                            rule.deviceId = std::stoi(valStr.substr(start, end != std::string::npos ? end - start : std::string::npos));
                        }
                    }
                }

                // 解析 point_id
                pos = req.body.find("\"point_id\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        std::string valStr = req.body.substr(colonPos + 1);
                        size_t start = valStr.find_first_of("-0123456789");
                        if (start != std::string::npos) {
                            size_t end = valStr.find_first_not_of("-0123456789", start);
                            rule.pointId = std::stoi(valStr.substr(start, end != std::string::npos ? end - start : std::string::npos));
                        }
                    }
                }

                // 解析 threshold
                pos = req.body.find("\"threshold\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        std::string valStr = req.body.substr(colonPos + 1);
                        size_t start = valStr.find_first_of("-0123456789.");
                        if (start != std::string::npos) {
                            size_t end = valStr.find_first_of(",} \\t\\r\\n", start);
                            if (end != std::string::npos) {
                                rule.threshold = std::stod(valStr.substr(start, end - start));
                            } else {
                                rule.threshold = std::stod(valStr.substr(start));
                            }
                        }
                    }
                }

                // 解析 operator
                pos = req.body.find("\"operator\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        std::string valStr = req.body.substr(colonPos + 1);
                        size_t start = valStr.find_first_of("012345");
                        if (start != std::string::npos) {
                            rule.operator_ = (AgentRuleOperator)std::stoi(valStr.substr(start, 1));
                        }
                    }
                }

                // 解析 action
                pos = req.body.find("\"action\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        size_t quoteStart = req.body.find('"', colonPos + 1);
                        size_t quoteEnd = req.body.find('"', quoteStart + 1);
                        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                            rule.action = req.body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        }
                    }
                }

                // 解析 webhook_url
                pos = req.body.find("\"webhook_url\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        size_t quoteStart = req.body.find('"', colonPos + 1);
                        size_t quoteEnd = req.body.find('"', quoteStart + 1);
                        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                            rule.webhookUrl = req.body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        }
                    }
                }
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid rule format").ToString();
                return;
            }

            bool success = AddAlarmRule(rule);
            auto rules = GetAlarmRules();
            AgentAlarmRule addedRule = rules.back();

            CAgentJson result;
            result.SetObject();
            result.Add("success", CAgentJson(success));
            result.Add("rule_id", CAgentJson(addedRule.ruleId));

            resp.body = result.ToString();
        });

    // DELETE /api/v1/rules/{id}
    m_httpServer->RegisterRoute("DELETE", AGENTBRIDGE_API_PREFIX "/rules/{id}",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            int ruleId = 0;
            try {
                ruleId = std::stoi(req.params.at("id"));
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid rule ID").ToString();
                return;
            }

            bool success = RemoveAlarmRule(ruleId);

            CAgentJson result;
            result.SetObject();
            result.Add("success", CAgentJson(success));
            result.Add("rule_id", CAgentJson(ruleId));

            resp.body = result.ToString();
        });

    // POST /api/v1/rules/evaluate
    m_httpServer->RegisterRoute("POST", AGENTBRIDGE_API_PREFIX "/rules/evaluate",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            bool triggered = EvaluateRules();

            CAgentJson result;
            result.SetObject();
            result.Add("triggered", CAgentJson(triggered));
            result.Add("timestamp", CAgentJson(std::string([]{
                time_t now = time(NULL);
                char timeStr[64];
                strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&now));
                return std::string(timeStr);
            }())));

            resp.body = JsonSuccess(result).ToString();
        });

    // ============================
    // Webhook API
    // ============================

    // GET /api/v1/webhooks
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/webhooks",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            auto webhooks = GetWebhooks();
            CAgentJson webhookList;
            webhookList.SetArray();

            for (auto& webhook : webhooks) {
                CAgentJson webhookJson;
                webhookJson.SetObject();
                webhookJson.Add("id", CAgentJson(webhook.webhookId));
                webhookJson.Add("name", CAgentJson(webhook.name));
                webhookJson.Add("url", CAgentJson(webhook.url));
                webhookJson.Add("enabled", CAgentJson(webhook.enabled));
                webhookJson.Add("success_count", CAgentJson(webhook.successCount));
                webhookJson.Add("fail_count", CAgentJson(webhook.failCount));
                webhookJson.Add("last_sent", CAgentJson(webhook.lastSent));
                webhookJson.Add("last_error", CAgentJson(webhook.lastError));
                
                // 添加事件列表
                CAgentJson events;
                events.SetArray();
                for (auto& event : webhook.events) {
                    events.Add(CAgentJson(event));
                }
                webhookJson.Add("events", events);
                
                webhookList.Add(webhookJson);
            }

            resp.body = JsonSuccess(webhookList).ToString();
        });

    // POST /api/v1/webhooks
    m_httpServer->RegisterRoute("POST", AGENTBRIDGE_API_PREFIX "/webhooks",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            AgentWebhookConfig webhook;
            webhook.enabled = true;
            webhook.successCount = 0;
            webhook.failCount = 0;

            // 解析请求体（简化实现）
            try {
                size_t pos = req.body.find("\"name\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        size_t quoteStart = req.body.find('"', colonPos + 1);
                        size_t quoteEnd = req.body.find('"', quoteStart + 1);
                        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                            webhook.name = req.body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        }
                    }
                }

                pos = req.body.find("\"url\"");
                if (pos != std::string::npos) {
                    size_t colonPos = req.body.find(':', pos);
                    if (colonPos != std::string::npos) {
                        size_t quoteStart = req.body.find('"', colonPos + 1);
                        size_t quoteEnd = req.body.find('"', quoteStart + 1);
                        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                            webhook.url = req.body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        }
                    }
                }
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid webhook format").ToString();
                return;
            }

            bool success = AddWebhook(webhook);
            auto webhooks = GetWebhooks();
            AgentWebhookConfig addedWebhook = webhooks.back();

            CAgentJson result;
            result.SetObject();
            result.Add("success", CAgentJson(success));
            result.Add("webhook_id", CAgentJson(addedWebhook.webhookId));

            resp.body = result.ToString();
        });

    // DELETE /api/v1/webhooks/{id}
    m_httpServer->RegisterRoute("DELETE", AGENTBRIDGE_API_PREFIX "/webhooks/{id}",
        [this, authMiddleware](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            if (!authMiddleware(req, resp)) return;

            int webhookId = 0;
            try {
                webhookId = std::stoi(req.params.at("id"));
            } catch (...) {
                resp.statusCode = 400;
                resp.body = JsonError("Invalid webhook ID").ToString();
                return;
            }

            bool success = RemoveWebhook(webhookId);

            CAgentJson result;
            result.SetObject();
            result.Add("success", CAgentJson(success));
            result.Add("webhook_id", CAgentJson(webhookId));

            resp.body = result.ToString();
        });

    // ============================
    // Prometheus 监控指标
    // ============================

    // GET /metrics
    m_httpServer->RegisterRoute("GET", "/metrics",
        [this](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            // 返回 Prometheus 格式的指标
            std::string metrics;
            
            // 设备数量
            int deviceCount = 0;
            if (m_pMainFrame) {
                CMainFrame* pFrame = static_cast<CMainFrame*>(m_pMainFrame);
                deviceCount = (int)pFrame->m_product.size();
            }
            metrics += "# HELP t3000_devices_total Total number of devices\n";
            metrics += "# TYPE t3000_devices_total gauge\n";
            metrics += "t3000_devices_total " + std::to_string(deviceCount) + "\n\n";
            
            // 在线设备数量
            int onlineCount = 0;
            if (m_pMainFrame) {
                CMainFrame* pFrame = static_cast<CMainFrame*>(m_pMainFrame);
                for (size_t i = 0; i < pFrame->m_product.size(); i++) {
                    if (pFrame->m_product.at(i).status) onlineCount++;
                }
            }
            metrics += "# HELP t3000_devices_online Number of online devices\n";
            metrics += "# TYPE t3000_devices_online gauge\n";
            metrics += "t3000_devices_online " + std::to_string(onlineCount) + "\n\n";
            
            // 告警规则数量
            {
                std::lock_guard<std::mutex> lock(m_rulesMutex);
                int enabledRules = 0;
                for (auto& rule : m_alarmRules) {
                    if (rule.enabled) enabledRules++;
                }
                metrics += "# HELP t3000_rules_total Total number of alarm rules\n";
                metrics += "# TYPE t3000_rules_total gauge\n";
                metrics += "t3000_rules_total " + std::to_string((int)m_alarmRules.size()) + "\n\n";
                metrics += "# HELP t3000_rules_enabled Number of enabled alarm rules\n";
                metrics += "# TYPE t3000_rules_enabled gauge\n";
                metrics += "t3000_rules_enabled " + std::to_string(enabledRules) + "\n\n";
            }
            
            // Webhook 数量
            {
                std::lock_guard<std::mutex> lock(m_webhooksMutex);
                int enabledWebhooks = 0;
                for (auto& webhook : m_webhooks) {
                    if (webhook.enabled) enabledWebhooks++;
                }
                metrics += "# HELP t3000_webhooks_total Total number of webhooks\n";
                metrics += "# TYPE t3000_webhooks_total gauge\n";
                metrics += "t3000_webhooks_total " + std::to_string((int)m_webhooks.size()) + "\n\n";
                metrics += "# HELP t3000_webhooks_enabled Number of enabled webhooks\n";
                metrics += "# TYPE t3000_webhooks_enabled gauge\n";
                metrics += "t3000_webhooks_enabled " + std::to_string(enabledWebhooks) + "\n\n";
            }
            
            // WebSocket 连接数
            int wsClients = m_webSocketServer ? m_webSocketServer->GetClientCount() : 0;
            metrics += "# HELP t3000_websocket_connections Number of WebSocket connections\n";
            metrics += "# TYPE t3000_websocket_connections gauge\n";
            metrics += "t3000_websocket_connections " + std::to_string(wsClients) + "\n\n";
            
            // AgentBridge 版本
            metrics += "# HELP t3000_info AgentBridge version info\n";
            metrics += "# TYPE t3000_info gauge\n";
            metrics += "t3000_info{version=\"" + std::string(AGENTBRIDGE_VERSION) + "\"} 1\n";
            
            resp.contentType = "text/plain";
            resp.body = metrics;
        });

    // ============================
    // API 文档
    // ============================

    // GET /api/v1/docs
    m_httpServer->RegisterRoute("GET", AGENTBRIDGE_API_PREFIX "/docs",
        [this](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            // 返回 OpenAPI 3.0 文档
            std::string openapi = R"({
  "openapi": "3.0.0",
  "info": {
    "title": "T3000 AgentBridge API",
    "version": "1.0.0",
    "description": "T3000 建筑自动化系统智能体对接 API"
  },
  "servers": [
    {
      "url": "http://localhost:8080",
      "description": "本地服务器"
    }
  ],
  "paths": {
    "/api/v1/devices": {
      "get": {
        "summary": "获取设备列表",
        "tags": ["设备管理"],
        "security": [{"ApiKeyAuth": []}],
        "responses": {
          "200": {
            "description": "成功返回设备列表"
          }
        }
      }
    },
    "/api/v1/devices/{id}": {
      "get": {
        "summary": "获取设备详情",
        "tags": ["设备管理"],
        "security": [{"ApiKeyAuth": []}],
        "parameters": [
          {
            "name": "id",
            "in": "path",
            "required": true,
            "schema": {"type": "integer"}
          }
        ],
        "responses": {
          "200": {
            "description": "成功返回设备详情"
          }
        }
      }
    },
    "/api/v1/batch/read": {
      "post": {
        "summary": "批量读取输入值",
        "tags": ["批量操作"],
        "security": [{"ApiKeyAuth": []}],
        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "input_ids": {
                    "type": "array",
                    "items": {"type": "integer"}
                  }
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "成功返回批量读取结果"
          }
        }
      }
    },
    "/api/v1/batch/write": {
      "post": {
        "summary": "批量写入输出值",
        "tags": ["批量操作"],
        "security": [{"ApiKeyAuth": []}],
        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "writes": {
                    "type": "array",
                    "items": {
                      "type": "object",
                      "properties": {
                        "output_id": {"type": "integer"},
                        "value": {"type": "number"}
                      }
                    }
                  }
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "成功返回批量写入结果"
          }
        }
      }
    },
    "/api/v1/export/devices": {
      "get": {
        "summary": "导出设备数据",
        "tags": ["数据导出"],
        "security": [{"ApiKeyAuth": []}],
        "parameters": [
          {
            "name": "format",
            "in": "query",
            "schema": {"type": "string", "enum": ["json", "csv"]}
          }
        ],
        "responses": {
          "200": {
            "description": "成功导出数据"
          }
        }
      }
    },
    "/api/v1/export/alarms": {
      "get": {
        "summary": "导出告警数据",
        "tags": ["数据导出"],
        "security": [{"ApiKeyAuth": []}],
        "parameters": [
          {
            "name": "format",
            "in": "query",
            "schema": {"type": "string", "enum": ["json", "csv"]}
          }
        ],
        "responses": {
          "200": {
            "description": "成功导出数据"
          }
        }
      }
    },
    "/api/v1/rules": {
      "get": {
        "summary": "获取告警规则列表",
        "tags": ["告警规则"],
        "security": [{"ApiKeyAuth": []}],
        "responses": {
          "200": {
            "description": "成功返回规则列表"
          }
        }
      },
      "post": {
        "summary": "创建告警规则",
        "tags": ["告警规则"],
        "security": [{"ApiKeyAuth": []}],
        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "name": {"type": "string"},
                  "device_id": {"type": "integer"},
                  "point_id": {"type": "integer"},
                  "operator": {"type": "integer", "description": "0:>, 1:<, 2:==, 3:!=, 4:>=, 5:<="},
                  "threshold": {"type": "number"},
                  "action": {"type": "string", "enum": ["notify", "webhook", "log"]},
                  "webhook_url": {"type": "string"}
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "成功创建规则"
          }
        }
      }
    },
    "/api/v1/webhooks": {
      "get": {
        "summary": "获取 Webhook 列表",
        "tags": ["Webhook"],
        "security": [{"ApiKeyAuth": []}],
        "responses": {
          "200": {
            "description": "成功返回 Webhook 列表"
          }
        }
      },
      "post": {
        "summary": "创建 Webhook",
        "tags": ["Webhook"],
        "security": [{"ApiKeyAuth": []}],
        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "properties": {
                  "name": {"type": "string"},
                  "url": {"type": "string"},
                  "events": {
                    "type": "array",
                    "items": {"type": "string"}
                  }
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "成功创建 Webhook"
          }
        }
      }
    }
  },
  "components": {
    "securitySchemes": {
      "ApiKeyAuth": {
        "type": "apiKey",
        "in": "header",
        "name": "X-API-Key"
      }
    }
  }
})";
            resp.body = openapi;
        });

    // ============================
    // OPTIONS 预检请求
    // ============================
    m_httpServer->RegisterRoute("OPTIONS", "*",
        [this](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            resp.statusCode = 204;
            resp.body = "";
        });

    // ============================
    // 根路径
    // ============================
    m_httpServer->RegisterRoute("GET", "/",
        [this](const AgentHttpRequest& req, AgentHttpResponse& resp) {
            CAgentJson info;
            info.SetObject();
            info.Add("name", CAgentJson("T3000 AgentBridge"));
            info.Add("version", CAgentJson(AGENTBRIDGE_VERSION));
            info.Add("api_version", CAgentJson("v1"));
            info.Add("endpoints", CAgentJson());

            CAgentJson endpoints;
            endpoints.SetObject();
            endpoints.Add("devices", CAgentJson("GET  " AGENTBRIDGE_API_PREFIX "/devices"));
            endpoints.Add("device_detail", CAgentJson("GET  " AGENTBRIDGE_API_PREFIX "/devices/{id}"));
            endpoints.Add("write_output", CAgentJson("POST " AGENTBRIDGE_API_PREFIX "/outputs/{id}/write"));
            endpoints.Add("read_input", CAgentJson("GET  " AGENTBRIDGE_API_PREFIX "/inputs/{id}"));
            endpoints.Add("alarms", CAgentJson("GET  " AGENTBRIDGE_API_PREFIX "/alarms"));
            endpoints.Add("ack_alarm", CAgentJson("POST " AGENTBRIDGE_API_PREFIX "/alarms/{id}/ack"));
            endpoints.Add("system_info", CAgentJson("GET  " AGENTBRIDGE_API_PREFIX "/system/info"));
            endpoints.Add("system_scan", CAgentJson("POST " AGENTBRIDGE_API_PREFIX "/system/scan"));
            endpoints.Add("batch_read", CAgentJson("POST " AGENTBRIDGE_API_PREFIX "/batch/read"));
            endpoints.Add("batch_write", CAgentJson("POST " AGENTBRIDGE_API_PREFIX "/batch/write"));
            endpoints.Add("export_devices", CAgentJson("GET  " AGENTBRIDGE_API_PREFIX "/export/devices"));
            endpoints.Add("export_alarms", CAgentJson("GET  " AGENTBRIDGE_API_PREFIX "/export/alarms"));
            endpoints.Add("websocket", CAgentJson("WS   ws://localhost:" + std::to_string(m_port + 1) + "/"));
            endpoints.Add("mcp", CAgentJson("TCP  localhost:" + std::to_string(m_port + 2)));
            info.Add("endpoints", endpoints);

            resp.body = info.ToString();
        });

    Log(_T("AgentBridge API routes registered"));
}

// ============================================
// 事件推送
// ============================================
void CAgentBridge::PushEvent(AgentEventType type, const CAgentJson& data) {
    CAgentJson event;
    event.SetObject();
    event.Add("type", CAgentJson(std::to_string((int)type)));
    event.Add("event", CAgentJson(
        type == EVENT_DEVICE_ONLINE ? "device.online" :
        type == EVENT_DEVICE_OFFLINE ? "device.offline" :
        type == EVENT_INPUT_CHANGED ? "input.changed" :
        type == EVENT_OUTPUT_CHANGED ? "output.changed" :
        type == EVENT_ALARM_ACTIVE ? "alarm.active" :
        type == EVENT_ALARM_CLEARED ? "alarm.cleared" :
        type == EVENT_ALARM_ACKNOWLEDGED ? "alarm.acknowledged" :
        type == EVENT_SYSTEM_SCAN_COMPLETE ? "scan.complete" :
        type == EVENT_SCHEDULE_CHANGED ? "schedule.changed" : "unknown"
    ));

    time_t now = time(NULL);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    event.Add("timestamp", CAgentJson(std::string(timeStr)));
    event.Add("data", data);

    if (m_webSocketServer) {
        m_webSocketServer->BroadcastJson(event);
    }
}

void CAgentBridge::PushDeviceEvent(bool online, const AgentDeviceInfo& device) {
    CAgentJson data;
    data.SetObject();
    data.Add("id", CAgentJson(device.deviceId));
    data.Add("name", CAgentJson(device.name));
    data.Add("online", CAgentJson(online));
    PushEvent(online ? EVENT_DEVICE_ONLINE : EVENT_DEVICE_OFFLINE, data);
}

void CAgentBridge::PushAlarmEvent(const AgentAlarmInfo& alarm, bool active) {
    CAgentJson data;
    data.SetObject();
    data.Add("id", CAgentJson(alarm.alarmId));
    data.Add("device_id", CAgentJson(alarm.deviceId));
    data.Add("description", CAgentJson(alarm.description));
    data.Add("severity", CAgentJson((int)alarm.severity));
    PushEvent(active ? EVENT_ALARM_ACTIVE : EVENT_ALARM_CLEARED, data);
}

void CAgentBridge::PushPointEvent(int deviceId, const AgentPointInfo& point) {
    CAgentJson data;
    data.SetObject();
    data.Add("device_id", CAgentJson(deviceId));
    data.Add("point_id", CAgentJson(point.pointId));
    data.Add("name", CAgentJson(point.name));
    data.Add("value", CAgentJson(point.currentValue));
    PushEvent(EVENT_OUTPUT_CHANGED, data);
}

// ============================================
// 设备数据访问（与 T3000 实际数据对接）
// ============================================

std::vector<AgentDeviceInfo> CAgentBridge::GetDevices() {
    std::vector<AgentDeviceInfo> devices;

    // 通过 MainFrm 的 m_product 获取设备列表
    if (m_pMainFrame) {
        CMainFrame* pFrame = static_cast<CMainFrame*>(m_pMainFrame);
        for (size_t i = 0; i < pFrame->m_product.size(); i++) {
            tree_product& prod = pFrame->m_product.at(i);

            AgentDeviceInfo dev;
            dev.deviceId = (int)prod.serial_number;
            dev.name = CStringA(prod.NameShowOnTree);
            dev.model = CStringA(ProdModelToString(prod.product_class_id));
            dev.serialNumber = std::to_string(prod.serial_number);
            dev.firmwareVersion = std::to_string(prod.software_version);
            dev.hardwareVersion = std::to_string(prod.hardware_version);
            dev.online = prod.status;
            dev.bacnetId = (int)prod.object_instance;
            dev.modbusId = prod.ModbusID;

            // 根据 product_class_id 判断设备类型
            switch (prod.product_class_id) {
                case 1: dev.deviceType = DEVICE_TYPE_THERMOSTAT; break;
                case 2: dev.deviceType = DEVICE_TYPE_LIGHTING; break;
                case 3: dev.deviceType = DEVICE_TYPE_IO_MODULE; break;
                default: dev.deviceType = DEVICE_TYPE_UNKNOWN; break;
            }

            devices.push_back(dev);
        }
    }

    return devices;
}

AgentDeviceInfo CAgentBridge::GetDeviceById(int deviceId) {
    AgentDeviceInfo device;
    device.deviceId = deviceId;
    device.online = false;

    if (m_pMainFrame) {
        CMainFrame* pFrame = static_cast<CMainFrame*>(m_pMainFrame);
        for (size_t i = 0; i < pFrame->m_product.size(); i++) {
            tree_product& prod = pFrame->m_product.at(i);
            if ((int)prod.serial_number == deviceId) {
                device.name = CStringA(prod.NameShowOnTree);
                device.model = CStringA(ProdModelToString(prod.product_class_id));
                device.serialNumber = std::to_string(prod.serial_number);
                device.firmwareVersion = std::to_string(prod.software_version);
                device.hardwareVersion = std::to_string(prod.hardware_version);
                device.online = prod.status;
                device.bacnetId = (int)prod.object_instance;
                device.modbusId = prod.ModbusID;
                break;
            }
        }
    }

    return device;
}

std::vector<AgentPointInfo> CAgentBridge::GetDeviceInputs(int deviceId) {
    std::vector<AgentPointInfo> inputs;

    if (m_pMainFrame) {
        CMainFrame* pFrame = static_cast<CMainFrame*>(m_pMainFrame);
        for (size_t i = 0; i < pFrame->m_product.size(); i++) {
            tree_product& prod = pFrame->m_product.at(i);
            if ((int)prod.serial_number == deviceId) {
                // 从 sub_io_info 获取输入点
                // tree_sub_io 包含输入输出点信息
                for (int type = 0; type < TREE_MAX_TYPE; type++) {
                    tree_sub_io& subIo = prod.sub_io_info[type];
                    // 根据类型判断是输入还是输出
                    if (type == TYPE_INPUT) {
                        for (size_t j = 0; j < subIo.m_InputList.size(); j++) {
                            AgentPointInfo point;
                            point.pointId = (int)j;
                            point.name = CStringA(subIo.m_InputList.at(j).m_strName);
                            point.pointType = POINT_TYPE_ANALOG_INPUT;
                            point.currentValue = subIo.m_InputList.at(j).m_fValue;
                            point.writable = false;
                            point.deviceId = deviceId;
                            inputs.push_back(point);
                        }
                    }
                }
                break;
            }
        }
    }

    return inputs;
}

std::vector<AgentPointInfo> CAgentBridge::GetDeviceOutputs(int deviceId) {
    std::vector<AgentPointInfo> outputs;

    if (m_pMainFrame) {
        CMainFrame* pFrame = static_cast<CMainFrame*>(m_pMainFrame);
        for (size_t i = 0; i < pFrame->m_product.size(); i++) {
            tree_product& prod = pFrame->m_product.at(i);
            if ((int)prod.serial_number == deviceId) {
                for (int type = 0; type < TREE_MAX_TYPE; type++) {
                    tree_sub_io& subIo = prod.sub_io_info[type];
                    if (type == TYPE_OUTPUT) {
                        for (size_t j = 0; j < subIo.m_OutputList.size(); j++) {
                            AgentPointInfo point;
                            point.pointId = (int)j;
                            point.name = CStringA(subIo.m_OutputList.at(j).m_strName);
                            point.pointType = POINT_TYPE_ANALOG_OUTPUT;
                            point.currentValue = subIo.m_OutputList.at(j).m_fValue;
                            point.writable = true;
                            point.deviceId = deviceId;
                            outputs.push_back(point);
                        }
                    }
                }
                break;
            }
        }
    }

    return outputs;
}

bool CAgentBridge::WriteOutput(int outputId, double value) {
    // 通过 T3000 的写函数写入输出值
    // 使用 write_one 或 Write_Multi 函数
    unsigned char deviceVar = (unsigned char)(outputId & 0xFF);
    unsigned short address = (unsigned short)(outputId >> 8);
    short val = (short)value;

    int result = write_one(deviceVar, address, val, 3);
    return (result == WRITE_ONE_SUCCESS_LIST);
}

double CAgentBridge::ReadInput(int inputId) {
    // 通过 T3000 的读函数读取输入值
    unsigned char deviceVar = (unsigned char)(inputId & 0xFF);
    unsigned short address = (unsigned short)(inputId >> 8);

    int result = read_one(deviceVar, address, 3);
    return (double)result;
}

std::vector<AgentAlarmInfo> CAgentBridge::GetAlarms() {
    std::vector<AgentAlarmInfo> alarms;

    // 通过 T3000 全局变量 m_alarmlog_data 获取告警列表
    // Alarm_point 结构体定义在 CM5/ud_str.h 中
    // m_alarmlog_data 在 global_variable.h 中声明为 vector<Alarm_point>
    
    // 注意：需要在 Windows 编译环境中包含以下头文件：
    // #include "CM5/ud_str.h"
    // #include "global_variable_extern.h"
    
    // 以下为对接代码框架（需在 Windows 环境中编译验证）：
    /*
    extern vector<Alarm_point> m_alarmlog_data;
    extern vector<vector<Alarm_point>> g_alarmlog_data;
    
    // 从全局告警数据获取
    for (size_t i = 0; i < m_alarmlog_data.size(); i++) {
        Alarm_point& ap = m_alarmlog_data.at(i);
        
        // 只显示活动告警（alarm=1 且未删除）
        if (ap.alarm == 1 && ap.ddelete == 0) {
            AgentAlarmInfo alarm;
            alarm.alarmId = (int)i;
            alarm.deviceId = ap.alarm_panel;  // 面板号作为设备ID
            alarm.deviceName = "";  // TODO: 从设备列表获取面板名称
            alarm.description = std::string((char*)ap.alarm_message);
            
            // 严重级别映射
            switch (ap.level) {
                case 0: alarm.severity = ALARM_SEVERITY_NORMAL; break;
                case 1: alarm.severity = ALARM_SEVERITY_WARNING; break;
                case 2: alarm.severity = ALARM_SEVERITY_CRITICAL; break;
                default: alarm.severity = ALARM_SEVERITY_EMERGENCY; break;
            }
            
            alarm.acknowledged = (ap.acknowledged == 1);
            
            // 时间戳转换
            time_t alarm_time = (time_t)ap.alarm_time;
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&alarm_time));
            alarm.timestamp = std::string(timeStr);
            
            alarms.push_back(alarm);
        }
    }
    
    // 也检查多设备告警数据
    for (size_t devIdx = 0; devIdx < g_alarmlog_data.size(); devIdx++) {
        for (size_t i = 0; i < g_alarmlog_data.at(devIdx).size(); i++) {
            Alarm_point& ap = g_alarmlog_data.at(devIdx).at(i);
            if (ap.alarm == 1 && ap.ddelete == 0) {
                // 类似处理...
            }
        }
    }
    */
   
    // 临时返回空列表（等待 Windows 编译环境验证）
    return alarms;
}

bool CAgentBridge::AcknowledgeAlarm(int alarmId) {
    // 通过 T3000 的告警确认机制确认告警
    // 参考 BacnetAlarmLog.cpp 中的 OnClickListAlarmlog 实现
    
    /*
    extern vector<Alarm_point> m_alarmlog_data;
    extern Alarm_point m_temp_alarmlog_data[BAC_ALARMLOG_COUNT];
    extern HWND m_alarmlog_dlg_hwnd;
    extern unsigned int g_bac_instance;
    
    if (alarmId < 0 || alarmId >= (int)m_alarmlog_data.size()) {
        return false;
    }
    
    // 保存原始数据用于比较
    memcpy_s(&m_temp_alarmlog_data[alarmId], sizeof(Alarm_point),
             &m_alarmlog_data.at(alarmId), sizeof(Alarm_point));
    
    // 设置确认标志
    m_alarmlog_data.at(alarmId).acknowledged = 1;
    
    // 检查是否有变化
    int cmp_ret = memcmp(&m_temp_alarmlog_data[alarmId], 
                         &m_alarmlog_data.at(alarmId), sizeof(Alarm_point));
    if (cmp_ret != 0) {
        // 通过 T3000 消息机制写入告警确认
        CString temp_task_info;
        temp_task_info.Format(_T("Write Alarmlog Ack Item %d"), alarmId + 1);
        Post_Write_Message(g_bac_instance, WRITEALARM_T3000, 
                          alarmId, alarmId, sizeof(Alarm_point),
                          m_alarmlog_dlg_hwnd, temp_task_info, alarmId, ALARMLOG_ACK);
    }
    
    return true;
    */
   
    LogError(_T("AcknowledgeAlarm: 需要 Windows 编译环境验证"));
    return false;
}

bool CAgentBridge::StartScan(bool fullScan) {
    if (!m_pMainFrame) return false;

    CMainFrame* pFrame = static_cast<CMainFrame*>(m_pMainFrame);

    // 设置扫描参数
    g_Scanfully = fullScan ? TRUE : FALSE;
    g_ScanSecurity = !fullScan ? TRUE : FALSE;
    g_bCancelScan = FALSE;

    // 清空之前的设备列表
    pFrame->m_product.clear();
    pFrame->m_bScanALL = TRUE;
    pFrame->m_bScanFinished = FALSE;

    // 启动扫描线程
    // pFrame->m_pThreadScan = AfxBeginThread(ScanThreadProc, pFrame);

    Log(fullScan ? _T("AgentBridge: Full scan started") : _T("AgentBridge: Quick scan started"));
    return true;
}

CAgentJson CAgentBridge::GetSystemInfo() {
    CAgentJson info;
    info.SetObject();
    info.Add("name", CAgentJson("T3000 Building Automation System"));
    info.Add("agentbridge_version", CAgentJson(AGENTBRIDGE_VERSION));
    info.Add("http_port", CAgentJson(m_port));
    info.Add("websocket_port", CAgentJson(m_port + 1));
    info.Add("mcp_port", CAgentJson(m_port + 2));
    info.Add("api_key_configured", CAgentJson(!m_apiKey.IsEmpty()));

    // 设备数量
    int deviceCount = 0;
    if (m_pMainFrame) {
        CMainFrame* pFrame = static_cast<CMainFrame*>(m_pMainFrame);
        deviceCount = (int)pFrame->m_product.size();
    }
    info.Add("device_count", CAgentJson(deviceCount));

    time_t now = time(NULL);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    info.Add("timestamp", CAgentJson(std::string(timeStr)));

    return info;
}

void CAgentBridge::Log(const CString& message) {
    OutputDebugString(_T("[AgentBridge] ") + message + _T("\n"));
}

void CAgentBridge::LogError(const CString& message) {
    OutputDebugString(_T("[AgentBridge ERROR] ") + message + _T("\n"));
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

// 便捷函数
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

// ============================================
// 告警规则引擎实现
// ============================================

bool CAgentBridge::AddAlarmRule(const AgentAlarmRule& rule) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    AgentAlarmRule newRule = rule;
    newRule.ruleId = m_nextRuleId++;
    newRule.triggerCount = 0;
    m_alarmRules.push_back(newRule);
    
    CString msg;
    msg.Format(_T("Added alarm rule: %s (ID: %d)"), 
               CString(newRule.name.c_str()), newRule.ruleId);
    Log(msg);
    return true;
}

bool CAgentBridge::RemoveAlarmRule(int ruleId) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    auto it = std::find_if(m_alarmRules.begin(), m_alarmRules.end(),
        [ruleId](const AgentAlarmRule& r) { return r.ruleId == ruleId; });
    
    if (it != m_alarmRules.end()) {
        m_alarmRules.erase(it);
        Log(_T("Removed alarm rule"));
        return true;
    }
    return false;
}

bool CAgentBridge::UpdateAlarmRule(const AgentAlarmRule& rule) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    auto it = std::find_if(m_alarmRules.begin(), m_alarmRules.end(),
        [rule](const AgentAlarmRule& r) { return r.ruleId == rule.ruleId; });
    
    if (it != m_alarmRules.end()) {
        *it = rule;
        Log(_T("Updated alarm rule"));
        return true;
    }
    return false;
}

std::vector<AgentAlarmRule> CAgentBridge::GetAlarmRules() {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    return m_alarmRules;
}

bool CAgentBridge::EvaluateRules() {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    bool anyTriggered = false;
    
    time_t now = time(NULL);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    
    for (auto& rule : m_alarmRules) {
        if (!rule.enabled) continue;
        
        // 读取输入值
        double currentValue = ReadInput(rule.pointId);
        bool conditionMet = false;
        
        switch (rule.operator_) {
            case RULE_OP_GREATER:
                conditionMet = (currentValue > rule.threshold);
                break;
            case RULE_OP_LESS:
                conditionMet = (currentValue < rule.threshold);
                break;
            case RULE_OP_EQUAL:
                conditionMet = (currentValue == rule.threshold);
                break;
            case RULE_OP_NOT_EQUAL:
                conditionMet = (currentValue != rule.threshold);
                break;
            case RULE_OP_GREATER_EQUAL:
                conditionMet = (currentValue >= rule.threshold);
                break;
            case RULE_OP_LESS_EQUAL:
                conditionMet = (currentValue <= rule.threshold);
                break;
        }
        
        if (conditionMet) {
            anyTriggered = true;
            rule.triggerCount++;
            rule.lastTriggered = timeStr;
            
            // 创建触发事件
            CAgentJson eventData;
            eventData.SetObject();
            eventData.Add("rule_id", CAgentJson(rule.ruleId));
            eventData.Add("rule_name", CAgentJson(rule.name));
            eventData.Add("device_id", CAgentJson(rule.deviceId));
            eventData.Add("point_id", CAgentJson(rule.pointId));
            eventData.Add("current_value", CAgentJson(currentValue));
            eventData.Add("threshold", CAgentJson(rule.threshold));
            eventData.Add("trigger_count", CAgentJson(rule.triggerCount));
            eventData.Add("timestamp", CAgentJson(timeStr));
            
            PushEvent(EVENT_RULE_TRIGGERED, eventData);
            
            // 执行动作
            if (rule.action == "log") {
                CString msg;
                msg.Format(_T("Rule triggered: %s (value: %.2f, threshold: %.2f)"),
                          CString(rule.name.c_str()), currentValue, rule.threshold);
                Log(msg);
            } else if (rule.action == "webhook" && !rule.webhookUrl.empty()) {
                SendWebhook(rule.webhookUrl, eventData);
            }
        }
    }
    
    return anyTriggered;
}

bool CAgentBridge::SendWebhook(const std::string& url, const CAgentJson& data) {
    // 使用 HTTP POST 发送 Webhook
    // 注意：这里需要实现 HTTP 客户端功能
    // 简化实现：记录日志
    Log(_T("Webhook would be sent to: ") + CString(url.c_str()));
    return true;
}

// ============================================
// Webhook 管理实现
// ============================================

bool CAgentBridge::AddWebhook(const AgentWebhookConfig& webhook) {
    std::lock_guard<std::mutex> lock(m_webhooksMutex);
    AgentWebhookConfig newWebhook = webhook;
    newWebhook.webhookId = m_nextWebhookId++;
    newWebhook.successCount = 0;
    newWebhook.failCount = 0;
    m_webhooks.push_back(newWebhook);
    
    CString msg;
    msg.Format(_T("Added webhook: %s (ID: %d)"), 
               CString(newWebhook.name.c_str()), newWebhook.webhookId);
    Log(msg);
    return true;
}

bool CAgentBridge::RemoveWebhook(int webhookId) {
    std::lock_guard<std::mutex> lock(m_webhooksMutex);
    auto it = std::find_if(m_webhooks.begin(), m_webhooks.end(),
        [webhookId](const AgentWebhookConfig& w) { return w.webhookId == webhookId; });
    
    if (it != m_webhooks.end()) {
        m_webhooks.erase(it);
        Log(_T("Removed webhook"));
        return true;
    }
    return false;
}

bool CAgentBridge::UpdateWebhook(const AgentWebhookConfig& webhook) {
    std::lock_guard<std::mutex> lock(m_webhooksMutex);
    auto it = std::find_if(m_webhooks.begin(), m_webhooks.end(),
        [webhook](const AgentWebhookConfig& w) { return w.webhookId == webhook.webhookId; });
    
    if (it != m_webhooks.end()) {
        *it = webhook;
        Log(_T("Updated webhook"));
        return true;
    }
    return false;
}

std::vector<AgentWebhookConfig> CAgentBridge::GetWebhooks() {
    std::lock_guard<std::mutex> lock(m_webhooksMutex);
    return m_webhooks;
}

bool CAgentBridge::SendEventToWebhooks(AgentEventType eventType, const CAgentJson& data) {
    std::lock_guard<std::mutex> lock(m_webhooksMutex);
    bool anySent = false;
    
    // 获取事件名称
    std::string eventName = 
        eventType == EVENT_DEVICE_ONLINE ? "device.online" :
        eventType == EVENT_DEVICE_OFFLINE ? "device.offline" :
        eventType == EVENT_INPUT_CHANGED ? "input.changed" :
        eventType == EVENT_OUTPUT_CHANGED ? "output.changed" :
        eventType == EVENT_ALARM_ACTIVE ? "alarm.active" :
        eventType == EVENT_ALARM_CLEARED ? "alarm.cleared" :
        eventType == EVENT_ALARM_ACKNOWLEDGED ? "alarm.acknowledged" :
        eventType == EVENT_SYSTEM_SCAN_COMPLETE ? "scan.complete" :
        eventType == EVENT_SCHEDULE_CHANGED ? "schedule.changed" :
        eventType == EVENT_RULE_TRIGGERED ? "rule.triggered" : "unknown";
    
    time_t now = time(NULL);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    
    for (auto& webhook : m_webhooks) {
        if (!webhook.enabled) continue;
        
        // 检查是否订阅了该事件
        bool subscribed = false;
        for (auto& event : webhook.events) {
            if (event == "*" || event == eventName) {
                subscribed = true;
                break;
            }
        }
        
        if (!subscribed) continue;
        
        // 构建 Webhook 负载
        CAgentJson payload;
        payload.SetObject();
        payload.Add("event", CAgentJson(eventName));
        payload.Add("timestamp", CAgentJson(std::string(timeStr)));
        payload.Add("webhook_id", CAgentJson(webhook.webhookId));
        payload.Add("data", data);
        
        // 发送 Webhook（简化实现：记录日志）
        Log(_T("Webhook event sent: ") + CString(eventName.c_str()));
        webhook.successCount++;
        webhook.lastSent = std::string(timeStr);
        anySent = true;
    }
    
    return anySent;
}
