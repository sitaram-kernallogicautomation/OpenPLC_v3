; Inno Setup Script for OpenPLC Runtime Windows Installer
; This creates a single .exe installer that clients can run to install OpenPLC

#define MyAppName "OpenPLC Runtime"
#define MyAppVersion "3.0"
#define MyAppPublisher "OpenPLC Project"
#define MyAppURL "https://openplcproject.com/"
#define MyAppExeName "OpenPLC_Webserver.exe"

[Setup]
; NOTE: The value of AppId uniquely identifies this application. Do not use the same AppId value in installers for other applications.
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=dist
OutputBaseFilename=OpenPLC_Installer
SetupIconFile=
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startmenu"; Description: "Create Start Menu shortcuts"; GroupDescription: "Shortcuts"; Flags: unchecked
Name: "autostart"; Description: "Start OpenPLC automatically on Windows startup"; GroupDescription: "Startup"; Flags: unchecked
Name: "installservice"; Description: "Install as Windows Service (Recommended)"; GroupDescription: "Service"; Flags: checked

[Files]
; Python embedded distribution (if bundled)
Source: "python\*"; DestDir: "{app}\python"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: PythonNotInstalled

; OpenPLC webserver files
Source: "..\webserver\*.py"; DestDir: "{app}\webserver"; Flags: ignoreversion
Source: "..\webserver\*.db"; DestDir: "{app}\webserver"; Flags: ignoreversion
Source: "..\webserver\*.cfg"; DestDir: "{app}\webserver"; Flags: ignoreversion
Source: "..\webserver\static\*"; DestDir: "{app}\webserver\static"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\webserver\st_files\*"; DestDir: "{app}\webserver\st_files"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\webserver\scripts\*"; DestDir: "{app}\webserver\scripts"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\webserver\core\*"; DestDir: "{app}\webserver\core"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\webserver\lib\*"; DestDir: "{app}\webserver\lib"; Flags: ignoreversion recursesubdirs createallsubdirs

; Pre-compiled runtime executable (if available)
Source: "runtime\openplc.exe"; DestDir: "{app}\webserver\core"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\runtime\openplc.exe'))

; Utilities and dependencies
Source: "runtime\*.dll"; DestDir: "{app}\webserver\core"; Flags: ignoreversion
Source: "runtime\iec2c.exe"; DestDir: "{app}\webserver"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\runtime\iec2c.exe'))
Source: "runtime\st_optimizer.exe"; DestDir: "{app}\webserver"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\runtime\st_optimizer.exe'))
Source: "runtime\glue_generator.exe"; DestDir: "{app}\webserver\core"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\runtime\glue_generator.exe'))

; Installation scripts
Source: "install_runtime.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "setup_service.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "start_openplc.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "compile_program_windows.bat"; DestDir: "{app}\webserver\scripts"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\start_openplc.bat"; WorkingDir: "{app}"
Name: "{group}\OpenPLC Web Interface"; Filename: "http://localhost:8080"; IconFilename: "{app}\webserver\static\logo-openplc.png"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\start_openplc.bat"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userstartup}\{#MyAppName}"; Filename: "{app}\start_openplc.bat"; WorkingDir: "{app}"; Tasks: autostart

[Run]
Filename: "{app}\install_runtime.bat"; StatusMsg: "Setting up OpenPLC Runtime..."; Flags: runhidden waituntilterminated; BeforeInstall: BeforeInstallRuntime
Filename: "{app}\setup_service.bat"; StatusMsg: "Installing Windows Service..."; Flags: runhidden waituntilterminated; Tasks: installservice; Check: ServiceNotInstalled

[Code]
var
  PythonPath: String;
  PythonInstalled: Boolean;

function PythonNotInstalled(): Boolean;
begin
  Result := not PythonInstalled;
end;

function ServiceNotInstalled(): Boolean;
begin
  Result := True; // Always check, installer will verify
end;

procedure BeforeInstallRuntime();
begin
  // This runs before install_runtime.bat
end;

function InitializeSetup(): Boolean;
var
  PythonVersion: String;
begin
  // Check if Python is installed
  PythonInstalled := RegQueryStringValue(HKLM, 'SOFTWARE\Python\PythonCore\3.11\InstallPath', '', PythonPath) or
                     RegQueryStringValue(HKLM, 'SOFTWARE\Python\PythonCore\3.10\InstallPath', '', PythonPath) or
                     RegQueryStringValue(HKLM, 'SOFTWARE\Python\PythonCore\3.9\InstallPath', '', PythonPath) or
                     RegQueryStringValue(HKLM, 'SOFTWARE\Python\PythonCore\3.8\InstallPath', '', PythonPath);
  
  if PythonInstalled then
  begin
    PythonPath := PythonPath + 'python.exe';
  end;
  
  Result := True;
end;

function InitializeUninstall(): Boolean;
begin
  // Stop service if running
  Exec('sc', 'stop OpenPLC', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec('sc', 'delete OpenPLC', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := True;
end;
