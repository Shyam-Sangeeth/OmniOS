# Runs a shell command in the VM over ssh, as omni, with no password prompt.
#   vm-ssh.ps1 'uname -r; systemctl --failed'
#   vm-ssh.ps1 -File test.sh
# The live ISO only: an installed system has no ssh (omni-install turns it
# off). run-qemu.ps1 forwards host port 12222 to the guest's 22.
param([string]$Command, [string]$File, [string]$User = 'omni', [int]$Port = 12222)
# Windows PowerShell 5.1 mangles quotes and pipes on the way to a native exe,
# so the command travels base64-encoded and is decoded by the guest's shell.
if ($File) { $Command = Get-Content $File -Raw }
$Command = $Command -replace "`r", ''
$b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Command))
# The live image's password is printed in this repository (README); it is not
# a secret. ssh reads it from an askpass program rather than a prompt.
$askpass = Join-Path $env:TEMP 'omnios-askpass.bat'
'@echo omnios' | Set-Content $askpass -Encoding ascii
$env:SSH_ASKPASS = $askpass
$env:SSH_ASKPASS_REQUIRE = 'force'
$env:DISPLAY = 'dummy:0'
# Each live boot has a new host key; nothing is worth remembering. Not NUL:
# Windows' ssh takes that for a file name and writes one wherever it runs.
$knownHosts = Join-Path $env:TEMP 'omnios-known_hosts'
Remove-Item $knownHosts -ErrorAction SilentlyContinue
& ssh.exe -p $Port -o StrictHostKeyChecking=no -o "UserKnownHostsFile=$knownHosts" -o LogLevel=ERROR `
    -o PreferredAuthentications=password,keyboard-interactive -o ConnectTimeout=10 `
    "$User@127.0.0.1" "echo $b64 | base64 -d | bash"
