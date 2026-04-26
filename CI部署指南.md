# T3000 完整 CI/CD 部署指南

## 当前状态

所有代码已在本地 `/root/workspace/T3000_Building_Automation_System/` 完成，包含：
- 35 个文件变更，21,525 行新增代码
- 汉化封装（85%）
- AgentBridge 智能体对接（92%）
- GitHub Actions CI/CD 工作流
- AgentBridge 独立跨平台模块

## 部署步骤

### 步骤 1: Fork 仓库到个人 GitHub

```bash
# 在 GitHub 上 fork:
# https://github.com/temcocontrols/T3000_Building_Automation_System
# → Fork → 选择你的账户

# 然后克隆你的 fork
git clone https://github.com/你的用户名/T3000_Building_Automation_System.git
cd T3000_Building_Automation_System

# 添加本地修改（从当前目录复制）
# 或者直接将当前目录的 .git 指向你的 fork
cd /root/workspace/T3000_Building_Automation_System
git remote set-url origin https://github.com/你的用户名/T3000_Building_Automation_System.git
git push origin master
```

### 步骤 2: 配置 GitHub Actions

推送代码后，GitHub Actions 会自动触发：

1. **`.github/workflows/build.yml`** — Windows 完整编译
   - 编译 T3000 主程序（VS2019 + MFC）
   - 编译 C# 控件库
   - 编译 Inno Setup 安装包
   - 上传 Artifacts

2. **`.github/workflows/agentbridge-cross-compile.yml`** — 跨平台编译
   - Linux 原生编译 AgentBridge 独立模块
   - MinGW 交叉编译 Windows 版本
   - Wine 测试运行

### 步骤 3: 查看构建结果

```
https://github.com/你的用户名/T3000_Building_Automation_System/actions
```

### 步骤 4: 下载构建产物

在 Actions 页面点击最新的 workflow run → Artifacts → 下载

---

## 方案 A: AgentBridge 独立模块（立即可用）

### Linux 原生编译

```bash
cd /root/workspace/T3000_Building_Automation_System/AgentBridgeStandalone
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./AgentBridgeTest
```

### 测试 API

```bash
# 启动服务器
./AgentBridgeTest &

# 测试端点
curl http://localhost:8080/
curl http://localhost:8080/api/v1/system/info
curl http://localhost:8080/api/v1/devices
curl http://localhost:8080/api/v1/alarms

# 控制输出
curl -X POST http://localhost:8080/api/v1/outputs/201/write \
     -H "Content-Type: application/json" \
     -d '{"value": 75.0}'
```

---

## 方案 B: Wine 运行测试

```bash
# 安装 Wine
sudo apt-get install wine wine64

# 运行 Windows 程序
wine /path/to/T3000.exe

# 运行 AgentBridge 测试
wine /path/to/AgentBridgeTest.exe
```

---

## 方案 C: GitHub Actions CI（推荐）

### 自动触发条件

| 事件 | 工作流 |
|------|--------|
| 推送到 master/main | 完整编译 + 交叉编译 |
| 推送到 develop | 完整编译 + 交叉编译 |
| 创建 tag (v*) | 完整编译 + 创建 Release |
| 手动触发 (workflow_dispatch) | 可选择 Release/Debug |

### 构建产物

每次构建会生成：
- `T3000-Release/` — 主程序 + DLL + 中文语言包
- `T3000-Debug/` — 调试版本
- `AgentBridge-Linux-x64/` — Linux 原生版本
- `AgentBridge-Windows-x64-MinGW/` — Windows 交叉编译版本
- `T3000_建筑自动化系统_Setup.exe` — Inno Setup 安装包（tag 触发时创建 Release）

---

## 文件清单

### 新增源码（35 个文件）

| 文件 | 说明 | 行数 |
|------|------|------|
| `T3000/AgentBridge.h/cpp` | 主模块（含数据层对接） | 910 |
| `T3000/AgentHttpServer.h/cpp` | HTTP REST API | 439 |
| `T3000/AgentWebSocket.h/cpp` | WebSocket 事件推送 | 433 |
| `T3000/AgentMcpServer.h/cpp` | MCP 协议服务器 | 903 |
| `T3000/AgentBridgeDlg.h` | 配置对话框 | 278 |
| `T3000/LanguageLocale_zh.ini` | 中文语言包 | 200+条 |
| `T3000/*_zh.rc` (7个) | 翻译资源文件 | - |
| `AgentBridgeStandalone/*` | 独立跨平台模块 | 1,145 |
| `.github/workflows/*.yml` | CI/CD 工作流 | 2 个 |
| `build_zh.bat` | 一键编译脚本 | - |
| `build_installer.bat` | 安装包编译脚本 | - |
| `test_api.py` | API 测试脚本 | - |

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `T3000/LanguageLocale.h` | 语言枚举修改 |
| `T3000/LanguageLocale.cpp` | SetLanguage 方法修复 |
| `T3000/T3000.cpp` | 语言初始化 + AgentBridge 启动 |
| `T3000/T3000_VS2019.vcxproj` | 添加 9 个新文件 |

---

## 快速验证

### 1. 本地测试 AgentBridge 独立模块

```bash
# 等待工具安装后
cd /root/workspace/T3000_Building_Automation_System/AgentBridgeStandalone
mkdir build && cd build
cmake .. && make
./AgentBridgeTest
```

### 2. Fork 后推送触发 CI

```bash
# Fork 仓库后
git remote set-url origin https://github.com/你的用户名/T3000_Building_Automation_System.git
git push origin master

# 查看 CI 进度
# https://github.com/你的用户名/T3000_Building_Automation_System/actions
```

### 3. 下载构建产物

Actions 页面 → 点击 workflow run → Artifacts → 下载

---

## 架构总结

```
┌─────────────────────────────────────────────────────┐
│                    Linux 开发环境                     │
│                                                      │
│  ┌──────────────────┐  ┌──────────────────┐        │
│  │ AgentBridge 独立  │  │  Wine 测试环境    │        │
│  │ 模块 (CMake)      │  │  (运行.exe)       │        │
│  │ 可交叉编译        │  │                  │        │
│  └────────┬─────────┘  └────────┬─────────┘        │
│           │                     │                   │
│           └──────────┬──────────┘                   │
│                      │                              │
│              ┌───────▼───────┐                      │
│              │ GitHub Actions │ ← 云端 Windows 编译  │
│              │ Windows Runner │                      │
│              └───────────────┘                      │
│                      │                              │
│              ┌───────▼───────┐                      │
│              │  构建产物      │                      │
│              │ .exe / .msi   │                      │
│              └───────────────┘                      │
└─────────────────────────────────────────────────────┘
```

---
*最后更新: 2026年4月26日*
