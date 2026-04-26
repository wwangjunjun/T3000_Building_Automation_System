# T3000 Linux 编译方案

## 方案对比

| 方案 | 可行性 | 适用范围 | 优势 | 限制 |
|------|--------|----------|------|------|
| **A: MinGW + CMake** | ⚠️ 部分可行 | AgentBridge 独立模块 | 纯 Linux 环境，无需 Windows | MFC 主程序不可编译 |
| **B: Wine 运行** | ✅ 可行 | 运行已编译的 .exe | 可测试 Windows 程序 | 无法编译 MFC 项目 |
| **C: GitHub Actions** | ✅ 完全可行 | 完整项目编译 | 云端 Windows runner，免费 | 需网络，有使用限制 |

## 推荐策略：组合方案

```
┌─────────────────────────────────────────────────┐
│              Linux 开发环境                       │
│                                                  │
│  ┌─────────────┐  ┌──────────────┐              │
│  │ AgentBridge  │  │   Wine       │              │
│  │ 独立模块     │  │   测试环境   │              │
│  │ (MinGW编译)  │  │   (运行.exe) │              │
│  └──────┬──────┘  └──────┬───────┘              │
│         │                │                       │
│         └────────┬───────┘                       │
│                  │                               │
│         ┌────────▼────────┐                      │
│         │ GitHub Actions  │ ← 完整编译 + 测试    │
│         │ (Windows Runner)│                      │
│         └─────────────────┘                      │
└─────────────────────────────────────────────────┘
```

## 方案 A: AgentBridge 独立模块交叉编译

### 已创建文件

```
AgentBridgeStandalone/
├── CMakeLists.txt                 ← CMake 配置
├── AgentBridgeStandalone.h        ← 纯 C++ 头文件（无 MFC）
├── AgentBridgeStandalone.cpp      ← 纯 C++ 实现
└── AgentBridgeTest.cpp            ← 测试程序
```

### 编译方法

#### Linux 原生编译
```bash
cd AgentBridgeStandalone
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./AgentBridgeTest
```

#### MinGW 交叉编译（Linux → Windows）
```bash
# 安装 MinGW
sudo apt-get install mingw-w64 cmake

# 创建工具链文件
cat > mingw-toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
EOF

# 编译
mkdir build-mingw && cd build-mingw
cmake .. -DCMAKE_TOOLCHAIN_FILE=../mingw-toolchain.cmake
make -j$(nproc)
# 输出: AgentBridgeTest.exe
```

### API 测试
```bash
# 启动服务器
./AgentBridgeTest &

# 测试 HTTP API
curl http://localhost:8080/
curl http://localhost:8080/api/v1/system/info
curl http://localhost:8080/api/v1/devices
curl http://localhost:8080/api/v1/alarms

# 控制输出
curl -X POST http://localhost:8080/api/v1/outputs/201/write \
     -H "Content-Type: application/json" \
     -d '{"value": 75.0}'
```

## 方案 B: Wine 运行测试

### 安装 Wine
```bash
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install wine wine64
```

### 运行 Windows 程序
```bash
# 运行 T3000
wine T3000.exe

# 运行 AgentBridge 测试
wine AgentBridgeTest.exe

# 测试 API
curl http://localhost:8080/api/v1/system/info
```

## 方案 C: GitHub Actions CI/CD

### 已创建工作流

```
.github/workflows/
├── build.yml                        ← T3000 完整编译（Windows runner）
└── agentbridge-cross-compile.yml    ← AgentBridge 交叉编译（Linux runner）
```

### 使用方法

1. **推送代码到 GitHub**：
```bash
cd /root/workspace/T3000_Building_Automation_System
git add .
git commit -m "添加 AgentBridge 和 GitHub Actions CI"
git push origin master
```

2. **查看构建进度**：
```
https://github.com/你的用户名/T3000_Building_Automation_System/actions
```

3. **下载构建产物**：
- 在 Actions 页面点击最新的 workflow run
- 下载 Artifacts 中的编译产物

### 自动触发条件

| 事件 | 触发工作流 |
|------|-----------|
| 推送到 master/main | 完整编译 + 交叉编译 |
| 推送到 develop | 完整编译 + 交叉编译 |
| 创建 tag | 完整编译 + 创建 Release |
| 手动触发 | 可选择构建类型 |

## 文件清单

### 新增文件

| 文件 | 说明 |
|------|------|
| `.github/workflows/build.yml` | T3000 完整编译（Windows） |
| `.github/workflows/agentbridge-cross-compile.yml` | AgentBridge 交叉编译（Linux） |
| `AgentBridgeStandalone/CMakeLists.txt` | CMake 配置 |
| `AgentBridgeStandalone/AgentBridgeStandalone.h` | 纯 C++ 头文件 |
| `AgentBridgeStandalone/AgentBridgeStandalone.cpp` | 纯 C++ 实现 |
| `AgentBridgeStandalone/AgentBridgeTest.cpp` | 测试程序 |
| `Linux编译方案.md` | 本文档 |

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `T3000/AgentBridge.cpp` | 告警数据层对接 |
| `T3000/AgentBridge.h` | 添加 SetMainFrame |
| `T3000/AgentBridgeDlg.h` | 完整配置对话框 |
| `T3000/T3000_VS2019.vcxproj` | 添加 9 个新文件 |

## 快速开始

### 1. 立即可用：GitHub Actions CI
```bash
# 提交代码到 GitHub
cd /root/workspace/T3000_Building_Automation_System
git add .
git commit -m "T3000汉化 + AgentBridge + CI/CD"
git push

# GitHub Actions 会自动编译并生成安装包
```

### 2. 立即可用：AgentBridge 独立测试
```bash
# 等待 MinGW 安装完成后
cd /root/workspace/T3000_Building_Automation_System/AgentBridgeStandalone
mkdir build && cd build
cmake .. && make
./AgentBridgeTest
```

### 3. 待配置：Wine 测试
```bash
# 安装 Wine 后
sudo apt-get install wine
wine /path/to/T3000.exe
```

---
*最后更新: 2026年4月26日*
