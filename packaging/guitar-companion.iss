; Inno Setup script for the Guitar Companion Windows installer.
;
; Build the app first (Release), then:
;   iscc packaging\guitar-companion.iss
; The .exe lands in packaging\output\.
;
; Nothing from the repository is bundled beyond the two build artefacts and the
; licence: the WebView2 loader is linked into the executable, and no
; third-party plugin binary ships here (the in-app catalogue downloads those
; from each project's own release).

#define AppName        "Guitar Companion"
#define AppVersion     "0.1"
#define AppPublisher   "Rapha"
#define ExeName        "Guitar Companion.exe"
#define Vst3Name       "Guitar Companion.vst3"
#define ArtefactsDir   "..\build\GuitarCompanion_artefacts\Release"

[Setup]
AppId={{7B3A1C64-4F2E-4E8B-9C1D-2E5A6F0B93D1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion=0.1.0.0
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=output
OutputBaseFilename=Guitar-Companion-{#AppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The plugin and the app are x64 only - NAM Core and the JUCE build are 64-bit.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Program Files and the shared VST3 folder both need elevation.
PrivilegesRequired=admin
UninstallDisplayName={#AppName} {#AppVersion}
AppComments=Beta (v0.1) - please report anything that breaks.

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Types]
Name: "full";   Description: "Application and VST3 plugin"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "app";  Description: "{#AppName} (standalone application)"; Types: full custom; Flags: fixed
Name: "vst3"; Description: "VST3 plugin (for your DAW)";          Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#ArtefactsDir}\Standalone\{#ExeName}"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\LICENSE";                           DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\THIRD_PARTY.md";                    DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\README.md";                         DestDir: "{app}"; Components: app; Flags: ignoreversion
; The VST3 is a BUNDLE (a folder), so the whole tree goes.
Source: "{#ArtefactsDir}\VST3\{#Vst3Name}\*"; DestDir: "{commoncf64}\VST3\{#Vst3Name}"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}";                Filename: "{app}\{#ExeName}"; Components: app
Name: "{group}\Licence (AGPLv3)";          Filename: "{app}\LICENSE";    Components: app
Name: "{autodesktop}\{#AppName}";          Filename: "{app}\{#ExeName}"; Components: app; Tasks: desktopicon

[Run]
Filename: "{app}\{#ExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The VST3 bundle folder is left behind empty otherwise.
Type: dirifempty; Name: "{commoncf64}\VST3\{#Vst3Name}"

[Code]
{ The Tone Store browses TONE3000 in an embedded browser, which needs the
  WebView2 Runtime. Windows 11 ships it; Windows 10 usually has it through
  Edge, but not always. Missing it is NOT fatal - the store falls back to the
  system browser - so this informs and carries on rather than blocking. }
function WebView2Installed: Boolean;
var
  Version: String;
begin
  Result :=
    RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) or
    RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) or
    RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version);
  if Result then
    Result := (Version <> '') and (Version <> '0.0.0.0');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and (not WebView2Installed) then
    MsgBox('The Microsoft Edge WebView2 Runtime was not found on this PC.'#13#10#13#10 +
           'Guitar Companion works without it, but the Tone Store will open ' +
           'TONE3000 in your normal browser instead of inside the app.'#13#10#13#10 +
           'It comes with Windows 11. On Windows 10 you can install it from:'#13#10 +
           'https://developer.microsoft.com/microsoft-edge/webview2/',
           mbInformation, MB_OK);
end;
