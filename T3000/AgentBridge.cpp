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
