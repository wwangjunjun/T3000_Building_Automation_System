// AgentBridgeDlg.h: 智能体对接配置对话框
#pragma once

#include "AgentBridge.h"

// CAgentBridgeDlg dialog
class CAgentBridgeDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CAgentBridgeDlg)

public:
    CAgentBridgeDlg(CWnd* pParent = NULL);
    virtual ~CAgentBridgeDlg();

    enum { IDD = IDD_AGENTBRIDGE_CONFIG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);

    DECLARE_MESSAGE_MAP()

public:
    virtual BOOL OnInitDialog();
    afx_msg void OnBnClickedButtonStart();
    afx_msg void OnBnClickedButtonStop();
    afx_msg void OnBnClickedButtonSave();
    afx_msg void OnBnClickedButtonCheck();
    afx_msg LRESULT OnLogUpdate(WPARAM wParam, LPARAM lParam);

private:
    int m_port;
    CString m_apiKey;
    bool m_enabled;
    CString m_logMessages;
    CEdit m_editLog;
    CEdit m_editPort;
    CEdit m_editApiKey;
    CButton m_btnStart;
    CButton m_btnStop;
    CButton m_btnCheck;
    CCheckBox m_chkEnabled;

    void UpdateUI();
    void AddLog(const CString& message);
    static CAgentBridgeDlg* s_instance;
    static void LogCallback(const CString& message);
};

// AgentBridgeDlg.cpp: 智能体对接配置对话框实现
#include "stdafx.h"
#include "T3000.h"
#include "AgentBridgeDlg.h"
#include "afxdialogex.h"

// 对话框资源定义（需要在 .rc 文件中添加）
// IDD_AGENTBRIDGE_CONFIG DIALOGEX 0, 0, 400, 350
// STYLE DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU
// CAPTION "AgentBridge 配置"
// FONT 9, "微软雅黑"
// BEGIN
//     LTEXT           "端口号:",IDC_STATIC,10,15,40,10
//     EDITTEXT        IDC_EDIT_AGENT_PORT,60,12,80,14,ES_NUMBER
//     LTEXT           "API 密钥:",IDC_STATIC,10,35,40,10
//     EDITTEXT        IDC_EDIT_API_KEY,60,32,200,14
//     CONTROL         "启用 AgentBridge",IDC_CHECK_ENABLED,"Button",BS_AUTOCHECKBOX,10,55,120,10
//     PUSHBUTTON      "启动",IDC_BUTTON_START,10,75,60,20
//     PUSHBUTTON      "停止",IDC_BUTTON_STOP,80,75,60,20
//     PUSHBUTTON      "保存配置",IDC_BUTTON_SAVE,150,75,60,20
//     PUSHBUTTON      "检查状态",IDC_BUTTON_CHECK,220,75,60,20
//     LTEXT           "日志:",IDC_STATIC,10,105,20,10
//     EDITTEXT        IDC_EDIT_LOG,10,118,380,220,ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL
// END

#define IDC_EDIT_AGENT_PORT     1100
#define IDC_EDIT_API_KEY        1101
#define IDC_CHECK_ENABLED       1102
#define IDC_BUTTON_START        1103
#define IDC_BUTTON_STOP         1104
#define IDC_BUTTON_SAVE         1105
#define IDC_BUTTON_CHECK        1106
#define IDC_EDIT_LOG            1107
#define IDD_AGENTBRIDGE_CONFIG  1108

#define WM_AGENTBRIDGE_LOG_UPDATE (WM_USER + 9000)

CAgentBridgeDlg* CAgentBridgeDlg::s_instance = NULL;

IMPLEMENT_DYNAMIC(CAgentBridgeDlg, CDialogEx)

CAgentBridgeDlg::CAgentBridgeDlg(CWnd* pParent)
    : CDialogEx(CAgentBridgeDlg::IDD, pParent)
    , m_port(AGENTBRIDGE_DEFAULT_PORT)
    , m_apiKey(AGENTBRIDGE_DEFAULT_API_KEY)
    , m_enabled(false)
{
    s_instance = this;
}

CAgentBridgeDlg::~CAgentBridgeDlg()
{
    s_instance = NULL;
}

void CAgentBridgeDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_LOG, m_editLog);
    DDX_Control(pDX, IDC_EDIT_AGENT_PORT, m_editPort);
    DDX_Control(pDX, IDC_EDIT_API_KEY, m_editApiKey);
    DDX_Control(pDX, IDC_CHECK_ENABLED, m_chkEnabled);
    DDX_Control(pDX, IDC_BUTTON_START, m_btnStart);
    DDX_Control(pDX, IDC_BUTTON_STOP, m_btnStop);
    DDX_Control(pDX, IDC_BUTTON_CHECK, m_btnCheck);
}

BEGIN_MESSAGE_MAP(CAgentBridgeDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BUTTON_START, &CAgentBridgeDlg::OnBnClickedButtonStart)
    ON_BN_CLICKED(IDC_BUTTON_STOP, &CAgentBridgeDlg::OnBnClickedButtonStop)
    ON_BN_CLICKED(IDC_BUTTON_SAVE, &CAgentBridgeDlg::OnBnClickedButtonSave)
    ON_BN_CLICKED(IDC_BUTTON_CHECK, &CAgentBridgeDlg::OnBnClickedButtonCheck)
    ON_MESSAGE(WM_AGENTBRIDGE_LOG_UPDATE, &CAgentBridgeDlg::OnLogUpdate)
END_MESSAGE_MAP()

BOOL CAgentBridgeDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // 加载当前配置
    m_port = g_AgentBridge.GetPort();
    m_apiKey = g_AgentBridge.GetApiKey();
    m_enabled = g_AgentBridge.IsEnabled();

    // 设置控件值
    m_editPort.SetWindowTextW(CString(std::to_wstring(m_port).c_str()));
    m_editApiKey.SetWindowTextW(m_apiKey);
    m_chkEnabled.SetCheck(m_enabled ? BST_CHECKED : BST_UNCHECKED);

    UpdateUI();
    AddLog(_T("AgentBridge 配置对话框已初始化"));

    return TRUE;
}

void CAgentBridgeDlg::UpdateUI()
{
    bool running = g_AgentBridge.IsRunning();

    m_btnStart.EnableWindow(!running);
    m_btnStop.EnableWindow(running);
    m_editPort.EnableWindow(!running);
    m_editApiKey.EnableWindow(!running);
    m_chkEnabled.EnableWindow(!running);
}

void CAgentBridgeDlg::AddLog(const CString& message)
{
    // 获取当前时间
    CTime now = CTime::GetCurrentTime();
    CString timeStr = now.Format(_T("%H:%M:%S"));

    CString logLine;
    logLine.Format(_T("[%s] %s\r\n"), timeStr, message);

    m_logMessages += logLine;

    // 更新日志编辑框
    if (m_editLog.m_hWnd) {
        m_editLog.SetWindowTextW(m_logMessages);
        // 滚动到底部
        m_editLog.LineScroll(m_editLog.GetLineCount() - 1);
    }
}

LRESULT CAgentBridgeDlg::OnLogUpdate(WPARAM wParam, LPARAM lParam)
{
    CString* pMessage = (CString*)wParam;
    if (pMessage) {
        AddLog(*pMessage);
        delete pMessage;
    }
    return 0;
}

void CAgentBridgeDlg::OnBnClickedButtonStart()
{
    // 读取配置
    CString portStr;
    m_editPort.GetWindowTextW(portStr);
    m_port = _wtoi(portStr);

    m_editApiKey.GetWindowTextW(m_apiKey);
    m_enabled = (m_chkEnabled.GetCheck() == BST_CHECKED);

    // 应用配置
    g_AgentBridge.SetPort(m_port);
    g_AgentBridge.SetApiKey(m_apiKey);
    g_AgentBridge.SetEnabled(m_enabled);

    // 设置 MainFrm 指针
    g_AgentBridge.SetMainFrame(AfxGetMainWnd());

    // 启动
    if (g_AgentBridge.Start()) {
        AddLog(_T("AgentBridge 启动成功"));
        UpdateUI();
    } else {
        AddLog(_T("AgentBridge 启动失败，请检查端口是否被占用"));
    }
}

void CAgentBridgeDlg::OnBnClickedButtonStop()
{
    g_AgentBridge.Stop();
    AddLog(_T("AgentBridge 已停止"));
    UpdateUI();
}

void CAgentBridgeDlg::OnBnClickedButtonSave()
{
    // 保存到注册表
    CRegKey key;
    LPCTSTR data_Set = _T("Software\\TemcoControls\\T3000\\AgentBridge");
    if (key.Create(HKEY_CURRENT_USER, data_Set) == ERROR_SUCCESS) {
        key.SetDWORDValue(_T("Port"), m_port);
        key.SetStringValue(_T("ApiKey"), m_apiKey);
        key.SetDWORDValue(_T("Enabled"), m_enabled ? 1 : 0);
        AddLog(_T("配置已保存到注册表"));
    } else {
        AddLog(_T("保存配置失败"));
    }
}

void CAgentBridgeDlg::OnBnClickedButtonCheck()
{
    AddLog(_T("=== AgentBridge 状态检查 ==="));

    CString status;
    status.Format(_T("运行状态: %s"), g_AgentBridge.IsRunning() ? _T("运行中") : _T("已停止"));
    AddLog(status);

    status.Format(_T("HTTP 端口: %d"), g_AgentBridge.GetPort());
    AddLog(status);

    status.Format(_T("WebSocket 端口: %d"), g_AgentBridge.GetPort() + 1);
    AddLog(status);

    status.Format(_T("MCP 端口: %d"), g_AgentBridge.GetPort() + 2);
    AddLog(status);

    status.Format(_T("API Key: %s"), g_AgentBridge.GetApiKey().IsEmpty() ? _T("未配置") : _T("已配置"));
    AddLog(status);

    int clientCount = 0;
    if (g_AgentBridge.GetWebSocketServer()) {
        clientCount = g_AgentBridge.GetWebSocketServer()->GetClientCount();
    }
    status.Format(_T("WebSocket 客户端: %d"), clientCount);
    AddLog(status);

    int deviceCount = 0;
    if (g_AgentBridge.GetMainFrame()) {
        CMainFrame* pFrame = static_cast<CMainFrame*>(g_AgentBridge.GetMainFrame());
        deviceCount = (int)pFrame->m_product.size();
    }
    status.Format(_T("设备数量: %d"), deviceCount);
    AddLog(status);

    AddLog(_T("========================"));
}

// 静态日志回调函数
void CAgentBridgeDlg::LogCallback(const CString& message)
{
    if (s_instance && s_instance->m_editLog.m_hWnd) {
        CString* pMsg = new CString(message);
        ::PostMessage(s_instance->m_hWnd, WM_AGENTBRIDGE_LOG_UPDATE, (WPARAM)pMsg, 0);
    }
}
