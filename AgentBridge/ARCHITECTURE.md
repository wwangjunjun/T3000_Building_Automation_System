# T3000 智能体对接架构设计

## 概述

为 T3000 建筑自动化系统增加智能体（AI Agent）对接功能，使 AI 系统能够通过标准化 API 访问和控制建筑设备。

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                    AI 智能体系统                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │ ChatGPT  │  │ Claude   │  │ 自定义   │              │
│  │ / Copilot│  │ / Cursor │  │ Agent    │              │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘              │
│       │              │              │                    │
│       └──────────────┼──────────────┘                    │
│                      │                                   │
│              HTTP / WebSocket / MCP                      │
└──────────────────────┼───────────────────────────────────┘
                       │
              ┌───────▼────────┐
              │  AgentBridge   │  ← 新增模块
              │  (端口 8080)   │
              └───────┬────────┘
                      │
         ┌────────────┼────────────┐
         ▼            ▼            ▼
    ┌────────┐  ┌──────────┐  ┌──────────┐
    │ REST   │  │WebSocket │  │  MCP     │
    │ API    │  │  Events  │  │ Server   │
    └────┬───┘  └────┬─────┘  └────┬─────┘
         │           │             │
    ┌────▼───────────▼─────────────▼────┐
    │        T3000 主程序 (MFC)          │
    │  ┌────────┐ ┌────────┐ ┌───────┐ │
    │  │BACnet  │ │ Modbus │ │ TCP   │ │
    │  │ Stack  │ │ Stack  │ │Server │ │
    │  └───┬────┘ └───┬────┘ └───┬───┘ │
    │      │          │          │      │
    │  ┌───▼──────────▼──────────▼───┐ │
    │  │     建筑设备层               │ │
    │  │  温控器/IO模块/传感器/执行器  │ │
    │  └─────────────────────────────┘ │
    └──────────────────────────────────┘
```

## API 端点

### 设备管理
```
GET  /api/v1/devices              获取所有设备列表
GET  /api/v1/devices/{id}         获取设备详情
GET  /api/v1/devices/{id}/info    获取设备信息（型号/固件/序列号）
GET  /api/v1/devices/{id}/inputs  获取输入点
GET  /api/v1/devices/{id}/outputs 获取输出点
GET  /api/v1/devices/{id}/variables 获取变量
GET  /api/v1/devices/{id}/programs 获取程序
```

### 设备控制
```
GET  /api/v1/outputs/{id}         获取输出状态
POST /api/v1/outputs/{id}/write   写入输出值
GET  /api/v1/inputs/{id}          获取输入值
POST /api/v1/inputs/{id}/calibrate 校准输入
GET  /api/v1/variables/{id}       获取变量值
POST /api/v1/variables/{id}/write 写入变量
```

### 日程管理
```
GET  /api/v1/schedules            获取所有日程
GET  /api/v1/schedules/{id}       获取日程详情
POST /api/v1/schedules/{id}/write 写入日程
GET  /api/v1/annual-routines      获取年度例程
GET  /api/v1/weekly-routines      获取周例程
```

### 告警和监控
```
GET  /api/v1/alarms               获取告警列表
GET  /api/v1/alarms/{id}/ack      确认告警
GET  /api/v1/trend-logs           获取趋势日志
POST /api/v1/monitor/start        开始监控
POST /api/v1/monitor/stop         停止监控
```

### 系统
```
GET  /api/v1/system/info          系统信息
GET  /api/v1/system/status        系统状态
POST /api/v1/system/scan          扫描设备
GET  /api/v1/system/buildings     获取建筑列表
```

### WebSocket 事件
```
WS /api/v1/events                 实时事件流
  - device.online / device.offline
  - alarm.active / alarm.cleared
  - input.changed
  - output.changed
```

### MCP 工具
```
mcp://localhost:8080/mcp
  - list_devices
  - get_device_info
  - read_input
  - write_output
  - read_variable
  - write_variable
  - get_schedules
  - set_schedule
  - get_alarms
  - ack_alarm
  - scan_devices
  - get_building_info
```

## 安全

- API Key 认证（可选）
- IP 白名单
- 读写权限分离
- 操作日志审计

## 技术实现

- C++ 模块：AgentBridge.dll（嵌入 T3000）
- HTTP 服务器：基于 Windows HTTP API (HTTP.sys)
- WebSocket：基于 cpprest SDK
- JSON：nlohmann/json 或 RapidJSON
- MCP：JSON-RPC 2.0 协议
