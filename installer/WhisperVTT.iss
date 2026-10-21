; Inno Setup script for the WhisperVTT installer.
;
; Built by the `installer` CMake target, which stages `cmake --install`
; output and passes AppVersion, SourceDir and OutputDir.  The installer:
;   - installs per user, with no administrator prompt,
;   - adds Start menu (and optionally desktop and sign-in) entries,
;   - can download a speech model from Hugging Face, checked against its
;     published SHA-256.  This download is the installer's only network
;     use; the app itself has no networking code.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z (the CMake `installer` target does this)
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<staged install folder>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define ModelUrl  "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"
#define RunKey    "Software\Microsoft\Windows\CurrentVersion\Run"

[Setup]
AppId={{833EBC2E-CABE-4655-9357-37DB5FA7EA1A}
AppName=WhisperVTT
AppVersion={#AppVersion}
AppVerName=WhisperVTT {#AppVersion}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\WhisperVTT
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern dynamic
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\WhisperVTT.exe
UninstallDisplayName=WhisperVTT
AppMutex=Local\WhisperVTT-SingleInstance
OutputDir={#OutputDir}
OutputBaseFilename=WhisperVTT-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes

[Tasks]
Name: desktopicon; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: autostart;   Description: "&Start WhisperVTT when I sign in to Windows"

[InstallDelete]
; ggml loads every backend DLL it finds next to the exe, so drop any left
; over from a previous version before installing this one.
Type: files; Name: "{app}\ggml*.dll"

[Files]
; Everything except the portable-mode models folder: an installed copy
; keeps its models in %APPDATA%\WhisperVTT\models.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "\models"; \
    Flags: recursesubdirs ignoreversion
; The model downloaded on the "Speech model" page, if any.  It is kept on
; uninstall unless the user asks for it to be removed.
Source: "{tmp}\ggml-*.bin"; DestDir: "{userappdata}\WhisperVTT\models"; \
    Flags: external skipifsourcedoesntexist ignoreversion uninsneveruninstall

[Icons]
Name: "{autoprograms}\WhisperVTT"; Filename: "{app}\WhisperVTT.exe"
Name: "{autodesktop}\WhisperVTT"; Filename: "{app}\WhisperVTT.exe"; Tasks: desktopicon

[Registry]
; The same value the app's "Start with Windows" setting writes.
Root: HKCU; Subkey: "{#RunKey}"; ValueType: string; ValueName: "WhisperVTT"; \
    ValueData: """{app}\WhisperVTT.exe"""; Tasks: autostart

[Run]
Filename: "{app}\WhisperVTT.exe"; Description: "Start WhisperVTT now"; \
    Flags: nowait postinstall skipifsilent

[Code]
const
  TurboFile = 'ggml-large-v3-turbo.bin';
  TurboHash = '1fc70f774d38eb169993ac391eea357ef47c88757ef72ee5943879b7e8e2bc69';
  SmallFile = 'ggml-small.en.bin';
  SmallHash = 'c6138d6d58ecc8322097e0f987c32f1be8bb0a18532a3f88f734d1bbf9c41e5d';

var
  ModelPage: TInputOptionWizardPage;
  DownloadPage: TDownloadWizardPage;

function ModelsDir: String;
begin
  Result := ExpandConstant('{userappdata}\WhisperVTT\models');
end;

function HasModel: Boolean;
var
  F: TFindRec;
begin
  Result := FindFirst(ModelsDir + '\*.bin', F);
  if Result then FindClose(F);
end;

// A best guess at whether the PC has a dedicated graphics card, used
// only to preselect a model.  Integrated graphics report names like
// "Intel(R) UHD Graphics" or "AMD Radeon(TM) Graphics".
function HasDedicatedGpu: Boolean;
var
  Locator, Wmi, Items, Item: Variant;
  Adapter: String;
  I: Integer;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Wmi := Locator.ConnectServer('.', 'root\CIMV2');
    Items := Wmi.ExecQuery('SELECT Name FROM Win32_VideoController');
    for I := 0 to Items.Count - 1 do begin
      Item := Items.ItemIndex(I);
      Adapter := Item.Name;
      Adapter := Lowercase(Adapter);
      if (Pos('nvidia', Adapter) > 0) or (Pos('radeon rx', Adapter) > 0) or
         (Pos('radeon pro', Adapter) > 0) or (Pos('arc(tm) a', Adapter) > 0) or
         (Pos('arc(tm) b', Adapter) > 0) then
        Result := True;
    end;
  except
    Log('Could not query graphics adapters: ' + GetExceptionMessage);
  end;
end;

procedure InitializeWizard;
begin
  ModelPage := CreateInputOptionPage(wpSelectTasks,
    'Speech model',
    'WhisperVTT needs a speech model to turn your voice into text.',
    'Setup can download one now from Hugging Face, where the Whisper models ' +
    'are published. This is the only download: WhisperVTT itself never ' +
    'connects to the internet. You can add or switch models later.',
    True, False);
  ModelPage.Add('Large v3 Turbo (1.6 GB): the most accurate; best with a graphics card');
  ModelPage.Add('Small English (488 MB): accurate and quick on any PC');
  ModelPage.Add('Don''t download a model now');
  if HasDedicatedGpu then
    ModelPage.SelectedValueIndex := 0
  else
    ModelPage.SelectedValueIndex := 1;

  DownloadPage := CreateDownloadPage('Downloading the speech model',
    'This can take a few minutes, depending on your connection.', nil);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // Upgrades and reinstalls keep the models already there.
  Result := (PageID = ModelPage.ID) and HasModel;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID <> wpReady) or HasModel or (ModelPage.SelectedValueIndex = 2) then
    Exit;

  DownloadPage.Clear;
  if ModelPage.SelectedValueIndex = 0 then
    DownloadPage.Add('{#ModelUrl}' + TurboFile, TurboFile, TurboHash)
  else
    DownloadPage.Add('{#ModelUrl}' + SmallFile, SmallFile, SmallHash);
  DownloadPage.Show;
  try
    try
      DownloadPage.Download;   // into {tmp}; [Files] copies it into place
    except
      if DownloadPage.AbortedByUser then
        Log('Model download cancelled.')
      else
        Log('Model download failed: ' + GetExceptionMessage);
      // Yes: install without a model (the app explains how to add one).
      // No: stay on this page to try again.
      Result := SuppressibleMsgBox(
        'The speech model wasn''t downloaded.' + #13#10#13#10 +
        'Install WhisperVTT without it? It will show you how to add a ' +
        'model when it starts.', mbConfirmation, MB_YESNO, IDYES) = IDYES;
    end;
  finally
    DownloadPage.Hide;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  // The app may have added this itself ("Start with Windows").
  RegDeleteValue(HKCU, '{#RunKey}', 'WhisperVTT');
  if DirExists(ExpandConstant('{userappdata}\WhisperVTT')) and
     (SuppressibleMsgBox(
        'Also remove your speech models, settings and transcript log?' + #13#10#13#10 +
        ExpandConstant('{userappdata}\WhisperVTT'),
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES) then
  begin
    DelTree(ExpandConstant('{userappdata}\WhisperVTT'), True, True, True);
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\WhisperVTT');
  end;
end;
