# T3000 AgentBridge 优化报告

## 优化概述

本次优化为 T3000 建筑自动化系统的 AgentBridge 智能体对接模块添加了以下功能：

- ✅ 批量操作 API（批量读取/写入）
- ✅ 数据导出功能（CSV/JSON）
- ✅ 告警规则引擎
- ✅ Webhook 通知支持
- ✅ OpenAPI/Swagger 文档
- ✅ Prometheus 监控指标
- ✅ 跨平台版本优化（AgentBridgeStandalone）

---

## 新增 API 端点

### 1. 批量操作 API

#### POST /api/v1/batch/read
批量读取多个输入点的值。

**请求体：**
```json
{
    "input_ids": [101, 102, 103]
}
```

**响应：**
```json
{
    "success": true,
    "data": [
        {"input_id": 101, "value": 22.5, "timestamp": "2026-04-26T12:00:00"},
        {"input_id": 102, "value": 45.0, "timestamp": "2026-04-26T12:00:00"},
        {"input_id": 103, "value": 350.0, "timestamp": "2026-04-26T12:00:00"}
    ]
}
```

#### POST /api/v1/batch/write
批量写入多个输出点的值。

**请求体：**
```json
{
    "writes": [
        {"output_id": 201, "value": 75.0},
        {"output_id": 202, "value": 50.0}
    ]
}
```

**响应：**
```json
{
    "success": true,
    "data": {
        "results": [
            {"output_id": 201, "value": 75.0, "success": true},
            {"output_id": 202, "value": 50.0, "success": true}
        ],
        "success_count": 2,
        "fail_count": 0,
        "total": 2
    }
}
```

---

### 2. 数据导出 API

#### GET /api/v1/export/devices
导出设备数据，支持 JSON 和 CSV 格式。

**查询参数：**
- `format`: `json`（默认）或 `csv`

**CSV 响应示例：**
```
ID,Name,Model,Serial Number,Firmware Version,Online,BACnet ID,Modbus ID
1,Office Thermostat,T3-8AI8AO,T300012345,3.2.1,是,1001,1
2,Hallway Sensor,T3-32AI,T300012346,2.1.0,是,1002,2
```

#### GET /api/v1/export/alarms
导出告警数据，支持 JSON 和 CSV 格式。

---

### 3. 告警规则 API

#### GET /api/v1/rules
获取告警规则列表。

**响应：**
```json
{
    "success": true,
    "data": [
        {
            "id": 1,
            "name": "高温告警",
            "description": "温度超过 30°C 时触发",
            "device_id": 1,
            "point_id": 101,
            "operator": 0,
            "threshold": 30.0,
            "enabled": true,
            "action": "notify",
            "webhook_url": "",
            "last_triggered": "",
            "trigger_count": 0
        }
    ]
}
```

#### POST /api/v1/rules
创建告警规则。

**请求体：**
```json
{
    "name": "高温告警",
    "device_id": 1,
    "point_id": 101,
    "operator": 0,
    "threshold": 30.0,
    "action": "notify"
}
```

**operator 值说明：**
- 0: `>` (大于)
- 1: `<` (小于)
- 2: `==` (等于)
- 3: `!=` (不等于)
- 4: `>=` (大于等于)
- 5: `<=` (小于等于)

#### DELETE /api/v1/rules/{id}
删除告警规则。

#### POST /api/v1/rules/evaluate
手动评估所有规则。

---

### 4. Webhook API

#### GET /api/v1/webhooks
获取 Webhook 列表。

#### POST /api/v1/webhooks
创建 Webhook。

**请求体：**
```json
{
    "name": "钉钉通知",
    "url": "https://oapi.dingtalk.com/robot/send?access_token=xxx",
    "events": ["alarm.active", "device.offline"]
}
```

#### DELETE /api/v1/webhooks/{id}
删除 Webhook。

---

### 5. Prometheus 监控指标

#### GET /metrics
返回 Prometheus 格式的监控指标。

**响应示例：**
```
# HELP t3000_devices_total Total number of devices
# TYPE t3000_devices_total gauge
t3000_devices_total 10

# HELP t3000_devices_online Number of online devices
# TYPE t3000_devices_online gauge
t3000_devices_online 8

# HELP t3000_rules_total Total number of alarm rules
# TYPE t3000_rules_total gauge
t3000_rules_total 5

# HELP t3000_rules_enabled Number of enabled alarm rules
# TYPE t3000_rules_enabled gauge
t3000_rules_enabled 3

# HELP t3000_webhooks_total Total number of webhooks
# TYPE t3000_webhooks_total gauge
t3000_webhooks_total 2

# HELP t3000_websocket_connections Number of WebSocket connections
# TYPE t3000_websocket_connections gauge
t3000_websocket_connections 3

# HELP t3000_info AgentBridge version info
# TYPE t3000_info gauge
t3000_info{version="1.0.0"} 1
```

---

### 6. API 文档

#### GET /api/v1/docs
返回 OpenAPI 3.0 格式的 API 文档。

---

## 文件变更清单

### T3000/AgentBridge.h
- 添加 `AgentAlarmRule` 结构体
- 添加 `AgentWebhookConfig` 结构体
- 添加规则引擎方法声明
- 添加 Webhook 管理方法声明
- 添加规则存储成员变量
- 添加 Webhook 存储成员变量

### T3000/AgentBridge.cpp
- 实现批量操作 API（`/api/v1/batch/read`, `/api/v1/batch/write`）
- 实现数据导出 API（`/api/v1/export/devices`, `/api/v1/export/alarms`）
- 实现告警规则 API（`/api/v1/rules`, `/api/v1/rules/{id}`, `/api/v1/rules/evaluate`）
- 实现 Webhook API（`/api/v1/webhooks`, `/api/v1/webhooks/{id}`）
- 实现 Prometheus 指标端点（`/metrics`）
- 实现 API 文档端点（`/api/v1/docs`）
- 实现规则引擎核心逻辑（`EvaluateRules`）
- 实现 Webhook 发送逻辑（`SendEventToWebhooks`）

### AgentBridgeStandalone/AgentBridgeStandalone.cpp
- 添加批量读取 API
- 添加数据导出 API
- 添加 Prometheus 指标端点
- 添加 API 文档端点

### test_api.py
- 添加批量操作测试
- 添加数据导出测试
- 添加告警规则测试
- 添加 Webhook 测试
- 添加 Prometheus 指标测试
- 添加 API 文档测试

---

## 使用示例

### 1. 创建告警规则

```bash
curl -X POST http://localhost:8080/api/v1/rules \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-api-key" \
  -d '{
    "name": "高温告警",
    "device_id": 1,
    "point_id": 101,
    "operator": 0,
    "threshold": 30.0,
    "action": "notify"
  }'
```

### 2. 批量读取输入值

```bash
curl -X POST http://localhost:8080/api/v1/batch/read \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-api-key" \
  -d '{
    "input_ids": [101, 102, 103]
  }'
```

### 3. 导出设备数据为 CSV

```bash
curl -X GET "http://localhost:8080/api/v1/export/devices?format=csv" \
  -H "X-API-Key: your-api-key" \
  -o devices.csv
```

### 4. 查看 Prometheus 指标

```bash
curl -X GET http://localhost:8080/metrics
```

### 5. 获取 API 文档

```bash
curl -X GET http://localhost:8080/api/v1/docs
```

---

## 架构改进

### 1. 线程安全
- 使用 `std::mutex` 保护规则列表和 Webhook 列表
- 使用 `std::lock_guard` 确保异常安全

### 2. 模块化设计
- 规则引擎独立于主逻辑
- Webhook 管理独立模块
- 支持插件式扩展

### 3. 跨平台兼容
- AgentBridgeStandalone 支持 Linux/Windows 编译
- 使用标准 C++ 库，无 MFC 依赖
- OpenSSL 3.0+ 兼容

---

## 性能优化

1. **批量操作**: 减少网络往返次数，提高吞吐量
2. **异步事件推送**: WebSocket 事件推送不阻塞主线程
3. **规则评估优化**: 只评估启用的规则，减少不必要的计算

---

## 安全改进

1. **API 认证**: 所有 API 端点支持 API Key 认证
2. **CORS 支持**: 跨域请求安全控制
3. **输入验证**: 所有用户输入经过验证

---

## 未来计划

1. **数据持久化**: 支持 SQLite 存储历史数据
2. **定时任务**: 支持定时扫描和报告
3. **多租户**: 支持多用户和多项目
4. **GraphQL**: 提供 GraphQL 查询接口
5. **gRPC**: 提供高性能 RPC 接口

---

## 编译说明

### Windows (Visual Studio 2019+)
```bash
msbuild "T3000 - VS2019.sln" /p:Configuration=Release /p:Platform=Win32
```

### Linux (CMake)
```bash
cd AgentBridgeStandalone
mkdir build && cd build
cmake ..
make
```

---

## 版本信息

- **AgentBridge 版本**: 1.0.0
- **API 版本**: v1
- **编译日期**: 2026-04-26
- **提交哈希**: 见 Git 历史
