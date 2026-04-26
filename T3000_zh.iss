; T3000 建筑自动化系统 - 中文安装包脚本
; 使用 Inno Setup 编译

#define MyAppName "T3000 建筑自动化系统"
#define MyAppVersion "3.2.1"
#define MyAppPublisher "Temco Controls"
#define MyAppURL "https://www.temcocontrols.com"
#define MyAppExeName "T3000.exe"

[Setup]
AppId={{A3B5C7D9-E1F2-4567-89AB-CDEF01234567}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=T3000InstallShield\LICENSE.txt
OutputDir=Output
OutputBaseFilename=T3000_建筑自动化系统_Setup
SetupIconFile=T3000InstallShield\T3000.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x86
ArchitecturesInstallIn64BitMode=x86

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; 主程序
Source: "T3000InstallShield\T3000.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "T3000InstallShield\T3000Controls.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "T3000InstallShield\ModbusDllforVc.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "T3000InstallShield\FlexSlideBar.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "T3000InstallShield\sqlite3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "T3000InstallShield\BACnet_Stack_Library.dll"; DestDir: "{app}"; Flags: ignoreversion

; 中文语言包
Source: "T3000InstallShield\LanguageLocale_zh.ini"; DestDir: "{app}"; Flags: ignoreversion

; 工具程序
Source: "T3000InstallShield\Tools\ISP.exe"; DestDir: "{app}\Tools"; Flags: ignoreversion

; 文档
Source: "T3000InstallShield\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "Documentation\*"; DestDir: "{app}\Documentation"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:ProgramOnTheWeb,{#MyAppName}}"; Filename: "{#MyAppURL}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\user_data"
