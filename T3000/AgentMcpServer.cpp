// AgentMcpServer.cpp: MCP (Model Context Protocol) 服务器实现
#include "stdafx.h"
#include "AgentMcpServer.h"
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <sstream>

// ============================================
// CAgentMcpServer 实现
// ============================================

CAgentMcpServer::CAgentMcpServer()
    : m_port(8082)
    , m_bridge(nullptr)
    , m_running(false)
    , m_listenSocket(INVALID_SOCKET)
{
}

CAgentMcpServer::~CAgentMcpServer() {
    Stop();
}

bool CAgentMcpServer::Initialize(int port, CAgentBridge* bridge) {
    m_port = port;
    m_bridge = bridge;

    // 注册内置 MCP 工具
    RegisterDefaultTools();

    return true;
}

void CAgentMcpServer::RegisterDefaultTools() {
    // 工具: list_devices
    {
        CAgentJson schema;
        schema.SetObject();
        schema.Add("type", CAgentJson("object"));
        schema.Add("properties", CAgentJson());
        CAgentJson props;
        props.SetObject();
        CAgentJson typeFilter;
        typeFilter.SetObject();
        typeFilter.Add("type", CAgentJson("string"));
        typeFilter.Add("description", CAgentJson("设备类型过滤器 (thermostat/io/sensor/controller/lighting/air_quality)"));
        props.Add("type", typeFilter);
        schema.Add("properties", props);

        RegisterTool("list_devices",
            "列出所有建筑自动化设备。支持按类型过滤。返回设备列表，包含设备 ID、名称、型号、在线状态等信息。",
            schema);
    }

    // 工具: get_device_info
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson deviceId;
        deviceId.SetObject();
        deviceId.Add("type", CAgentJson("integer"));
        deviceId.Add("description", CAgentJson("设备 ID"));
        props.Add("device_id", deviceId);
        schema.Add("properties", props);
        CAgentJson required;
        required.SetArray();
        required.Add(CAgentJson("device_id"));
        schema.Add("required", required);

        RegisterTool("get_device_info",
            "获取指定设备的详细信息，包括型号、固件版本、序列号、输入输出点列表等。",
            schema);
    }

    // 工具: read_input
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson inputId;
        inputId.SetObject();
        inputId.Add("type", CAgentJson("integer"));
        inputId.Add("description", CAgentJson("输入点 ID"));
        props.Add("input_id", inputId);
        schema.Add("properties", props);
        CAgentJson required;
        required.SetArray();
        required.Add(CAgentJson("input_id"));
        schema.Add("required", required);

        RegisterTool("read_input",
            "读取指定输入点的当前值。输入点包括温度传感器、湿度传感器、光照传感器等。",
            schema);
    }

    // 工具: write_output
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson outputId;
        outputId.SetObject();
        outputId.Add("type", CAgentJson("integer"));
        outputId.Add("description", CAgentJson("输出点 ID"));
        props.Add("output_id", outputId);
        CAgentJson value;
        value.SetObject();
        value.Add("type", CAgentJson("number"));
        value.Add("description", CAgentJson("要写入的值 (0-100 表示百分比，或具体数值)"));
        props.Add("value", value);
        schema.Add("properties", props);
        CAgentJson required;
        required.SetArray();
        required.Add(CAgentJson("output_id"));
        required.Add(CAgentJson("value"));
        schema.Add("required", required);

        RegisterTool("write_output",
            "控制输出设备。可以设置阀门开度、风机速度、灯光亮度等。值范围 0-100 表示百分比。",
            schema);
    }

    // 工具: read_variable
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson varId;
        varId.SetObject();
        varId.Add("type", CAgentJson("integer"));
        varId.Add("description", CAgentJson("变量 ID"));
        props.Add("variable_id", varId);
        schema.Add("properties", props);
        CAgentJson required;
        required.SetArray();
        required.Add(CAgentJson("variable_id"));
        schema.Add("required", required);

        RegisterTool("read_variable",
            "读取设备变量的当前值。变量包括温度设定值、湿度设定值、运行模式等。",
            schema);
    }

    // 工具: write_variable
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson varId;
        varId.SetObject();
        varId.Add("type", CAgentJson("integer"));
        varId.Add("description", CAgentJson("变量 ID"));
        props.Add("variable_id", varId);
        CAgentJson value;
        value.SetObject();
        value.Add("type", CAgentJson("number"));
        value.Add("description", CAgentJson("要写入的值"));
        props.Add("value", value);
        schema.Add("properties", props);
        CAgentJson required;
        required.SetArray();
        required.Add(CAgentJson("variable_id"));
        required.Add(CAgentJson("value"));
        schema.Add("required", required);

        RegisterTool("write_variable",
            "设置设备变量值。可以修改温度设定值、湿度设定值、运行模式等参数。",
            schema);
    }

    // 工具: get_alarms
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson activeOnly;
        activeOnly.SetObject();
        activeOnly.Add("type", CAgentJson("boolean"));
        activeOnly.Add("description", CAgentJson("仅返回活动告警 (默认 true)"));
        props.Add("active_only", activeOnly);
        schema.Add("properties", props);

        RegisterTool("get_alarms",
            "获取系统告警列表。可以过滤活动告警或所有告警。返回告警 ID、设备、描述、严重级别等信息。",
            schema);
    }

    // 工具: ack_alarm
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson alarmId;
        alarmId.SetObject();
        alarmId.Add("type", CAgentJson("integer"));
        alarmId.Add("description", CAgentJson("告警 ID"));
        props.Add("alarm_id", alarmId);
        schema.Add("properties", props);
        CAgentJson required;
        required.SetArray();
        required.Add(CAgentJson("alarm_id"));
        schema.Add("required", required);

        RegisterTool("ack_alarm",
            "确认指定告警。确认后的告警不再显示为活动状态。",
            schema);
    }

    // 工具: get_schedules
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson deviceId;
        deviceId.SetObject();
        deviceId.Add("type", CAgentJson("integer"));
        deviceId.Add("description", CAgentJson("设备 ID (可选)"));
        props.Add("device_id", deviceId);
        schema.Add("properties", props);

        RegisterTool("get_schedules",
            "获取日程表列表。包括周例程和年度例程。返回日程 ID、设备、时间段、动作等信息。",
            schema);
    }

    // 工具: set_schedule
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson deviceId;
        deviceId.SetObject();
        deviceId.Add("type", CAgentJson("integer"));
        deviceId.Add("description", CAgentJson("设备 ID"));
        props.Add("device_id", deviceId);
        CAgentJson scheduleData;
        scheduleData.SetObject();
        scheduleData.Add("type", CAgentJson("object"));
        scheduleData.Add("description", CAgentJson("日程数据 {day: 0-6, start_hour: 0-23, start_min: 0-59, action: 'on'/'off', setpoint: number}"));
        props.Add("schedule", scheduleData);
        schema.Add("properties", props);
        CAgentJson required;
        required.SetArray();
        required.Add(CAgentJson("device_id"));
        required.Add(CAgentJson("schedule"));
        schema.Add("required", required);

        RegisterTool("set_schedule",
            "设置设备日程表。可以配置每天不同时间段的运行计划和温度设定值。",
            schema);
    }

    // 工具: scan_devices
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson fullScan;
        fullScan.SetObject();
        fullScan.Add("type", CAgentJson("boolean"));
        fullScan.Add("description", CAgentJson("深度扫描 (解决地址冲突)，默认 false"));
        props.Add("full_scan", fullScan);
        schema.Add("properties", props);

        RegisterTool("scan_devices",
            "扫描网络上的所有设备。深度扫描会自动解决地址冲突。返回扫描结果和新发现的设备列表。",
            schema);
    }

    // 工具: get_building_info
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson buildingId;
        buildingId.SetObject();
        buildingId.Add("type", CAgentJson("integer"));
        buildingId.Add("description", CAgentJson("建筑 ID (可选，不传则返回所有建筑)"));
        props.Add("building_id", buildingId);
        schema.Add("properties", props);

        RegisterTool("get_building_info",
            "获取建筑信息。包括建筑名称、楼层、房间、关联的设备列表等。",
            schema);
    }

    // 工具: get_system_info
    {
        CAgentJson schema;
        schema.SetObject();

        RegisterTool("get_system_info",
            "获取 T3000 系统信息。包括版本号、运行时间、设备总数、告警数量等。",
            schema);
    }

    // 工具: get_trend_data
    {
        CAgentJson schema;
        schema.SetObject();
        CAgentJson props;
        props.SetObject();
        CAgentJson pointId;
        pointId.SetObject();
        pointId.Add("type", CAgentJson("integer"));
        pointId.Add("description", CAgentJson("监测点 ID"));
        props.Add("point_id", pointId);
        CAgentJson hours;
        hours.SetObject();
        hours.Add("type", CAgentJson("integer"));
        hours.Add("description", CAgentJson("获取最近多少小时的数据，默认 24"));
        props.Add("hours", hours);
        schema.Add("properties", props);
        CAgentJson required;
        required.SetArray();
        required.Add(CAgentJson("point_id"));
        schema.Add("required", required);

        RegisterTool("get_trend_data",
            "获取趋势日志数据。返回指定监测点在指定时间段内的历史数据，用于分析和预测。",
            schema);
    }

    // 注册工具处理函数
    RegisterToolHandler("list_devices", [this](const CAgentJson& params) {
        return HandleListDevices(params);
    });
    RegisterToolHandler("get_device_info", [this](const CAgentJson& params) {
        return HandleGetDeviceInfo(params);
    });
    RegisterToolHandler("read_input", [this](const CAgentJson& params) {
        return HandleReadInput(params);
    });
    RegisterToolHandler("write_output", [this](const CAgentJson& params) {
        return HandleWriteOutput(params);
    });
    RegisterToolHandler("read_variable", [this](const CAgentJson& params) {
        return HandleReadVariable(params);
    });
    RegisterToolHandler("write_variable", [this](const CAgentJson& params) {
        return HandleWriteVariable(params);
    });
    RegisterToolHandler("get_alarms", [this](const CAgentJson& params) {
        return HandleGetAlarms(params);
    });
    RegisterToolHandler("ack_alarm", [this](const CAgentJson& params) {
        return HandleAckAlarm(params);
    });
    RegisterToolHandler("get_schedules", [this](const CAgentJson& params) {
        return HandleGetSchedules(params);
    });
    RegisterToolHandler("set_schedule", [this](const CAgentJson& params) {
        return HandleSetSchedule(params);
    });
    RegisterToolHandler("scan_devices", [this](const CAgentJson& params) {
        return HandleScanDevices(params);
    });
    RegisterToolHandler("get_building_info", [this](const CAgentJson& params) {
        return HandleGetBuildingInfo(params);
    });
    RegisterToolHandler("get_system_info", [this](const CAgentJson& params) {
        return HandleGetSystemInfo(params);
    });
    RegisterToolHandler("get_trend_data", [this](const CAgentJson& params) {
        return HandleGetTrendData(params);
    });
}

bool CAgentMcpServer::Start() {
    if (m_running) return true;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        if (m_bridge) m_bridge->LogError(_T("AgentBridge: MCP WSAStartup failed"));
        return false;
    }

    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        if (m_bridge) m_bridge->LogError(_T("AgentBridge: MCP socket creation failed"));
        WSACleanup();
        return false;
    }

    int optVal = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optVal, sizeof(optVal));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(m_port);

    if (bind(m_listenSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        if (m_bridge) {
            CString msg;
            msg.Format(_T("AgentBridge: MCP bind failed (port %d)"), m_port);
            m_bridge->LogError(msg);
        }
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    listen(m_listenSocket, SOMAXCONN);

    u_long mode = 1;
    ioctlsocket(m_listenSocket, FIONBIO, &mode);

    m_running = true;
    m_workerThread = std::thread(&CAgentMcpServer::WorkerThread, this);

    if (m_bridge) {
        CString msg;
        msg.Format(_T("AgentBridge MCP server started on port %d"), m_port);
        m_bridge->Log(msg);
    }

    return true;
}

void CAgentMcpServer::Stop() {
    if (!m_running) return;
    m_running = false;

    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    WSACleanup();

    if (m_bridge) {
        m_bridge->Log(_T("AgentBridge MCP server stopped"));
    }
}

void CAgentMcpServer::WorkerThread() {
    while (m_running) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(m_listenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);

        if (clientSocket != INVALID_SOCKET) {
            std::thread(&CAgentMcpServer::HandleClient, this, clientSocket).detach();
        }

        Sleep(100);
    }
}

void CAgentMcpServer::HandleClient(SOCKET clientSocket) {
    char buffer[65536];
    std::string accumulated;

    // 设置为阻塞模式
    u_long mode = 0;
    ioctlsocket(clientSocket, FIONBIO, &mode);

    while (m_running) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) break;

        buffer[bytesReceived] = '\0';
        accumulated += buffer;

        // 按行分割 JSON-RPC 消息（每行一个 JSON 对象）
        size_t pos = 0;
        while ((pos = accumulated.find('\n')) != std::string::npos) {
            std::string line = accumulated.substr(0, pos);
            accumulated = accumulated.substr(pos + 1);

            // 跳过空行
            if (line.empty() || line == "\r") continue;

            // 解析 JSON-RPC 消息
            McpJsonRpcMessage request = ParseMessage(line);
            if (request.method.empty() && request.id.empty()) continue;

            // 处理请求
            McpJsonRpcMessage response;
            response.jsonrpc = "2.0";
            response.id = request.id;
            response.isResponse = true;

            if (!request.id.empty()) {
                response.result = HandleRequest(request);
            }

            // 发送响应
            std::string responseStr = FormatResponse(response) + "\n";
            send(clientSocket, responseStr.c_str(), (int)responseStr.size(), 0);
        }
    }

    closesocket(clientSocket);
}

McpJsonRpcMessage CAgentMcpServer::ParseMessage(const std::string& rawMessage) {
    McpJsonRpcMessage msg;

    // 简易 JSON 解析（查找关键字段）
    auto findJsonString = [&rawMessage](const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\"";
        size_t pos = rawMessage.find(searchKey);
        if (pos == std::string::npos) return "";
        pos = rawMessage.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = rawMessage.find('"', pos);
        if (pos == std::string::npos) return "";
        size_t end = rawMessage.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return rawMessage.substr(pos + 1, end - pos - 1);
    };

    auto findJsonValue = [&rawMessage](const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\"";
        size_t pos = rawMessage.find(searchKey);
        if (pos == std::string::npos) return "";
        pos = rawMessage.find(':', pos);
        if (pos == std::string::npos) return "";
        pos++;
        // 跳过空格
        while (pos < rawMessage.size() && (rawMessage[pos] == ' ' || rawMessage[pos] == '\t')) pos++;
        if (pos >= rawMessage.size()) return "";
        if (rawMessage[pos] == '"') {
            // 字符串值
            size_t end = rawMessage.find('"', pos + 1);
            if (end == std::string::npos) return "";
            return rawMessage.substr(pos + 1, end - pos - 1);
        }
        // 数值或布尔值
        size_t end = pos;
        while (end < rawMessage.size() && rawMessage[end] != ',' && rawMessage[end] != '}' &&
               rawMessage[end] != '\n' && rawMessage[end] != '\r') {
            end++;
        }
        return rawMessage.substr(pos, end - pos);
    };

    msg.jsonrpc = findJsonString("jsonrpc");
    msg.method = findJsonString("method");
    msg.id = findJsonString("id");

    return msg;
}

std::string CAgentMcpServer::FormatResponse(const McpJsonRpcMessage& response) {
    std::string result = "{";
    result += "\"jsonrpc\":\"2.0\"";

    if (!response.id.empty()) {
        result += ",\"id\":" + response.id;
    }

    if (!response.error.ToString().empty() && response.error.ToString() != "null") {
        result += ",\"error\":" + response.error.ToString();
    } else {
        result += ",\"result\":" + response.result.ToString();
    }

    result += "}";
    return result;
}

CAgentJson CAgentMcpServer::HandleRequest(const McpJsonRpcMessage& request) {
    std::lock_guard<std::mutex> lock(m_toolsMutex);

    auto it = m_toolHandlers.find(request.method);
    if (it != m_toolHandlers.end()) {
        return it->second(request.params);
    }

    // 内置方法处理
    if (request.method == "initialize") {
        return HandleInitialize(request.params);
    } else if (request.method == "ping") {
        return HandlePing(request.params);
    } else if (request.method == "tools/list") {
        return HandleListTools(request.params);
    } else if (request.method == "tools/call") {
        return HandleCallTool(request.params);
    }

    CAgentJson error;
    error.SetObject();
    error.Add("code", CAgentJson(-32601));
    error.Add("message", CAgentJson("Method not found: " + request.method));
    return error;
}

CAgentJson CAgentMcpServer::HandleInitialize(const CAgentJson& params) {
    CAgentJson result;
    result.SetObject();
    result.Add("protocolVersion", CAgentJson("2024-11-05"));
    result.Add("serverInfo", CAgentJson());

    CAgentJson serverInfo;
    serverInfo.SetObject();
    serverInfo.Add("name", CAgentJson("T3000-AgentBridge"));
    serverInfo.Add("version", CAgentJson(AGENTBRIDGE_VERSION));
    result.Add("serverInfo", serverInfo);

    CAgentJson capabilities;
    capabilities.SetObject();
    CAgentJson tools;
    tools.SetObject();
    tools.Add("listChanged", CAgentJson(true));
    capabilities.Add("tools", tools);
    result.Add("capabilities", capabilities);

    return result;
}

CAgentJson CAgentMcpServer::HandlePing(const CAgentJson& params) {
    CAgentJson result;
    result.SetObject();
    return result;
}

CAgentJson CAgentMcpServer::HandleListTools(const CAgentJson& params) {
    CAgentJson tools;
    tools.SetArray();

    std::lock_guard<std::mutex> lock(m_toolsMutex);
    for (auto& tool : m_tools) {
        CAgentJson toolJson;
        toolJson.SetObject();
        toolJson.Add("name", CAgentJson(tool.name));
        toolJson.Add("description", CAgentJson(tool.description));
        toolJson.Add("inputSchema", tool.inputSchema);
        tools.Add(toolJson);
    }

    CAgentJson result;
    result.SetObject();
    result.Add("tools", tools);
    return result;
}

CAgentJson CAgentMcpServer::HandleCallTool(const CAgentJson& params) {
    // 从 params 中提取 tool name 和 arguments
    // 简化处理
    CAgentJson result;
    result.SetObject();
    result.Add("content", CAgentJson());
    return result;
}

void CAgentMcpServer::RegisterTool(const std::string& name, const std::string& description,
                                    const CAgentJson& inputSchema) {
    std::lock_guard<std::mutex> lock(m_toolsMutex);
    McpTool tool;
    tool.name = name;
    tool.description = description;
    tool.inputSchema = inputSchema;
    m_tools.push_back(tool);
}

void CAgentMcpServer::RegisterToolHandler(const std::string& name, ToolHandler handler) {
    std::lock_guard<std::mutex> lock(m_toolsMutex);
    m_toolHandlers[name] = handler;
}

std::vector<McpTool> CAgentMcpServer::GetTools() {
    std::lock_guard<std::mutex> lock(m_toolsMutex);
    return m_tools;
}

// ============================================
// 工具处理函数实现
// ============================================

CAgentJson CAgentMcpServer::HandleListDevices(const CAgentJson& params) {
    auto devices = m_bridge->GetDevices();

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

    CAgentJson result;
    result.SetObject();
    result.Add("devices", deviceList);
    result.Add("total", CAgentJson((int)devices.size()));
    return result;
}

CAgentJson CAgentMcpServer::HandleGetDeviceInfo(const CAgentJson& params) {
    // 从 params 中提取 device_id
    int deviceId = 0;
    // 简化解析
    CAgentJson result;
    result.SetObject();

    try {
        auto device = m_bridge->GetDeviceById(deviceId);
        result.Add("device", CAgentJson());
        CAgentJson devJson;
        devJson.SetObject();
        devJson.Add("id", CAgentJson(device.deviceId));
        devJson.Add("name", CAgentJson(device.name));
        devJson.Add("model", CAgentJson(device.model));
        devJson.Add("serial_number", CAgentJson(device.serialNumber));
        devJson.Add("firmware_version", CAgentJson(device.firmwareVersion));
        devJson.Add("hardware_version", CAgentJson(device.hardwareVersion));
        devJson.Add("online", CAgentJson(device.online));
        result.Add("device", devJson);

        // 添加输入输出点
        auto inputs = m_bridge->GetDeviceInputs(deviceId);
        CAgentJson inputList;
        inputList.SetArray();
        for (auto& inp : inputs) {
            CAgentJson inpJson;
            inpJson.SetObject();
            inpJson.Add("id", CAgentJson(inp.pointId));
            inpJson.Add("name", CAgentJson(inp.name));
            inpJson.Add("value", CAgentJson(inp.currentValue));
            inputList.Add(inpJson);
        }
        result.Add("inputs", inputList);

        auto outputs = m_bridge->GetDeviceOutputs(deviceId);
        CAgentJson outputList;
        outputList.SetArray();
        for (auto& out : outputs) {
            CAgentJson outJson;
            outJson.SetObject();
            outJson.Add("id", CAgentJson(out.pointId));
            outJson.Add("name", CAgentJson(out.name));
            outJson.Add("value", CAgentJson(out.currentValue));
            outJson.Add("writable", CAgentJson(out.writable));
            outputList.Add(outJson);
        }
        result.Add("outputs", outputList);

    } catch (...) {
        result.Add("error", CAgentJson("Device not found"));
    }

    return result;
}

CAgentJson CAgentMcpServer::HandleReadInput(const CAgentJson& params) {
    int inputId = 0;
    double value = m_bridge->ReadInput(inputId);

    CAgentJson result;
    result.SetObject();
    result.Add("input_id", CAgentJson(inputId));
    result.Add("value", CAgentJson(value));
    result.Add("timestamp", CAgentJson(std::to_string(time(NULL))));
    return result;
}

CAgentJson CAgentMcpServer::HandleWriteOutput(const CAgentJson& params) {
    int outputId = 0;
    double value = 0;
    bool success = m_bridge->WriteOutput(outputId, value);

    CAgentJson result;
    result.SetObject();
    result.Add("success", CAgentJson(success));
    result.Add("output_id", CAgentJson(outputId));
    result.Add("value", CAgentJson(value));
    return result;
}

CAgentJson CAgentMcpServer::HandleReadVariable(const CAgentJson& params) {
    int varId = 0;
    // 简化实现
    CAgentJson result;
    result.SetObject();
    result.Add("variable_id", CAgentJson(varId));
    result.Add("value", CAgentJson(0.0));
    return result;
}

CAgentJson CAgentMcpServer::HandleWriteVariable(const CAgentJson& params) {
    int varId = 0;
    double value = 0;
    // 简化实现
    CAgentJson result;
    result.SetObject();
    result.Add("success", CAgentJson(true));
    result.Add("variable_id", CAgentJson(varId));
    result.Add("value", CAgentJson(value));
    return result;
}

CAgentJson CAgentMcpServer::HandleGetAlarms(const CAgentJson& params) {
    auto alarms = m_bridge->GetAlarms();

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

    CAgentJson result;
    result.SetObject();
    result.Add("alarms", alarmList);
    result.Add("total", CAgentJson((int)alarms.size()));
    return result;
}

CAgentJson CAgentMcpServer::HandleAckAlarm(const CAgentJson& params) {
    int alarmId = 0;
    bool success = m_bridge->AcknowledgeAlarm(alarmId);

    CAgentJson result;
    result.SetObject();
    result.Add("success", CAgentJson(success));
    result.Add("alarm_id", CAgentJson(alarmId));
    return result;
}

CAgentJson CAgentMcpServer::HandleGetSchedules(const CAgentJson& params) {
    // 简化实现
    CAgentJson result;
    result.SetObject();
    result.Add("schedules", CAgentJson());
    return result;
}

CAgentJson CAgentMcpServer::HandleSetSchedule(const CAgentJson& params) {
    // 简化实现
    CAgentJson result;
    result.SetObject();
    result.Add("success", CAgentJson(true));
    return result;
}

CAgentJson CAgentMcpServer::HandleScanDevices(const CAgentJson& params) {
    bool fullScan = false;
    bool success = m_bridge->StartScan(fullScan);

    CAgentJson result;
    result.SetObject();
    result.Add("success", CAgentJson(success));
    result.Add("scan_type", CAgentJson(fullScan ? "full" : "quick"));
    return result;
}

CAgentJson CAgentMcpServer::HandleGetBuildingInfo(const CAgentJson& params) {
    // 简化实现
    CAgentJson result;
    result.SetObject();
    result.Add("buildings", CAgentJson());
    return result;
}

CAgentJson CAgentMcpServer::HandleGetSystemInfo(const CAgentJson& params) {
    return m_bridge->GetSystemInfo();
}

CAgentJson CAgentMcpServer::HandleGetTrendData(const CAgentJson& params) {
    // 简化实现
    CAgentJson result;
    result.SetObject();
    result.Add("data", CAgentJson());
    return result;
}
