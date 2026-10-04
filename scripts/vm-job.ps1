# Hands one batch of tokens to vm-relay.ps1 and waits for its screenshot.
#   vm-job.ps1 -Name step1 '<KEY:alt-f2>' '<WAIT:4>' 'konsole' '<ENTER>' '<WAIT:10>'
# Prints "ok -> <path to the PNG>", or the relay's error.
[CmdletBinding(PositionalBinding = $false)]
param([string]$Name = 'step', [string]$Dir = "$env:TEMP\omnimon",
      [Parameter(ValueFromRemainingArguments)][string[]]$Tokens)
$job = "job-$Name"
Remove-Item "$Dir\$job.done", "$Dir\$job.png" -ErrorAction SilentlyContinue
# Written aside and renamed, so the relay never reads half a job.
$Tokens | Set-Content "$Dir\$job.txt.tmp"
Move-Item "$Dir\$job.txt.tmp" "$Dir\$job.txt"
$deadline = (Get-Date).AddMinutes(10)
while (-not (Test-Path "$Dir\$job.done")) {
    if ((Get-Date) -gt $deadline) { throw "relay did not finish $job" }
    Start-Sleep -Milliseconds 500
}
"$(Get-Content "$Dir\$job.done") -> $Dir\$job.png"
