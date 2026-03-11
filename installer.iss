[Setup]
AppName=Motorcycle Forensic Data Visualizer
AppVersion=1.0.0
DefaultDirName={pf}\Motorcycle Forensic Data Visualizer
DefaultGroupName=Motorcycle Forensic Data Visualizer
OutputDir=installer_output
OutputBaseFilename=MotorcycleForensicDataVisualizer-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
SetupIconFile=assets\icon.ico

[Files]
Source: "dist\Motorcycle Forensic Data Visualizer\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\Motorcycle Forensic Data Visualizer"; Filename: "{app}\Motorcycle Forensic Data Visualizer.exe"
Name: "{commondesktop}\Motorcycle Forensic Data Visualizer"; Filename: "{app}\Motorcycle Forensic Data Visualizer.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"