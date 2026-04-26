#!/usr/bin/env python3
"""
T3000 AgentBridge API 测试脚本
测试 REST API、WebSocket 和 MCP 协议
"""

import json
import time
import sys
import threading

# ============================================
# 配置
# ============================================
BASE_URL = "http://localhost:8080"
API_PREFIX = "/api/v1"
WS_URL = "ws://localhost:8081"
MCP_HOST = "localhost"
MCP_PORT = 8082
API_KEY = "t3000-agent-key-change-me"  # 如果配置了 API Key

headers = {
    "Content-Type": "application/json",
}
if API_KEY:
    headers["X-API-Key"] = API_KEY

# ============================================
# REST API 测试
# ============================================
def test_rest_api():
    """测试 REST API 端点"""
    import urllib.request
    import urllib.error
    
    print("=" * 60)
    print("REST API 测试")
    print("=" * 60)
    
    endpoints = [
        ("GET", "/", "API 信息"),
        ("GET", f"{API_PREFIX}/system/info", "系统信息"),
        ("GET", f"{API_PREFIX}/system/status", "系统状态"),
        ("GET", f"{API_PREFIX}/devices", "设备列表"),
        ("GET", f"{API_PREFIX}/docs", "API 文档"),
        ("GET", "/metrics", "Prometheus 指标"),
    ]
    
    for method, path, desc in endpoints:
        url = BASE_URL + path
        print(f"\n[{method}] {desc}")
        print(f"  URL: {url}")
        
        try:
            req = urllib.request.Request(url, headers=headers, method=method)
            with urllib.request.urlopen(req, timeout=5) as resp:
                data = json.loads(resp.read().decode())
                print(f"  状态码: {resp.status}")
                print(f"  响应: {json.dumps(data, ensure_ascii=False, indent=2)[:300]}")
        except urllib.error.HTTPError as e:
            print(f"  HTTP 错误: {e.code} - {e.reason}")
            try:
                body = json.loads(e.read().decode())
                print(f"  错误详情: {json.dumps(body, ensure_ascii=False)}")
            except:
                pass
        except Exception as e:
            print(f"  连接错误: {e}")
            print(f"  ⚠️ 请确保 T3000 已启动且 AgentBridge 已启用")
            return False
    
    # 测试 POST 端点
    print(f"\n[POST] 扫描设备")
    url = BASE_URL + f"{API_PREFIX}/system/scan"
    try:
        data = json.dumps({"full_scan": False}).encode()
        req = urllib.request.Request(url, data=data, headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=5) as resp:
            result = json.loads(resp.read().decode())
            print(f"  状态码: {resp.status}")
            print(f"  响应: {json.dumps(result, ensure_ascii=False)}")
    except Exception as e:
        print(f"  错误: {e}")
    
    # 测试批量读取
    print(f"\n[POST] 批量读取输入值")
    url = BASE_URL + f"{API_PREFIX}/batch/read"
    try:
        data = json.dumps({"input_ids": [101, 102, 103]}).encode()
        req = urllib.request.Request(url, data=data, headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=5) as resp:
            result = json.loads(resp.read().decode())
            print(f"  状态码: {resp.status}")
            print(f"  响应: {json.dumps(result, ensure_ascii=False)}")
    except Exception as e:
        print(f"  错误: {e}")
    
    # 测试数据导出（CSV）
    print(f"\n[GET] 导出设备数据（CSV）")
    url = BASE_URL + f"{API_PREFIX}/export/devices?format=csv"
    try:
        req = urllib.request.Request(url, headers=headers, method="GET")
        with urllib.request.urlopen(req, timeout=5) as resp:
            csv_data = resp.read().decode()
            print(f"  状态码: {resp.status}")
            print(f"  CSV 数据:\n{csv_data[:500]}")
    except Exception as e:
        print(f"  错误: {e}")
    
    # 测试告警规则
    print(f"\n[GET] 获取告警规则列表")
    url = BASE_URL + f"{API_PREFIX}/rules"
    try:
        req = urllib.request.Request(url, headers=headers, method="GET")
        with urllib.request.urlopen(req, timeout=5) as resp:
            result = json.loads(resp.read().decode())
            print(f"  状态码: {resp.status}")
            print(f"  响应: {json.dumps(result, ensure_ascii=False)}")
    except Exception as e:
        print(f"  错误: {e}")
    
    # 测试 Webhook
    print(f"\n[GET] 获取 Webhook 列表")
    url = BASE_URL + f"{API_PREFIX}/webhooks"
    try:
        req = urllib.request.Request(url, headers=headers, method="GET")
        with urllib.request.urlopen(req, timeout=5) as resp:
            result = json.loads(resp.read().decode())
            print(f"  状态码: {resp.status}")
            print(f"  响应: {json.dumps(result, ensure_ascii=False)}")
    except Exception as e:
        print(f"  错误: {e}")
    
    print(f"\n[POST] 控制输出")
    url = BASE_URL + f"{API_PREFIX}/outputs/201/write"
    try:
        data = json.dumps({"value": 75.0}).encode()
        req = urllib.request.Request(url, data=data, headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=5) as resp:
            result = json.loads(resp.read().decode())
            print(f"  状态码: {resp.status}")
            print(f"  响应: {json.dumps(result, ensure_ascii=False)}")
    except Exception as e:
        print(f"  错误: {e}")
    
    return True


# ============================================
# WebSocket 测试
# ============================================
def test_websocket():
    """测试 WebSocket 事件推送"""
    try:
        import websocket
    except ImportError:
        print("\n⚠️  需要安装 websocket-client: pip install websocket-client")
        print("   跳过 WebSocket 测试")
        return
    
    print("\n" + "=" * 60)
    print("WebSocket 测试")
    print("=" * 60)
    
    def on_message(ws, message):
        data = json.loads(message)
        print(f"  📨 收到事件: {data.get('event', 'unknown')}")
        print(f"     时间: {data.get('timestamp', 'N/A')}")
        print(f"     数据: {json.dumps(data.get('data', {}), ensure_ascii=False)}")
    
    def on_error(ws, error):
        print(f"  ❌ WebSocket 错误: {error}")
    
    def on_close(ws, close_status_code, close_msg):
        print(f"  🔌 WebSocket 连接关闭")
    
    def on_open(ws):
        print(f"  ✅ WebSocket 连接成功")
        print(f"     等待事件推送... (10秒)")
    
    try:
        ws = websocket.WebSocketApp(
            WS_URL,
            on_open=on_open,
            on_message=on_message,
            on_error=on_error,
            on_close=on_close
        )
        
        # 在后台运行
        ws_thread = threading.Thread(target=ws.run_forever)
        ws_thread.daemon = True
        ws_thread.start()
        
        # 等待事件
        time.sleep(10)
        
        ws.close()
        ws_thread.join(timeout=2)
        
    except Exception as e:
        print(f"  ❌ 连接错误: {e}")
        print(f"  ⚠️ 请确保 T3000 已启动且 AgentBridge WebSocket 已启用")


# ============================================
# MCP 测试
# ============================================
def test_mcp():
    """测试 MCP 协议"""
    import socket
    
    print("\n" + "=" * 60)
    print("MCP 协议测试")
    print("=" * 60)
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((MCP_HOST, MCP_PORT))
        print(f"  ✅ MCP 连接成功 ({MCP_HOST}:{MCP_PORT})")
        
        # 测试 tools/list
        print(f"\n  [MCP] tools/list")
        request = json.dumps({
            "jsonrpc": "2.0",
            "method": "tools/list",
            "id": "1"
        }) + "\n"
        sock.send(request.encode())
        
        response = sock.recv(4096).decode()
        data = json.loads(response.strip())
        
        if "result" in data:
            tools = data["result"].get("tools", [])
            print(f"  可用工具数: {len(tools)}")
            for tool in tools[:5]:
                print(f"    - {tool.get('name')}: {tool.get('description', '')[:60]}")
            if len(tools) > 5:
                print(f"    ... 还有 {len(tools) - 5} 个工具")
        else:
            print(f"  响应: {json.dumps(data, ensure_ascii=False)[:200]}")
        
        # 测试 ping
        print(f"\n  [MCP] ping")
        request = json.dumps({
            "jsonrpc": "2.0",
            "method": "ping",
            "id": "2"
        }) + "\n"
        sock.send(request.encode())
        
        response = sock.recv(4096).decode()
        data = json.loads(response.strip())
        print(f"  响应: {json.dumps(data, ensure_ascii=False)}")
        
        # 测试 initialize
        print(f"\n  [MCP] initialize")
        request = json.dumps({
            "jsonrpc": "2.0",
            "method": "initialize",
            "id": "3",
            "params": {}
        }) + "\n"
        sock.send(request.encode())
        
        response = sock.recv(4096).decode()
        data = json.loads(response.strip())
        if "result" in data:
            info = data["result"]
            print(f"  协议版本: {info.get('protocolVersion', 'N/A')}")
            server_info = info.get('serverInfo', {})
            print(f"  服务器: {server_info.get('name', 'N/A')} v{server_info.get('version', 'N/A')}")
        
        sock.close()
        print(f"\n  ✅ MCP 测试完成")
        
    except ConnectionRefusedError:
        print(f"  ❌ 连接被拒绝 - MCP 服务器未启动")
        print(f"  ⚠️ 请确保 T3000 已启动且 AgentBridge MCP 已启用")
    except Exception as e:
        print(f"  ❌ MCP 测试错误: {e}")


# ============================================
# 认证测试
# ============================================
def test_auth():
    """测试 API 认证"""
    import urllib.request
    import urllib.error
    
    print("\n" + "=" * 60)
    print("认证测试")
    print("=" * 60)
    
    # 测试无认证
    print("\n[测试] 无 API Key 访问")
    url = BASE_URL + f"{API_PREFIX}/system/info"
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=5) as resp:
            print(f"  ⚠️  未配置 API Key，访问成功（{resp.status}）")
    except urllib.error.HTTPError as e:
        if e.code == 401:
            print(f"  ✅ API Key 认证已启用，正确返回 401")
        else:
            print(f"  HTTP 错误: {e.code}")
    except Exception as e:
        print(f"  连接错误: {e}")
    
    # 测试错误 API Key
    print("\n[测试] 错误 API Key")
    try:
        req = urllib.request.Request(url, headers={"X-API-Key": "wrong-key"}, method="GET")
        with urllib.request.urlopen(req, timeout=5) as resp:
            print(f"  ⚠️  错误 Key 也能访问（{resp.status}）")
    except urllib.error.HTTPError as e:
        if e.code == 401:
            print(f"  ✅ 错误 API Key 被正确拒绝（401）")
        else:
            print(f"  HTTP 错误: {e.code}")
    except Exception as e:
        print(f"  连接错误: {e}")


# ============================================
# 主函数
# ============================================
def main():
    print("=" * 60)
    print("  T3000 AgentBridge API 测试")
    print(f"  时间: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  服务器: {BASE_URL}")
    print("=" * 60)
    
    # 运行所有测试
    test_rest_api()
    test_websocket()
    test_mcp()
    test_auth()
    
    print("\n" + "=" * 60)
    print("  测试完成")
    print("=" * 60)


if __name__ == "__main__":
    main()
