<#
.SYNOPSIS
    Shrink Docker Desktop's data disk so Windows gets freed space back.

.DESCRIPTION
    Docker Desktop keeps images, containers and volumes in one virtual disk,
    docker_data.vhdx, which grows as they are written and never shrinks by
    itself. Each ISO build and container compile writes gigabytes that are
    deleted again afterwards; the disk keeps the space. Seen 2026-10-05: 26 GB
    on the host for 10 GB of images and volumes.

    Two steps. fstrim inside Docker's VM marks the freed blocks as free, which
    is what lets the disk give them up; then diskpart compacts it, which needs
    Docker and WSL stopped, and administrator rights (this asks for them).
    Nothing inside Docker is deleted: prune or remove images first for that.

.EXAMPLE
    ./scripts/compact-docker.ps1
#>
param([switch]$Elevated)
$ErrorActionPreference = 'Stop'
$vhdx = Join-Path $env:LOCALAPPDATA 'Docker\wsl\disk\docker_data.vhdx'
if (-not (Test-Path $vhdx)) { throw "Docker's data disk is not at $vhdx" }

if (-not $Elevated) {
    # The trim needs Docker running; the compaction needs it stopped.
    docker info *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-Host '==> trimming freed space inside Docker'
        docker run --rm --privileged --pid=host debian:stable nsenter -t 1 -m -- fstrim -av
    } else {
        Write-Host '==> Docker is not running; compacting without a trim first (frees less)'
    }
    $before = (Get-Item $vhdx).Length
    Write-Host '==> compacting (administrator prompt)'
    Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', "`"$PSCommandPath`"", '-Elevated'
    $after = (Get-Item $vhdx).Length
    Write-Host ("==> {0:N1} GB -> {1:N1} GB; Docker is stopped, and starts again when it is next needed" -f ($before / 1GB), ($after / 1GB))
    exit 0
}

# Elevated: the disk has to be free, attached by nothing, or diskpart cannot
# open it.
Get-Process 'Docker Desktop', 'com.docker.backend', 'com.docker.build' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
wsl --shutdown
Start-Sleep -Seconds 10
$commands = Join-Path $env:TEMP 'omnios-compact-docker.txt'
@"
select vdisk file="$vhdx"
attach vdisk readonly
compact vdisk
detach vdisk
"@ | Set-Content -Path $commands -Encoding ascii
diskpart /s $commands | Out-Null
Remove-Item $commands -ErrorAction SilentlyContinue
