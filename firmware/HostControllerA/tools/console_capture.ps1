<#
.SYNOPSIS
  Capture the HostController's USB console for a while, sending console
  commands on a schedule.

.DESCRIPTION
  Opens the board's CDC port exactly once, logs every line to a file, and
  sends the given commands one per CommandDelaySeconds. Made for the bench
  runs in docs/ST67_HTTPS_Implementation_Plan.md section 8 and for the port
  behaviour described in docs/Development.md ("COM port disappears or will
  not open"): on the development PC the port opens once per board reset and
  then fails, so everything a test needs goes into one session.

  -Port auto picks the present, working "USB Serial Device (COMx)"; the
  number moves between enumerations. Empty entries in CommandList are pauses,
  so 'wifi test;;;;;;status' sends status 35 s after wifi test at the default
  5 s spacing. The log keeps the firmware's ANSI colour codes; strip them
  with: sed -e 's/\x1b\[[0-9;]*m//g'.

.EXAMPLE
  .\tools\console_capture.ps1 -Seconds 150 -Log console.log -CommandList 'wifi test;;;;;;;;;;;;;status'

.EXAMPLE
  # Certificate cases on a build configured with -DAPP_ST67_TLS_BENCH_ANCHOR_ISRG=ON
  .\tools\console_capture.ps1 -Seconds 300 -Log bench.log -CommandList 'api host sha256.badssl.com;api path /;astro refresh;;;;;;;api host wrong.host.badssl.com;astro refresh;;;;;;;api default;api show'
#>
param(
  [string]$Port = 'auto',
  [int]$Seconds = 150,
  [string]$Log = "console.log",
  [string]$CommandList = '',
  [int]$CommandDelaySeconds = 5
)
# Semicolon-separated rather than string[], which is flattened when the script
# is launched with -File from another shell.
$Commands = $CommandList -split ';'
if ($Port -eq 'auto') {
  $dev = Get-PnpDevice -Class Ports -PresentOnly |
    Where-Object { $_.FriendlyName -like 'USB Serial Device*' -and $_.Status -eq 'OK' } |
    Select-Object -First 1
  if (-not $dev) { Write-Output '[capture] no working USB Serial Device present'; exit 2 }
  $Port = [regex]::Match($dev.FriendlyName, 'COM\d+').Value
  Write-Output ("[capture] using " + $Port)
}
$p = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$p.DtrEnable = $true   # the firmware prints its welcome on the DTR rising edge
$p.RtsEnable = $true
$p.ReadTimeout = 500
$p.NewLine = "`n"
try {
  $p.Open()
} catch {
  Write-Output ("[capture] open " + $Port + " failed: " + $_.Exception.InnerException.Message)
  exit 2
}
Set-Content -Path $Log -Value "" -Encoding utf8
$sw = [Diagnostics.Stopwatch]::StartNew()
$next = 0
$closedReads = 0
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
  try {
    $line = $p.ReadLine()
    Add-Content -Path $Log -Value $line -Encoding utf8
  } catch [System.TimeoutException] {
  } catch {
    Add-Content -Path $Log -Value ("[capture] " + $_.Exception.Message) -Encoding utf8
    if (++$closedReads -ge 5) { Write-Output '[capture] port stopped responding'; break }
  }
  if ($next -lt $Commands.Count -and $sw.Elapsed.TotalSeconds -gt ($CommandDelaySeconds * ($next + 1))) {
    if ($Commands[$next] -ne '') {
      $p.Write($Commands[$next] + "`n")
      Add-Content -Path $Log -Value ("[capture] sent: " + $Commands[$next]) -Encoding utf8
    }
    $next++
  }
}
$p.Close()
Write-Output ("[capture] " + (Get-Content -Path $Log | Measure-Object -Line).Lines + " lines in " + $Log)
