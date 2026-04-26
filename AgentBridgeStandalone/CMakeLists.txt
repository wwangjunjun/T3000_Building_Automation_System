cmake_minimum_required(VERSION 3.16)
project(AgentBridgeStandalone VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================
# AgentBridge 独立模块 - 可交叉编译
# 不依赖 MFC，可在 Linux/Windows 编译
# ============================================

# 源文件（独立跨平台实现，不依赖 MFC）
set(AGENTBRIDGE_SOURCES
    AgentBridgeStandalone.cpp
)

set(AGENTBRIDGE_HEADERS
    AgentBridgeStandalone.h
)

# ============================================
# 平台特定配置
# ============================================
if(WIN32)
    # Windows: 使用 Windows HTTP API + Winsock
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DNOMINMAX)
    set(LIBS httpapi ws2_32 crypt32 bcrypt)
elseif(UNIX)
    # Linux: 使用标准 socket + OpenSSL
    add_definitions(-DLINUX)
    find_package(OpenSSL REQUIRED)
    set(LIBS pthread OpenSSL::SSL OpenSSL::Crypto)
endif()

# ============================================
# 构建静态库
# ============================================
add_library(AgentBridge STATIC
    ${AGENTBRIDGE_SOURCES}
    ${AGENTBRIDGE_HEADERS}
)

target_include_directories(AgentBridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(AgentBridge PUBLIC ${LIBS})

# ============================================
# 构建可执行测试程序
# ============================================
add_executable(AgentBridgeTest
    AgentBridgeTest.cpp
)

target_link_libraries(AgentBridgeTest AgentBridge)

# ============================================
# 安装
# ============================================
install(TARGETS AgentBridge
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES ${AGENTBRIDGE_HEADERS}
    DESTINATION include/AgentBridge
)

# ============================================
# 交叉编译支持
# ============================================
if(CMAKE_CROSSCOMPILING)
    message(STATUS "交叉编译模式: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
    message(STATUS "工具链: ${CMAKE_CXX_COMPILER}")
endif()
