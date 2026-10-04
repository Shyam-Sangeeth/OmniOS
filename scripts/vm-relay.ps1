# Holds QEMU's one monitor connection for the life of the VM and runs batches
# of vm-console-style tokens dropped into $Dir as job-*.txt (vm-job.ps1 does
# that). Each job ends with a screenshot at job-*.png and a job-*.done marker.
#
# vm-console.ps1 connects, runs one script and leaves; QEMU's monitor then
# refuses every later connection for the life of the VM. This keeps the one
# connection open instead, so a test can be many small steps with a look
# between each. vm-boot.ps1 starts it; run it hidden, through powershell.exe:
#
#   Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile',
#       '-ExecutionPolicy', 'Bypass', '-File', 'scripts\vm-relay.ps1'
#
# Tokens, one per line: literal text to type, <ENTER>, <KEY:name> (a QEMU key
# name), <WAIT:seconds>, <MON:command> (any monitor command), <POLL:seconds>
# (a raw screendump every 0.4 s, as poll-N.ppm with poll.log; ppm-stats.py
# summarises them) and <QUIT>.
param([int]$Port = 24444, [string]$Dir = "$env:TEMP\omnimon")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Dir | Out-Null
$keyNames = @{
    ' ' = 'spc'; '.' = 'dot'; '-' = 'minus'; '/' = 'slash'; '_' = 'shift-minus'; ':' = 'shift-semicolon'
    '~' = 'shift-grave_accent'; '=' = 'equal'; ';' = 'semicolon'; '|' = 'shift-backslash'; '>' = 'shift-dot'
    '<' = 'shift-comma'; ',' = 'comma'; '(' = 'shift-9'; ')' = 'shift-0'; '*' = 'shift-8'; '+' = 'shift-equal'
    '"' = 'shift-apostrophe'; "'" = 'apostrophe'; '?' = 'shift-slash'; '!' = 'shift-1'; '@' = 'shift-2'
    '#' = 'shift-3'; '$' = 'shift-4'; '%' = 'shift-5'; '&' = 'shift-7'; '\' = 'backslash'
}
$client = New-Object System.Net.Sockets.TcpClient
foreach ($i in 1..60) { try { $client.Connect('127.0.0.1', $Port); break } catch { Start-Sleep 1 } }
$w = New-Object System.IO.StreamWriter($client.GetStream()); $w.AutoFlush = $true
Start-Sleep -Milliseconds 700
"relay up" | Set-Content "$Dir\relay.status"
while ($true) {
    $job = Get-ChildItem $Dir -Filter 'job-*.txt' | Sort-Object Name | Select-Object -First 1
    if (-not $job) { Start-Sleep -Milliseconds 300; continue }
    $name = [IO.Path]::GetFileNameWithoutExtension($job.FullName)
    $lines = Get-Content $job.FullName
    Remove-Item $job.FullName
    try {
        foreach ($line in $lines) {
            if ($line -eq '<ENTER>') { $w.WriteLine('sendkey ret'); Start-Sleep 1 }
            elseif ($line -match '^<WAIT:(\d+)>$') { Start-Sleep ([int]$Matches[1]) }
            elseif ($line -match '^<KEY:(.+)>$') { $w.WriteLine("sendkey $($Matches[1])"); Start-Sleep 1 }
            elseif ($line -match '^<MON:(.+)>$') { $w.WriteLine($Matches[1]); Start-Sleep 1 }
            elseif ($line -eq '<QUIT>') { $client.Close(); exit 0 }
            elseif ($line -match '^<POLL:(\d+)>$') {
                # Rapid raw screendumps, timestamped, for timing a transition.
                $sw = [Diagnostics.Stopwatch]::StartNew(); $n = 0
                Remove-Item "$Dir\poll-*.ppm", "$Dir\poll.log" -ErrorAction SilentlyContinue
                while ($sw.Elapsed.TotalSeconds -lt [int]$Matches[1]) {
                    $n++
                    $w.WriteLine("screendump $Dir\poll-$n.ppm")
                    "$n $([math]::Round($sw.Elapsed.TotalSeconds, 2)) $([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())" | Add-Content "$Dir\poll.log"
                    Start-Sleep -Milliseconds 400
                }
            }
            else {
                foreach ($ch in $line.ToCharArray()) {
                    $k = if ($keyNames.ContainsKey([string]$ch)) { $keyNames[[string]$ch] }
                         elseif ($ch -cmatch '[A-Z]') { "shift-$([char]::ToLower($ch))" } else { [string]$ch }
                    $w.WriteLine("sendkey $k"); Start-Sleep -Milliseconds 80
                }
            }
        }
        Start-Sleep 2
        $ppm = "$Dir\$name.ppm"
        $w.WriteLine("screendump $ppm")
        # Written by QEMU in pieces: done when its size stops changing.
        $last = -1
        foreach ($i in 1..30) {
            Start-Sleep -Milliseconds 600
            if (-not (Test-Path $ppm)) { continue }
            $size = (Get-Item $ppm).Length
            if ($size -gt 0 -and $size -eq $last) { break }
            $last = $size
        }
        & python "$PSScriptRoot\ppm2png.py" $ppm "$Dir\$name.png" | Out-Null
        Remove-Item $ppm -ErrorAction SilentlyContinue
        "ok" | Set-Content "$Dir\$name.done"
    } catch {
        "error: $_" | Set-Content "$Dir\$name.done"
    }
}
