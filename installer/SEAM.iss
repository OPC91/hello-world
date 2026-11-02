; SEAM by Exnilo - Windows installer (Inno Setup 6).
; Build:  ISCC.exe /DAppVersion=1.0.0 /DBuildDir=..\build\seam\plugin\SEAM_artefacts\Release installer\SEAM.iss
; Output: installer\Output\SEAM-Windows-Setup.exe
;
; Installs the VST3 to C:\Program Files\Common Files\VST3\SEAM.vst3 (the folder every VST3 host scans) and, optionally,
; the standalone app. Presets and the licence live in %APPDATA%\Exnilo\SEAM and are left in place on uninstall.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\seam\plugin\SEAM_artefacts\Release"
#endif

[Setup]
; Never change AppId: Windows uses it to find the existing installation for upgrades and uninstall.
AppId={{07AC2EDF-4FA9-5CF4-A265-FF22F75572AF}
AppName=SEAM
AppVersion={#AppVersion}
AppVerName=SEAM {#AppVersion}
AppPublisher=Exnilo
AppPublisherURL=https://exnilo.co.uk
AppSupportURL=https://exnilo.co.uk/support/
DefaultDirName={autopf}\Exnilo\SEAM
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=SEAM-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=SEAM by Exnilo
UninstallDisplayIcon={app}\SEAM.exe
CloseApplications=yes

[Types]
Name: "full"; Description: "VST3 plugin and standalone app"
Name: "vst3"; Description: "VST3 plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "SEAM VST3 plugin"; Types: full vst3 custom; Flags: fixed
Name: "standalone"; Description: "SEAM standalone app"; Types: full

[Files]
Source: "{#BuildDir}\VST3\SEAM.vst3\*"; DestDir: "{commoncf64}\VST3\SEAM.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#BuildDir}\Standalone\SEAM.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Exnilo\SEAM"; Filename: "{app}\SEAM.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SEAM.vst3"

[Messages]
FinishedLabel=SEAM is installed. Open your DAW, rescan plugins and load SEAM from the Exnilo folder.
