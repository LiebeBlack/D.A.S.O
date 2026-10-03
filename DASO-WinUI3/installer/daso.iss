; ============================================================================
;  D.A.S.O Nativo - instalador Inno Setup
;
;  Uso:
;     ISCC.exe installer\daso.iss
;     ISCC.exe /DPublishDir="..\x64\Release" installer\daso.iss
;
;  Si no se pasa PublishDir usa ..\x64\Release, que es donde MSBuild deja el
;  ejecutable autocontenido (ver <WindowsAppSDKSelfContained> en daso.vcxproj).
;
;  Esto empaqueta, NO compila. Para compilar antes, mira el workflow de CI.
; ============================================================================

#ifndef PublishDir
  #define PublishDir "..\x64\Release"
#endif

#define AppName        "D.A.S.O"
#define AppNameFull    "D.A.S.O - Debloater Android Script Optimizer"
#define AppVersion     "1.0.0"
#define AppPublisher   "Liebe Black"
#define AppExeName     "daso.exe"
#define AppMutex       "Local\DASO.Nativo.SingleInstance"

[Setup]
AppId={{7B3F1E2A-9C4D-4A6E-8B21-5D0C7E9F3A18}
AppName={#AppNameFull}
AppVersion={#AppVersion}
AppVerName={#AppNameFull} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}

; Carpeta por usuario. Un debloater no es un programa de sistema: instalarlo en
; Program Files exigiría elevación y D.A.S.O no la pide nunca.
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; UninstallDisplayName no debe colisionar con el script batch original.
UninstallDisplayName={#AppNameFull} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExeName}
SetupIconFile=..\assets\app_icon.ico

OutputDir=output
OutputBaseFilename=D.A.S.O-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; x64. La app es de 64 bits porque el Windows App SDK se distribuye asi.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041

[Languages]
Name: "es"; MessagesFile: "compiler:Default.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.CreateDesktopIcon=Crear acceso directo en el &Escritorio
en.CreateDesktopIcon=Create a &Desktop shortcut
es.LaunchApp=Iniciar D.A.S.O al terminar
en.LaunchApp=Launch D.A.S.O when finished
es.AdminWarning=D.A.S.O no necesita permisos de administrador.%n%nSe está instalando en la carpeta del sistema, así que Windows ha pedido elevación para copiar los archivos. La aplicación en sí no usa esos permisos.
en.AdminWarning=D.A.S.O does not need administrator rights.%n%nIt is being installed into the system folder, so Windows asked for elevation to copy the files. The application itself never uses those rights.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; El ejecutable es autocontenido: trae dentro el Windows App Runtime, asi que
; no hay que instalar nada mas ni depender de que el usuario lo tenga.
Source: "{#PublishDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Todo lo que MSBuild copia junto al ejecutable: DLL del runtime, .pri,
; recursos y el propio runtime autocontenido. No se enumera a mano para que
; anadir un fichero nuevo al proyecto no rompa el instalador.
Source: "{#PublishDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppNameFull}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Los manifiestos quedan: son el registro de lo que se deshabilito y son la
; unica forma de revertir. No se borran al desinstalar.
Type: filesandordirs; Name: "{userappdata}\{#AppName}\logs"

[Code]
// Aviso cuando el destino acaba en Program Files: el usuario acaba de elegir
// la elevacion, y conviene que sepa que la app no la necesita.
function InitializeSetup(): Boolean;
begin
  Result := True;
  if Pos(ExpandConstant('{common}'), WizardDirValue) = 1 then
  begin
    if MsgBox(CustomMessage('AdminWarning'), mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;

// Comprobar que hay un ejecutable que empaquetar antes de perder tiempo
// comprimiendo: si MSBuild no ha corrido, el fallo debe ser explicito.
function InitializeWizard(): Boolean;
var
  Found: Boolean;
begin
  Result := True;
  Found := FileExists(ExpandConstant('{#PublishDir}\{#AppExeName}'));
  if not Found then
  begin
    MsgBox('No se encontró ' + '{#PublishDir}\{#AppExeName}' + #13#10#13#10 +
            'Compila primero:' + #13#10 +
            '  msbuild daso.sln -p:Configuration=Release -p:Platform=x64' + #13#10#13#10 +
            'O define la ruta:' + #13#10 +
            '  ISCC.exe /DPublishDir="<carpeta>" installer\daso.iss',
            mbError, MB_OK);
    Result := False;
  end;
end;
