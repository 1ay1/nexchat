; nexchat's Windows installer (Inno Setup 6). Built by win32/package.sh:
;   iscc /DAPPVER=0.1.0 /DSTAGE=...\dist\stage\nexchat /DOUTDIR=...\dist
;        /DSRCDIR=...\  win32\installer\nexchat.iss
; Installs per user by default (no admin prompt); can install for everyone.

#ifndef APPVER
  #define APPVER "0.0.0"
#endif

[Setup]
; never change: Windows knows the installed app by this id (upgrades,
; uninstall)
AppId={{7F3B2C61-4E8A-4A9D-9C2E-5B1D0A6E3F42}
AppName=nexchat
AppVersion={#APPVER}
AppVerName=nexchat {#APPVER}
AppPublisher=nexchat
AppPublisherURL=https://1ay1.github.io/nexchat/
AppSupportURL=https://github.com/1ay1/nexchat/issues
AppUpdatesURL=https://github.com/1ay1/nexchat/releases
AppCopyright=GPL-2.0-or-later. Based on HexChat and X-Chat.
LicenseFile={#SRCDIR}\COPYING
SetupIconFile={#SRCDIR}\data\icons\nexchat.ico
UninstallDisplayIcon={app}\bin\nexchat.exe
UninstallDisplayName=nexchat
WizardStyle=modern
DefaultDirName={autopf}\nexchat
DefaultGroupName=nexchat
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SourceDir={#STAGE}
OutputDir={#OUTDIR}
OutputBaseFilename=nexchat-{#APPVER}-windows-x64-setup
Compression=lzma2/ultra64
SolidCompression=yes
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
VersionInfoProductName=nexchat
VersionInfoDescription=nexchat setup
VersionInfoVersion={#APPVER}

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: irclinks; Description: "Open irc:// and ircs:// links with nexchat"; GroupDescription: "Links:"

[Files]
Source: "*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; an old install's plugins and loaders that this version no longer has
Type: filesandordirs; Name: "{app}\lib\nexchat\plugins"
Type: filesandordirs; Name: "{app}\lib\gdk-pixbuf-2.0"

[Icons]
Name: "{autoprograms}\nexchat"; Filename: "{app}\bin\nexchat.exe"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\nexchat"; Filename: "{app}\bin\nexchat.exe"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Registry]
; irc:// and ircs:// (HKCU when installed per user, HKLM for everyone)
Root: HKA; Subkey: "Software\Classes\irc"; ValueType: string; ValueData: "URL:IRC Protocol"; Flags: uninsdeletekey; Tasks: irclinks
Root: HKA; Subkey: "Software\Classes\irc"; ValueName: "URL Protocol"; ValueType: string; ValueData: ""; Tasks: irclinks
Root: HKA; Subkey: "Software\Classes\irc\DefaultIcon"; ValueType: string; ValueData: """{app}\bin\nexchat.exe"",0"; Tasks: irclinks
Root: HKA; Subkey: "Software\Classes\irc\shell\open\command"; ValueType: string; ValueData: """{app}\bin\nexchat.exe"" --existing --url=""%1"""; Tasks: irclinks
Root: HKA; Subkey: "Software\Classes\ircs"; ValueType: string; ValueData: "URL:IRCS Protocol"; Flags: uninsdeletekey; Tasks: irclinks
Root: HKA; Subkey: "Software\Classes\ircs"; ValueName: "URL Protocol"; ValueType: string; ValueData: ""; Tasks: irclinks
Root: HKA; Subkey: "Software\Classes\ircs\DefaultIcon"; ValueType: string; ValueData: """{app}\bin\nexchat.exe"",0"; Tasks: irclinks
Root: HKA; Subkey: "Software\Classes\ircs\shell\open\command"; ValueType: string; ValueData: """{app}\bin\nexchat.exe"" --existing --url=""%1"""; Tasks: irclinks

[Run]
Filename: "{app}\bin\nexchat.exe"; Description: "{cm:LaunchProgram,nexchat}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; settings live in %APPDATA%\nexchat and are left alone (a reinstall finds them)
Type: files; Name: "{app}\portable-mode"
