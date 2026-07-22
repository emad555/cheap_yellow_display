param(
  [string]$InputVideo = "E:\005   Dog Trouble [1942].avi",
  [string]$OutputVideo = "E:\video.mjpg",
  [int]$Fps = 6,
  [int]$Width = 320,
  [int]$Height = 216,
  [int]$Quality = 18
)

function Find-Ffmpeg {
  $cmd = Get-Command ffmpeg -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ffmpeg.exe"
  if (Test-Path -LiteralPath $link) {
    $item = Get-Item -LiteralPath $link
    if ($item.Target -and (Test-Path -LiteralPath $item.Target[0])) {
      return $item.Target[0]
    }
  }

  return $null
}

$ffmpeg = Find-Ffmpeg
if (-not $ffmpeg) {
  Write-Host "ffmpeg was not found on PATH."
  Write-Host "Install ffmpeg, then run this script again."
  Write-Host ""
  Write-Host "Manual command once ffmpeg is installed:"
  Write-Host "ffmpeg -y -i `"$InputVideo`" -vf `"fps=$Fps,scale=$Width`:$Height`:force_original_aspect_ratio=decrease,pad=$Width`:$Height`:(ow-iw)/2`:(oh-ih)/2,format=yuvj420p`" -q:v $Quality -an -f mjpeg `"$OutputVideo`""
  exit 1
}

if (-not (Test-Path -LiteralPath $InputVideo)) {
  Write-Host "Input video not found: $InputVideo"
  exit 1
}

& $ffmpeg -y `
  -i "$InputVideo" `
  -vf "fps=$Fps,scale=$Width`:$Height`:force_original_aspect_ratio=decrease,pad=$Width`:$Height`:(ow-iw)/2`:(oh-ih)/2,format=yuvj420p" `
  -q:v $Quality `
  -an `
  -f mjpeg `
  "$OutputVideo"

if ($LASTEXITCODE -eq 0) {
  Write-Host ""
  Write-Host "Done. Put the SD card in the CYD and tap SD Video."
  Write-Host "File created: $OutputVideo"
}
