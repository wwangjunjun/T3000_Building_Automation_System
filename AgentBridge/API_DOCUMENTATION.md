# T3000 AgentBridge API 文档

## 概述

AgentBridge 是 T3000 建筑自动化系统的智能体对接模块，提供 REST API、WebSocket 事件推送和 MCP 协议支持，使 AI 智能体能够访问和控制建筑设备。

## 快速开始

### 启动服务

在 T3000 主程序中启用 AgentBridge：
```
工具 → AgentBridge → 启用并启动
```

默认端口：
- REST API: `http://localhost:8080`
- WebSocket: `ws://localhost:8081`
- MCP: `tcp://localhost:8082`

### 验证连接

```bash
# 检查服务状态
curl http://localhost:8080/api/v1/system/status

# 获取 API 信息
curl http://localhost:8080/
```

---

## REST API

### 基础信息

- **Base URL**: `http://localhost:8080/api/v1`
- **Content-Type**: `application/json`
- **认证**: 可选（通过 `X-API-Key` 头部）

### 设备管理

#### 获取设备列表

```
GET /api/v1/devices
```

**响应示例**:
```json
{
  "success": true,
  "data": [
    {
      "id": 1,
      "name": "Office Thermostat",
      "model": "T3-8AI8AO",
      "serial_number": "T300012345",
      "firmware_version": "3.2.1",
      "online": true,
      "bacnet_id": 1001,
      "modbus_id": 1
    }
  ]
}
```

#### 获取设备详情

```
GET /api/v1/devices/{id}
```

**响应示例**:
```json
{
  "success": true,
  "data": {
    "id": 1,
    "name": "Office Thermostat",
    "model": "T3-8AI8AO",
    "serial_number": "T300012345",
    "firmware_version": "3.2.1",
    "hardware_version": "2.0",
    "online": true,
    "bacnet_id": 1001,
    "modbus_id": 1,
    "inputs": [
      {
        "id": 101,
        "name": "Temperature",
        "type": 0,
        "value": 22.5,
        "unit": "°C"
      }
    ],
    "outputs": [
      {
        "id": 201,
        "name": "Valve Control",
        "type": 1,
        "value": 75.0,
        "writable": true
      }
    ]
  }
}
```

### 设备控制

#### 读取输入值

```
GET /api/v1/inputs/{id}
```

**响应示例**:
```json
{
  "success": true,
  "data": {
    "input_id": 101,
    "value": 22.5,
    "timestamp": "2026-04-26T12:00:00"
  }
}
```

#### 写入输出值

```
POST /api/v1/outputs/{id}/write
Content-Type: application/json

{
  "value": 75.0
}
```

**响应示例**:
```json
{
  "success": true,
  "output_id": 201,
  "value": 75.0
}
```

### 告警管理

#### 获取告警列表

```
GET /api/v1/alarms
```

**响应示例**:
```json
{
  "success": true,
  "data": [
    {
      "id": 1,
      "device_id": 1,
      "device_name": "Office Thermostat",
      "description": "High temperature alarm",
      "severity": 2,
      "acknowledged": false,
      "timestamp": "2026-04-26T11:30:00"
    }
  ]
}
```

**严重级别**: 0=正常, 1=警告, 2=严重, 3=紧急

#### 确认告警

```
POST /api/v1/alarms/{id}/ack
```

**响应示例**:
```json
{
  "success": true,
  "alarm_id": 1
}
```

### 系统管理

#### 获取系统信息

```
GET /api/v1/system/info
```

**响应示例**:
```json
{
  "success": true,
  "data": {
    "name": "T3000 Building Automation System",
    "agentbridge_version": "1.0.0",
    "http_port": 8080,
    "websocket_port": 8081,
    "mcp_port": 8082,
    "api_key_configured": false,
    "timestamp": "2026-04-26T12:00:00"
  }
}
```

#### 获取系统状态

```
GET /api/v1/system/status
```

**响应示例**:
```json
{
  "success": true,
  "data": {
    "running": true,
    "http_port": 8080,
    "websocket_port": 8081,
    "mcp_port": 8082,
    "websocket_clients": 2
  }
}
```

#### 扫描设备

```
POST /api/v1/system/scan
Content-Type: application/json

{
  "full_scan": false
}
```

**响应示例**:
```json
{
  "success": true,
  "scan_type": "quick"
}
```

---

## WebSocket 事件推送

### 连接

```javascript
const ws = new WebSocket('ws://localhost:8081');
```

### 事件类型

| 事件 | 说明 |
|------|------|
| `device.online` | 设备上线 |
| `device.offline` | 设备离线 |
| `input.changed` | 输入值变化 |
| `output.changed` | 输出值变化 |
| `alarm.active` | 新告警 |
| `alarm.cleared` | 告警清除 |
| `alarm.acknowledged` | 告警已确认 |
| `scan.complete` | 扫描完成 |
| `schedule.changed` | 日程变更 |

### 事件格式

```json
{
  "type": "0",
  "event": "device.online",
  "timestamp": "2026-04-26T12:00:00",
  "data": {
    "id": 1,
    "name": "Office Thermostat",
    "online": true
  }
}
```

### 欢迎消息

连接成功后会收到欢迎消息：

```json
{
  "type": "welcome",
  "message": "Connected to T3000 AgentBridge",
  "timestamp": "1714123200",
  "clientCount": 1
}
```

---

## MCP (Model Context Protocol)

### 连接

MCP 服务器通过 TCP 端口 8082 提供 JSON-RPC 2.0 接口。

### 协议格式

每行一个 JSON 对象：

```json
{"jsonrpc":"2.0","method":"tools/list","id":"1"}
```

响应：

```json
{"jsonrpc":"2.0","id":"1","result":{"tools":[...]}}
```

### 可用工具

#### list_devices
列出所有设备。

**参数**:
```json
{"type": "thermostat"}  // 可选，按类型过滤
```

#### get_device_info
获取设备详细信息。

**参数**:
```json
{"device_id": 1}
```

#### read_input
读取输入值。

**参数**:
```json
{"input_id": 101}
```

#### write_output
控制输出设备。

**参数**:
```json
{"output_id": 201, "value": 75.0}
```

#### read_variable
读取变量值。

**参数**:
```json
{"variable_id": 1}
```

#### write_variable
设置变量值。

**参数**:
```json
{"variable_id": 1, "value": 22.5}
```

#### get_alarms
获取告警列表。

**参数**:
```json
{"active_only": true}
```

#### ack_alarm
确认告警。

**参数**:
```json
{"alarm_id": 1}
```

#### get_schedules
获取日程表。

**参数**:
```json
{"device_id": 1}  // 可选
```

#### set_schedule
设置日程表。

**参数**:
```json
{
  "device_id": 1,
  "schedule": {
    "day": 1,
    "start_hour": 8,
    "start_min": 0,
    "action": "on",
    "setpoint": 22.0
  }
}
```

#### scan_devices
扫描设备。

**参数**:
```json
{"full_scan": false}
```

#### get_building_info
获取建筑信息。

**参数**:
```json
{"building_id": 1}  // 可选
```

#### get_system_info
获取系统信息。

**参数**: 无

#### get_trend_data
获取趋势数据。

**参数**:
```json
{"point_id": 101, "hours": 24}
```

---

## 使用示例

### Python

```python
import requests
import json

BASE_URL = "http://localhost:8080/api/v1"

# 获取设备列表
response = requests.get(f"{BASE_URL}/devices")
devices = response.json()["data"]
print(f"Found {len(devices)} devices")

# 读取温度传感器
response = requests.get(f"{BASE_URL}/inputs/101")
temp = response.json()["data"]["value"]
print(f"Temperature: {temp}°C")

# 控制阀门
requests.post(f"{BASE_URL}/outputs/201/write",
              json={"value": 75.0})
print("Valve set to 75%")

# 获取告警
response = requests.get(f"{BASE_URL}/alarms")
alarms = response.json()["data"]
for alarm in alarms:
    print(f"Alarm: {alarm['description']} (severity: {alarm['severity']})")
```

### Node.js

```javascript
const fetch = require('node-fetch');
const WebSocket = require('ws');

const BASE_URL = 'http://localhost:8080/api/v1';

// 获取设备列表
async function getDevices() {
    const response = await fetch(`${BASE_URL}/devices`);
    const data = await response.json();
    console.log(`Found ${data.data.length} devices`);
    return data.data;
}

// 控制输出
async function setOutput(id, value) {
    await fetch(`${BASE_URL}/outputs/${id}/write`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ value })
    });
    console.log(`Output ${id} set to ${value}`);
}

// WebSocket 事件监听
const ws = new WebSocket('ws://localhost:8081');
ws.on('message', (data) => {
    const event = JSON.parse(data);
    console.log(`Event: ${event.event}`, event.data);
});
```

### curl

```bash
# 获取设备列表
curl http://localhost:8080/api/v1/devices

# 读取输入
curl http://localhost:8080/api/v1/inputs/101

# 控制输出
curl -X POST http://localhost:8080/api/v1/outputs/201/write \
     -H "Content-Type: application/json" \
     -d '{"value": 75.0}'

# 获取告警
curl http://localhost:8080/api/v1/alarms

# 扫描设备
curl -X POST http://localhost:8080/api/v1/system/scan \
     -H "Content-Type: application/json" \
     -d '{"full_scan": false}'
```

---

## 错误处理

所有 API 错误返回统一格式：

```json
{
  "success": false,
  "error": "错误描述",
  "code": 400
}
```

常见 HTTP 状态码：

| 状态码 | 说明 |
|--------|------|
| 200 | 成功 |
| 400 | 请求参数错误 |
| 401 | 未授权（API Key 无效） |
| 404 | 资源不存在 |
| 500 | 服务器内部错误 |

---

## 安全配置

### API Key 认证

在 T3000 中配置 API Key：
```
工具 → AgentBridge → 设置 API Key
```

使用 API Key 访问：
```bash
curl -H "X-API-Key: your-api-key" http://localhost:8080/api/v1/devices
```

### IP 白名单

配置允许访问的 IP 地址：
```
工具 → AgentBridge → IP 白名单
```

支持通配符：`192.168.1.*`

---

## 与 AI 智能体集成

### ChatGPT / Claude

使用 MCP 协议将 T3000 作为 AI 工具：

```json
// MCP 配置
{
  "mcpServers": {
    "t3000": {
      "url": "http://localhost:8082",
      "transport": "stdio"
    }
  }
}
```

### Home Assistant

通过 REST API 集成：

```yaml
# configuration.yaml
rest:
  - resource: "http://localhost:8080/api/v1/devices"
    scan_interval: 30
    sensor:
      - name: "T3000 Devices"
        value_template: "{{ value_json.data | length }}"
```

---

## 故障排除

### 端口被占用

```bash
# Windows 查看端口占用
netstat -ano | findstr :8080

# 修改端口
# 工具 → AgentBridge → 修改端口号
```

### 服务未启动

1. 检查 T3000 中 AgentBridge 是否启用
2. 查看日志：工具 → AgentBridge → 日志
3. 检查防火墙是否阻止

### MCP 连接失败

1. 确认 MCP 端口（默认 8082）未被占用
2. 检查 JSON-RPC 消息格式
3. 查看 MCP 服务器日志
