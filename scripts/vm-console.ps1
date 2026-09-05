# Drive a running OmniOS VM through QEMU's monitor socket.
#
# This exists because the machine has no other way in. There is no ssh into a
# live session that boots straight to a fullscreen shell, and a foreground QEMU
# holds the terminal, so every interaction — pressing a key, reading the screen —
# goes through the monitor.
#
# One connection does the whole script. The monitor serves exactly one client
# and leaves the socket in CloseWait afterwards, so a second connection is
# refused for the life of the VM: opening one per keystroke works once and then
# never again.
#
#   ./scripts/vm-console.ps1 -Shot apps.png -Script 'tab', '<WAIT:2>', '<KEY:m>'
#   ./scripts/vm-console.ps1 -Shot log.png -Script '<KEY:ctrl-alt-f2>','<WAIT:3>',
#       'root','<ENTER>','omnios','<ENTER>','tail -20 /tmp/omnios-pkg.log','<ENTER>'
#
# Script entries are either literal text to type, or one of:
#   <ENTER>      press Return
#   <KEY:name>   press one QEMU key name, e.g. <KEY:tab>, <KEY:ctrl-alt-f2>
#   <WAIT:n>     wait n seconds
param(
    # Where to write the screenshot. A .png is converted from QEMU's PPM; any
    # other extension is left as the raw PPM.
    [string]$Shot = 'vm.png',

    [string[]]$Script = @(),

    [int]$Port = 4444,

    # Seconds to wait after the script before capturing. Anything that redraws —
    # a menu opening, an app starting — needs longer than it feels like it
    # should under software rendering.
    [int]$SettleSeconds = 2
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if ([System.IO.Path]::IsPathRooted($Shot)) { $shotPath = $Shot }
else { $shotPath = Join-Path (Get-Location) $Shot }

$isPng = [System.IO.Path]::GetExtension($shotPath) -eq '.png'
# QEMU only writes PPM, so a PNG is a conversion of a temporary one.
if ($isPng) { $ppmPath = [System.IO.Path]::ChangeExtension($shotPath, '.ppm') }
else { $ppmPath = $shotPath }

# Characters whose QEMU key name is not just the character itself. Anything
# missing from here is sent as-is, and an uppercase letter as shift-<lower>.
$keyNames = @{
    ' ' = 'spc';   '.' = 'dot';            '-' = 'minus';  '/' = 'slash'
    '_' = 'shift-minus';                   ':' = 'shift-semicolon'
    '~' = 'shift-grave_accent';            '=' = 'equal';  ';' = 'semicolon'
    '|' = 'shift-backslash';               '>' = 'shift-dot'
    '<' = 'shift-comma';                   ',' = 'comma'
    '(' = 'shift-9';                       ')' = 'shift-0'
    '*' = 'shift-8';                       '+' = 'shift-equal'
    '"' = 'shift-apostrophe';              "'" = 'apostrophe'
    '?' = 'shift-slash';                   '!' = 'shift-1'
    '@' = 'shift-2';                       '#' = 'shift-3'
    '$' = 'shift-4';                       '%' = 'shift-5'
    '&' = 'shift-7';                       '\' = 'backslash'
}

function Send-Text {
    param($Writer, [string]$Text)
    foreach ($ch in $Text.ToCharArray()) {
        $key = if ($keyNames.ContainsKey([string]$ch)) { $keyNames[[string]$ch] }
               elseif ($ch -cmatch '[A-Z]') { "shift-$([char]::ToLower($ch))" }
               else { [string]$ch }
        $Writer.WriteLine("sendkey $key")
        # The guest reads the virtual keyboard at its own pace; without a gap
        # between keys a login prompt drops characters.
        Start-Sleep -Milliseconds 45
    }
}

$client = New-Object System.Net.Sockets.TcpClient
try {
    $client.Connect('127.0.0.1', $Port)
} catch {
    throw ("Could not reach the QEMU monitor on port $Port. Boot with " +
           "-MonitorPort $Port, and remember the monitor accepts only one " +
           "connection for the life of the VM.")
}

try {
    $stream = $client.GetStream()
    $writer = New-Object System.IO.StreamWriter($stream)
    $writer.AutoFlush = $true
    # The monitor prints a banner first and ignores anything sent before it.
    Start-Sleep -Milliseconds 700

    foreach ($line in $Script) {
        if ($line -eq '<ENTER>') {
            $writer.WriteLine('sendkey ret'); Start-Sleep -Seconds 1
        } elseif ($line -match '^<WAIT:(\d+)>$') {
            Start-Sleep -Seconds ([int]$Matches[1])
        } elseif ($line -match '^<KEY:(.+)>$') {
            $writer.WriteLine("sendkey $($Matches[1])"); Start-Sleep -Seconds 1
        } else {
            Send-Text $writer $line
        }
    }

    Start-Sleep -Seconds $SettleSeconds
    if (Test-Path $ppmPath) { Remove-Item $ppmPath -Force }
    $writer.WriteLine("screendump $ppmPath")

    # screendump is asynchronous and a 1280x800 frame is about 3MB, so the file
    # appears before it is complete. Wait for the size to stop changing.
    $deadline = (Get-Date).AddSeconds(20)
    $lastSize = -1
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 600
        if (-not (Test-Path $ppmPath)) { continue }
        $size = (Get-Item $ppmPath).Length
        if ($size -gt 0 -and $size -eq $lastSize) { break }
        $lastSize = $size
    }

    $writer.Dispose()
    $stream.Dispose()
} finally {
    $client.Close()
    $client.Dispose()
}

if (-not (Test-Path $ppmPath)) { throw "screendump produced nothing at $ppmPath" }

if ($isPng) {
    & python (Join-Path $PSScriptRoot 'ppm2png.py') $ppmPath $shotPath
    Remove-Item $ppmPath -Force
} else {
    Write-Host "wrote $ppmPath"
}
