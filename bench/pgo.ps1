<#
.SYNOPSIS
    Profile-guided (PGO) release build of notepadxp.exe.

.DESCRIPTION
    1. Instrumented release build: /GENPROFILE, passed through link.exe's
       _LINK_ environment variable (vcbuild has no PGO switch of its own).
    2. Training: drives the instrumented app through the work it does most:
       startup, opening and saving every encoding and line-ending style,
       typing, selection replace, undo/redo, Find/Replace, Find Next, Go To,
       brace match, comment toggle, smart Enter, JSON format, zoom, word wrap,
       the status bar, .LOG stamping and Follow Tail.
    3. Optimized relink: /USEPROFILE against the merged profile.

    The result is build\notepadxp.exe. Training shows app windows for a few
    minutes and drives them with window messages only (no keystrokes, no
    clipboard). Your NotepadXP settings are saved first and restored after.

    The scenarios are built so the app never needs to ask anything: every
    search has a match, every session saves before it closes. A watchdog
    thread still answers any "Notepad" message box within ~50 ms and the run
    lists them at the end, so an unexpected prompt cannot stall training.

    The profile is regenerated on every run and never committed: a stored
    .pgd goes stale as the code changes and is rejected by a new toolset.

    Run from a plain shell (vcbuild locates the compiler itself):
        pwsh bench\pgo.ps1
#>
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$vcbuild = Join-Path (Split-Path -Parent $repo) 'vcbuild\vcbuild.py'
$exe = Join-Path $repo 'build\notepadxp.exe'
$pgoDir = Join-Path $repo 'bench\build\pgo'
$pgd = Join-Path $pgoDir 'notepadxp.pgd'
$work = Join-Path ([IO.Path]::GetTempPath()) 'notepadxp-pgo'
$settingsKey = 'HKCU\Software\NotepadXP'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class PgoWin {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetDlgItemText(IntPtr h, int id, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")] public static extern IntPtr SendText(IntPtr h, uint msg, IntPtr w, string l);

    // Synchronous send with a ceiling, so a hung app cannot hang the trainer.
    public static long Send(IntPtr h, uint msg, long w, long l, uint timeoutMs) {
        IntPtr result;
        SendMessageTimeout(h, msg, (IntPtr)w, (IntPtr)l, 0, timeoutMs, out result);
        return (long)result;
    }

    static string ClassOf(IntPtr h) { var s = new StringBuilder(64); GetClassName(h, s, s.Capacity); return s.ToString(); }
    static string TitleOf(IntPtr h) { var s = new StringBuilder(256); GetWindowText(h, s, s.Capacity); return s.ToString(); }

    public static IntPtr FindDialog(uint pid, string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner == pid && IsWindowVisible(h) && ClassOf(h) == "#32770" && TitleOf(h) == title) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindChild(IntPtr parent, string cls) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (h, l) => { if (ClassOf(h) == cls) { found = h; return false; } return true; }, IntPtr.Zero);
        return found;
    }

    // Alert watchdog: answers any "Notepad" message box of the watched
    // process by clicking No where offered, else OK, else Cancel (an OK-only
    // box gives its button id 2, not IDOK). It records each box's text once
    // and keeps clicking until the box is gone.
    public static volatile int WatchPid;
    public static readonly ConcurrentQueue<string> Alerts = new ConcurrentQueue<string>();
    static Thread watchdog;

    public static void StartWatchdog() {
        if (watchdog != null) return;
        watchdog = new Thread(() => {
            var seen = new HashSet<IntPtr>();
            while (true) {
                seen.RemoveWhere(h => !IsWindow(h));
                int pid = WatchPid;
                IntPtr box = pid == 0 ? IntPtr.Zero : FindDialog((uint)pid, "Notepad");
                if (box != IntPtr.Zero) {
                    if (seen.Add(box)) {
                        var text = new StringBuilder(512);
                        GetDlgItemText(box, 0xFFFF, text, text.Capacity);
                        Alerts.Enqueue(text.ToString().Replace("\r\n", " "));
                    }
                    foreach (int id in new[] { 7, 1, 2 }) {
                        IntPtr button = GetDlgItem(box, id);
                        if (button != IntPtr.Zero) { PostMessage(button, 0x00F5, IntPtr.Zero, IntPtr.Zero); break; }
                    }
                }
                Thread.Sleep(50);
            }
        });
        watchdog.IsBackground = true;
        watchdog.Start();
    }
}
'@

$WM_CLOSE = 0x0010
$WM_SETTEXT = 0x000C
$WM_GETTEXTLENGTH = 0x000E
$WM_CHAR = 0x0102
$WM_COMMAND = 0x0111
$EM_SETSEL = 0x00B1
$BM_CLICK = 0x00F5
$IDOK = 1
$IDCANCEL = 2

# Menu command ids (res/Resource.h).
$cmd = @{
    New = 40001; Save = 40003; Undo = 40010; Redo = 40009; Delete = 40014
    Find = 40015; FindNext = 40016; Replace = 40017; GoTo = 40018; SelectAll = 40019
    DateTime = 40020; MatchBrace = 40021; ToggleComment = 40022; WordWrap = 40030
    JsonPretty = 40032; JsonMinify = 40033; StatusBar = 40040; ZoomIn = 40041
    ZoomOut = 40042; ZoomReset = 40043; FollowTail = 40044
}

function Invoke-Build([string]$linkFlags) {
    $env:_LINK_ = $linkFlags
    Push-Location $repo
    try {
        $output = & python $vcbuild release --rebuild --verbose 2>&1 | ForEach-Object { "$_" }
        if ($LASTEXITCODE -ne 0) {
            $output | Write-Host
            throw "vcbuild failed with $linkFlags"
        }
        return $output
    } finally {
        Pop-Location
        Remove-Item Env:\_LINK_ -ErrorAction SilentlyContinue
    }
}

function Find-PgoRuntime {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC\*\bin\Hostx64\x64\pgort140.dll') |
        Sort-Object { [version]$_.Directory.Parent.Parent.Parent.Name } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

# --- Driving the app ---------------------------------------------------------

function Send-Message($hwnd, $msg, $w = 0, $l = 0) {
    [PgoWin]::Send($hwnd, $msg, $w, $l, 120000)
}

function Wait-Dialog($app, [string]$title) {
    $deadline = [DateTime]::Now.AddSeconds(15)
    while ([DateTime]::Now -lt $deadline) {
        $dialog = [PgoWin]::FindDialog($app.Process.Id, $title)
        if ($dialog -ne [IntPtr]::Zero) { return $dialog }
        Start-Sleep -Milliseconds 50
    }
    throw "dialog '$title' did not appear"
}

function Start-App([string]$file) {
    $process = if ($file) {
        Start-Process -FilePath $exe -ArgumentList "`"$file`"" -PassThru
    } else {
        Start-Process -FilePath $exe -PassThru
    }
    [PgoWin]::WatchPid = $process.Id
    [void]$process.WaitForInputIdle(30000)
    $frame = [IntPtr]::Zero
    for ($i = 0; $i -lt 100 -and $frame -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 50
        $process.Refresh()
        $frame = $process.MainWindowHandle
    }
    $app = [pscustomobject]@{ Process = $process; Frame = $frame; Edit = [IntPtr]::Zero }
    $app.Edit = [PgoWin]::FindChild($frame, 'Edit')
    return $app
}

function Stop-App($app) {
    [void][PgoWin]::PostMessage($app.Frame, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $app.Process.WaitForExit(60000)) {
        Write-Warning 'app did not exit, killed (this run adds no profile data)'
        $app.Process.Kill()
    }
}

function Invoke-Command($app, [int]$id) {
    [void](Send-Message $app.Frame $WM_COMMAND $id)
}

function Edit-Refresh($app) {
    # Word wrap recreates the edit control.
    $app.Edit = [PgoWin]::FindChild($app.Frame, 'Edit')
}

function Get-Length($app) {
    Send-Message $app.Edit $WM_GETTEXTLENGTH
}

function Set-Selection($app, [long]$start, [long]$end) {
    [void](Send-Message $app.Edit $EM_SETSEL $start $end)
}

function Send-Text($app, [string]$text) {
    foreach ($ch in $text.ToCharArray()) {
        [void](Send-Message $app.Edit $WM_CHAR ([int]$ch))
    }
}

function Set-DialogText($dialog, [int]$id, [string]$text) {
    [void][PgoWin]::SendText([PgoWin]::GetDlgItem($dialog, $id), $WM_SETTEXT, [IntPtr]::Zero, $text)
}

function Invoke-Click($dialog, [int]$id) {
    [void](Send-Message ([PgoWin]::GetDlgItem($dialog, $id)) $BM_CLICK)
}

# $key occurs on every line of every corpus file, in exactly this case, so
# each search below starts where a match is guaranteed. The app reads the
# direction and case options only when a search runs, so the round ends on a
# downward search and F3 then continues downward.
function Invoke-FindDialog($app, [string]$key) {
    Set-Selection $app 0 0
    [void][PgoWin]::PostMessage($app.Frame, $WM_COMMAND, [IntPtr]$cmd.Find, [IntPtr]::Zero)
    $find = Wait-Dialog $app 'Find'
    Set-DialogText $find 0x480 $key
    foreach ($i in 1..3) { Invoke-Click $find $IDOK }      # Down, ignore case.
    $length = Get-Length $app
    Set-Selection $app $length $length
    Invoke-Click $find 0x420                               # Up, from the end.
    foreach ($i in 1..3) { Invoke-Click $find $IDOK }
    Invoke-Click $find 0x421                               # Down, match case, from the top.
    Invoke-Click $find 0x410
    Set-Selection $app 0 0
    foreach ($i in 1..3) { Invoke-Click $find $IDOK }
    [void][PgoWin]::PostMessage($find, $WM_COMMAND, [IntPtr]$IDCANCEL, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    Set-Selection $app 0 0
    foreach ($i in 1..3) { Invoke-Command $app $cmd.FindNext }   # F3.
}

function Invoke-ReplaceDialog($app, [string]$key, [string]$replacement) {
    Set-Selection $app 0 0
    [void][PgoWin]::PostMessage($app.Frame, $WM_COMMAND, [IntPtr]$cmd.Replace, [IntPtr]::Zero)
    $replace = Wait-Dialog $app 'Replace'
    Set-DialogText $replace 0x480 $key
    Set-DialogText $replace 0x481 $replacement
    Invoke-Click $replace $IDOK                            # Find Next.
    foreach ($i in 1..3) { Invoke-Click $replace 0x400 }   # Replace (and find next).
    Invoke-Click $replace 0x401                            # Replace All.
    [void][PgoWin]::PostMessage($replace, $WM_COMMAND, [IntPtr]$IDCANCEL, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}

function Invoke-GoTo($app, [int]$line) {
    [void][PgoWin]::PostMessage($app.Frame, $WM_COMMAND, [IntPtr]$cmd.GoTo, [IntPtr]::Zero)
    $goTo = Wait-Dialog $app 'Goto line'
    Set-DialogText $goTo 258 "$line"
    [void][PgoWin]::PostMessage($goTo, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
}

# One editing session: the typing, undo, search and save paths every file
# type exercises, plus the extras its kind enables. Saved before closing,
# so no save prompt.
function Invoke-Session([string]$file, [hashtable]$opts) {
    Write-Host "[pgo]   $(Split-Path -Leaf $file)"
    $app = Start-App $file
    if ($opts.Json) {                                    # While the JSON is valid.
        Invoke-Command $app $cmd.JsonPretty
        Invoke-Command $app $cmd.JsonMinify
        Invoke-Command $app $cmd.JsonPretty
    }
    Invoke-Command $app $cmd.StatusBar                   # Caret Ln/Col updates.

    $sentence = 'The quick brown fox jumps over the lazy dog, then naps.'
    $length = Get-Length $app
    foreach ($at in @($length, [long]($length / 2), [long]($length / 7), 0)) {
        Set-Selection $app $at $at
        foreach ($i in 1..3) { Send-Text $app "$sentence`r" }
        Send-Text $app "`t$sentence"
        Send-Text $app ("`b" * 12)
    }
    foreach ($i in 1..6) {                               # Typing over a selection.
        Set-Selection $app (100 * $i) (100 * $i + 7)
        Send-Text $app 'abc'
    }
    Set-Selection $app 40 60
    Invoke-Command $app $cmd.Delete
    Invoke-Command $app $cmd.DateTime
    foreach ($i in 1..30) { Invoke-Command $app $cmd.Undo }
    foreach ($i in 1..20) { Invoke-Command $app $cmd.Redo }

    Invoke-FindDialog $app $opts.Find
    Invoke-ReplaceDialog $app $opts.Find $opts.ReplaceWith
    Invoke-Command $app $cmd.Undo
    Invoke-GoTo $app 25

    if ($opts.Code) {
        foreach ($pos in 10, 200, 900, 2500) {
            Set-Selection $app $pos $pos
            Invoke-Command $app $cmd.MatchBrace
        }
        Set-Selection $app 0 400
        Invoke-Command $app $cmd.ToggleComment
        Invoke-Command $app $cmd.ToggleComment
        Set-Selection $app 300 300
        foreach ($i in 1..5) { Send-Text $app "x = call(1)`r" }   # Smart Enter.
    }
    if ($opts.Zoom) {
        foreach ($i in 1..3) { Invoke-Command $app $cmd.ZoomIn }
        Invoke-Command $app $cmd.ZoomOut
        Invoke-Command $app $cmd.ZoomReset
    }
    if ($opts.Wrap) {
        Invoke-Command $app $cmd.WordWrap
        Edit-Refresh $app
        Set-Selection $app 50 50
        Send-Text $app $sentence
        Invoke-Command $app $cmd.WordWrap
        Edit-Refresh $app
    }
    Invoke-Command $app $cmd.StatusBar
    Invoke-Command $app $cmd.Save
    Stop-App $app
}

# --- Training corpus -----------------------------------------------------------

function New-Text([string]$line, [int]$targetChars, [string]$newline) {
    $sb = [Text.StringBuilder]::new($targetChars + 200)
    $n = 0
    while ($sb.Length -lt $targetChars) {
        [void]$sb.Append($line.Replace('#', "$n")).Append($newline)
        $n++
    }
    $sb.ToString()
}

function New-Corpus {
    $e = [char]0xE9
    $a = [char]0xE2
    $log = '2026-07-23 10:00:01.123 INFO  scheduler - task # finished in 42 ms, queue depth 7'
    $accented = "2026-07-23 10:00:01.123 INFO  caf$e - t${a}che # termin${e}e en 42 ms, file 7"
    $ascii = [Text.ASCIIEncoding]::new()
    $files = [ordered]@{}

    $files['log.txt'] = @{ Text = (New-Text $log 3MB "`r`n"); Enc = $ascii }
    $files['unix.log'] = @{ Text = (New-Text $log 1MB "`n"); Enc = $ascii }
    $files['mac.txt'] = @{ Text = (New-Text $log 200KB "`r"); Enc = $ascii }
    $files['utf8bom.txt'] = @{ Text = (New-Text $accented 1MB "`r`n"); Enc = [Text.UTF8Encoding]::new($true) }
    $files['utf8.md'] = @{ Text = (New-Text $accented 1MB "`n"); Enc = [Text.UTF8Encoding]::new($false) }
    $files['utf16le.txt'] = @{ Text = (New-Text $accented 1MB "`r`n"); Enc = [Text.UnicodeEncoding]::new($false, $true) }
    $files['utf16be.txt'] = @{ Text = (New-Text $accented 1MB "`r`n"); Enc = [Text.UnicodeEncoding]::new($true, $true) }

    $element = '{"id":#,"name":"component-#","ok":true,"tags":["a","b"],"values":[1,2.5e3,null]}'
    $json = '{"items":[' + (New-Text $element 1500KB ',').TrimEnd(',') + ']}'
    $files['data.json'] = @{ Text = $json; Enc = [Text.UTF8Encoding]::new($false) }

    $py = "def handler_#(event, context):`n    # Route the event.`n    if event.get('kind') == 'x':`n        return {'ok': True, 'n': [1, 2, (3, 4)]}`n    return None`n"
    $files['code.py'] = @{ Text = (New-Text $py 150KB "`n"); Enc = [Text.UTF8Encoding]::new($false) }
    $cpp = "int Handler#(const Event& e) {`r`n    // Route the event.`r`n    if (e.kind == Kind::X) {`r`n        return Call({1, 2}, [&] { return e.n; });`r`n    }`r`n    return 0;`r`n}"
    $files['code.cpp'] = @{ Text = (New-Text $cpp 150KB "`r`n"); Enc = $ascii }

    $files['journal.txt'] = @{ Text = ".LOG`r`n" + (New-Text $log 20KB "`r`n"); Enc = $ascii }
    $files['nul.txt'] = @{ Text = "before`0after`0`0end`r`n" + (New-Text $log 20KB "`r`n"); Enc = $ascii }
    $files['tail.log'] = @{ Text = (New-Text $log 50KB "`r`n"); Enc = $ascii }

    foreach ($name in $files.Keys) {
        [IO.File]::WriteAllText((Join-Path $work $name), $files[$name].Text, $files[$name].Enc)
    }
}

function Invoke-Training {
    foreach ($i in 1..4) {                                # Startup and exit.
        Stop-App (Start-App $null)
    }

    $plain = @{ Find = 'queue'; ReplaceWith = 'stack' }
    $accented = @{ Find = 'file'; ReplaceWith = 'pile' }
    Invoke-Session (Join-Path $work 'log.txt') ($plain + @{ Zoom = $true })
    Invoke-Session (Join-Path $work 'unix.log') ($plain + @{ Wrap = $true })
    Invoke-Session (Join-Path $work 'mac.txt') $plain
    Invoke-Session (Join-Path $work 'utf8bom.txt') ($accented + @{ Wrap = $true })
    Invoke-Session (Join-Path $work 'utf8.md') $accented
    Invoke-Session (Join-Path $work 'utf16le.txt') $accented
    Invoke-Session (Join-Path $work 'utf16be.txt') $accented
    Invoke-Session (Join-Path $work 'data.json') @{ Find = 'component'; ReplaceWith = 'part'; Json = $true }
    Invoke-Session (Join-Path $work 'code.py') @{ Find = 'event'; ReplaceWith = 'evt'; Code = $true }
    Invoke-Session (Join-Path $work 'code.cpp') @{ Find = 'Handler'; ReplaceWith = 'Route'; Code = $true; Wrap = $true }
    Invoke-Session (Join-Path $work 'nul.txt') $plain

    Write-Host '[pgo]   journal.txt (.LOG), new document'
    $app = Start-App (Join-Path $work 'journal.txt')     # Timestamp appended on open.
    Invoke-Command $app $cmd.Save
    Invoke-Command $app $cmd.New
    Send-Text $app 'scratch text in a new document'
    Invoke-Command $app $cmd.SelectAll
    Invoke-Command $app $cmd.Delete                      # Empty and untitled: no prompt.
    Stop-App $app

    Write-Host '[pgo]   tail.log (Follow Tail)'
    $tail = Join-Path $work 'tail.log'
    $app = Start-App $tail
    Invoke-Command $app $cmd.FollowTail
    foreach ($i in 1..4) {
        [IO.File]::AppendAllText($tail, (New-Text '2026-07-23 10:00:02.000 WARN  appended line #' 2KB "`r`n"))
        Start-Sleep -Milliseconds 700
    }
    Invoke-Command $app $cmd.FollowTail
    Stop-App $app
}

# --- Main ----------------------------------------------------------------------

$pgort = Find-PgoRuntime
if (-not $pgort) { throw 'pgort140.dll not found (Visual Studio C++ tools required)' }
New-Item -ItemType Directory -Force $pgoDir | Out-Null
Remove-Item (Join-Path $pgoDir '*') -Force -ErrorAction SilentlyContinue

Write-Host '[pgo] 1/3 instrumented build'
[void](Invoke-Build "/GENPROFILE:PGD=$pgd")
Copy-Item $pgort (Split-Path -Parent $exe)

Write-Host '[pgo] 2/3 training'
$env:VCPROFILE_PATH = $pgoDir   # .pgc files next to the .pgd, safe from the rebuild.
New-Item -ItemType Directory -Force $work | Out-Null
$backup = Join-Path $work 'settings.reg'
& reg.exe query $settingsKey *> $null
$hadSettings = $LASTEXITCODE -eq 0
if ($hadSettings) { & reg.exe export $settingsKey $backup /y *> $null }
[PgoWin]::StartWatchdog()
try {
    New-Corpus
    Invoke-Training
} finally {
    [PgoWin]::WatchPid = 0
    Remove-Item Env:\VCPROFILE_PATH -ErrorAction SilentlyContinue
    & reg.exe delete $settingsKey /f *> $null
    if ($hadSettings) { & reg.exe import $backup *> $null }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
$alerts = [PgoWin]::Alerts.ToArray()
if ($alerts.Count -gt 0) {
    Write-Warning "$($alerts.Count) unexpected message box(es) answered by the watchdog:"
    $alerts | Group-Object | ForEach-Object { Write-Warning "  $($_.Count)x $($_.Name)" }
} else {
    Write-Host '[pgo]   no message boxes'
}
$runs = @(Get-ChildItem (Join-Path $pgoDir '*.pgc')).Count
if ($runs -eq 0) { throw 'training produced no profile data (.pgc files)' }
Write-Host "[pgo]   $runs profiled runs"

Write-Host '[pgo] 3/3 optimized build'
$output = Invoke-Build "/USEPROFILE:PGD=$pgd"
$output | Where-Object { $_ -match 'profile data|compiled for speed' } | ForEach-Object { Write-Host "[pgo]   $($_.Trim())" }
Write-Host "[pgo] done: $exe"
