// AgentBridgeTest.cpp: 独立测试程序
// 不依赖 MFC，可在 Linux/Windows 编译运行
#include "AgentBridgeStandalone.h"
#include <iostream>
#include <chrono>

// 模拟设备数据
std::vector<DeviceInfo> MockGetDevices() {
    return {
        {1, "Office Thermostat", "T3-8AI8AO", "T300012345", "3.2.1", true, 1001, 1},
        {2, "Hallway Sensor", "T3-32AI", "T300012346", "2.1.0", true, 1002, 2},
        {3, "Lighting Controller", "T3-LC", "T300012347", "1.5.0", false, 1003, 3},
    };
}

DeviceInfo MockGetDeviceById(int id) {
    auto devices = MockGetDevices();
    for (auto& d : devices) {
        if (d.id == id) return d;
    }
    return {0, "", "", "", "", false, 0, 0};
}

std::vector<PointInfo> MockGetInputs(int deviceId) {
    return {
        {101, "Temperature", 22.5, "°C", false},
        {102, "Humidity", 45.0, "%", false},
        {103, "Light Level", 350.0, "lux", false},
    };
}

std::vector<PointInfo> MockGetOutputs(int deviceId) {
    return {
        {201, "Valve Control", 75.0, "%", true},
        {202, "Fan Speed", 50.0, "%", true},
        {203, "Light Dimmer", 100.0, "%", true},
    };
}

bool MockWriteOutput(int outputId, double value) {
    std::cout << "  [Mock] Write output " << outputId << " = " << value << std::endl;
    return true;
}

double MockReadInput(int inputId) {
    return 22.5 + (inputId % 10) * 0.5;
}

std::vector<AlarmInfo> MockGetAlarms() {
    return {
        {1, 1, "Office Thermostat", "High temperature alarm", 2, false, "2026-04-26T12:00:00"},
        {2, 2, "Hallway Sensor", "Sensor offline", 1, true, "2026-04-26T11:30:00"},
    };
}

bool MockAckAlarm(int alarmId) {
    std::cout << "  [Mock] Acknowledge alarm " << alarmId << std::endl;
    return true;
}

bool MockStartScan(bool fullScan) {
    std::cout << "  [Mock] " << (fullScan ? "Full" : "Quick") << " scan started" << std::endl;
    return true;
}

void MockLog(const std::string& msg) {
    std::cout << "[AgentBridge] " << msg << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  AgentBridge Standalone Test" << std::endl;
    std::cout << "  Platform: " <<
#ifdef _WIN32
        "Windows"
#else
        "Linux"
#endif
        << std::endl;
    std::cout << "========================================" << std::endl;

    AgentBridge bridge;
    bridge.SetPort(8080);
    bridge.SetApiKey("test-key-123");
    bridge.SetEnabled(true);

    // 设置回调
    bridge.SetGetDevicesCallback(MockGetDevices);
    bridge.SetGetDeviceByIdCallback(MockGetDeviceById);
    bridge.SetGetInputsCallback(MockGetInputs);
    bridge.SetGetOutputsCallback(MockGetOutputs);
    bridge.SetWriteOutputCallback(MockWriteOutput);
    bridge.SetReadInputCallback(MockReadInput);
    bridge.SetGetAlarmsCallback(MockGetAlarms);
    bridge.SetAckAlarmCallback(MockAckAlarm);
    bridge.SetStartScanCallback(MockStartScan);
    bridge.SetLogCallback(MockLog);

    // 初始化并启动
    std::cout << "\n[1] 初始化..." << std::endl;
    if (!bridge.Initialize()) {
        std::cerr << "初始化失败!" << std::endl;
        return 1;
    }

    std::cout << "[2] 启动服务器..." << std::endl;
    if (!bridge.Start()) {
        std::cerr << "启动失败!" << std::endl;
        return 1;
    }

    std::cout << "\n[3] 服务器已启动:" << std::endl;
    std::cout << "    HTTP:    http://localhost:8080" << std::endl;
    std::cout << "    WebSocket: ws://localhost:8081" << std::endl;
    std::cout << "    MCP:     tcp://localhost:8082" << std::endl;
    std::cout << "\n    按 Enter 键停止服务器..." << std::endl;

    std::string line;
    std::getline(std::cin, line);

    std::cout << "\n[4] 停止服务器..." << std::endl;
    bridge.Stop();

    std::cout << "\n测试完成!" << std::endl;
    return 0;
}
