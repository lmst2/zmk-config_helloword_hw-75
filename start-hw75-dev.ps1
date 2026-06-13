param(
  [switch]$OpenBrowser,
  [switch]$NoRestart
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$helperDir = Join-Path $repoRoot 'tools\hw75-core'
$appDir = Join-Path $repoRoot 'deps\zmkx.app'
$helperUrl = 'http://127.0.0.1:8755/api/health'
$frontendUrl = 'http://127.0.0.1:8080/'
$helperPort = 8755
$frontendPort = 8080

function Find-ManagedProcesses([string]$matchText) {
  # Return every node/npm process whose command line matches; the launcher
  # path usually spawns npm.cmd -> cmd.exe -> node.exe, so there can be more
  # than one entry to clean up.
  Get-CimInstance Win32_Process |
    Where-Object { $_.Name -match '^(node|npm|cmd)(\.exe)?$' -and $_.CommandLine -like "*$matchText*" }
}

function Get-PortOwners([int]$port) {
  # netstat is more reliable than Get-NetTCPConnection (which can hang on
  # some Windows boxes). Parse the LISTENING rows for the given port.
  $lines = netstat -ano | Select-String -Pattern ":$port\s+.*LISTENING"
  $pids = @()
  foreach ($line in $lines) {
    $tokens = ($line.Line -split '\s+') | Where-Object { $_ -ne '' }
    if ($tokens.Length -ge 5) {
      $pids += [int]$tokens[-1]
    }
  }
  return ($pids | Select-Object -Unique)
}

function Stop-ProcessQuiet([int]$targetPid, [string]$label) {
  try {
    Stop-Process -Id $targetPid -Force -ErrorAction Stop
    Write-Host "  已结束 $label 进程 PID $targetPid"
  } catch {
    # Process may have died between lookup and kill; safe to ignore.
  }
}

function Stop-ManagedService(
  [string]$displayName,
  [string]$matchText,
  [int]$port
) {
  $killed = @{}

  foreach ($proc in Find-ManagedProcesses $matchText) {
    Stop-ProcessQuiet -targetPid ([int]$proc.ProcessId) -label $displayName
    $killed[[int]$proc.ProcessId] = $true
  }

  foreach ($portPid in Get-PortOwners $port) {
    if (-not $killed.ContainsKey($portPid)) {
      Stop-ProcessQuiet -targetPid $portPid -label "$displayName (port $port)"
      $killed[$portPid] = $true
    }
  }

  if ($killed.Count -gt 0) {
    # Give Windows a moment to free the listening socket before we re-bind.
    Start-Sleep -Milliseconds 400
  }
}

function Wait-HttpOk([string]$url, [int]$timeoutSeconds) {
  $deadline = (Get-Date).AddSeconds($timeoutSeconds)

  while ((Get-Date) -lt $deadline) {
    try {
      $statusCode = & curl.exe -s -o NUL -w "%{http_code}" $url
      if ($statusCode -eq '200') {
        return $true
      }
    } catch {
    }

    Start-Sleep -Milliseconds 500
  }

  return $false
}

function Start-ManagedProcess(
  [string]$displayName,
  [string]$workingDirectory,
  [string[]]$arguments
) {
  $process = Start-Process -FilePath 'npm.cmd' -ArgumentList $arguments -WorkingDirectory $workingDirectory -PassThru
  Write-Host "$displayName 已启动，PID: $($process.Id)"
  return $process
}

if ($NoRestart) {
  Write-Host '启动 HW75 开发环境...'
} else {
  Write-Host '重启 HW75 开发环境...'
  Stop-ManagedService -displayName 'HW75 中枢' -matchText 'hw75-core' -port $helperPort
  Stop-ManagedService -displayName '前端开发服务器' -matchText 'vite' -port $frontendPort
}

Start-ManagedProcess -displayName 'HW75 中枢' -workingDirectory $helperDir -arguments @('run', 'start')
Start-ManagedProcess -displayName '前端开发服务器' -workingDirectory $appDir -arguments @('run', 'dev')

$helperReady = Wait-HttpOk -url $helperUrl -timeoutSeconds 10
$frontendReady = Wait-HttpOk -url $frontendUrl -timeoutSeconds 15

if ($helperReady) {
  Write-Host "中枢就绪: $helperUrl"
} else {
  Write-Warning "中枢未在预期时间内就绪: $helperUrl"
}

if ($frontendReady) {
  Write-Host "前端就绪: $frontendUrl"
} else {
  Write-Warning "前端未在预期时间内就绪: $frontendUrl"
}

if ($OpenBrowser -and $frontendReady) {
  Start-Process $frontendUrl | Out-Null
}
