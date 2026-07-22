param(
  [int]$Port = 8080,
  [int]$Width = 320,
  [int]$Height = 240,
  [int]$Fps = 8,
  [int]$Quality = 55
)

Add-Type -AssemblyName System.Drawing

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
$listener.Start()
$boundary = "cydtest"
$frameDelayMs = [Math]::Max(1, [int](1000 / [Math]::Max(1, $Fps)))

function Get-LocalIp {
  $udp = [System.Net.Sockets.UdpClient]::new()
  try {
    $udp.Connect("8.8.8.8", 80)
    return $udp.Client.LocalEndPoint.Address.ToString()
  } catch {
    return "127.0.0.1"
  } finally {
    $udp.Dispose()
  }
}

function New-TestJpeg([int]$Frame) {
  $bmp = [System.Drawing.Bitmap]::new($Width, $Height)
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  $gfx.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

  $bg = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
    [System.Drawing.Rectangle]::new(0, 0, $Width, $Height),
    [System.Drawing.Color]::FromArgb(28, 32, 42),
    [System.Drawing.Color]::FromArgb(10, 118, 128),
    25
  )
  $gfx.FillRectangle($bg, 0, 0, $Width, $Height)

  $barX = ($Frame * 9) % ($Width + 60) - 60
  $gfx.FillRectangle([System.Drawing.Brushes]::Gold, $barX, 0, 42, $Height)

  $cx = 40 + (($Frame * 5) % ($Width - 80))
  $cy = 88 + [int](45 * [Math]::Sin($Frame / 8.0))
  $gfx.FillEllipse([System.Drawing.Brushes]::DeepSkyBlue, $cx - 24, $cy - 24, 48, 48)
  $gfx.DrawEllipse([System.Drawing.Pens]::White, $cx - 24, $cy - 24, 48, 48)

  $fontBig = [System.Drawing.Font]::new("Consolas", 22, [System.Drawing.FontStyle]::Bold)
  $fontSmall = [System.Drawing.Font]::new("Consolas", 11, [System.Drawing.FontStyle]::Regular)
  $gfx.DrawString("CYD TEST", $fontBig, [System.Drawing.Brushes]::White, 12, 18)
  $gfx.DrawString(("{0}x{1}  {2} fps  q{3}" -f $Width, $Height, $Fps, $Quality), $fontSmall, [System.Drawing.Brushes]::WhiteSmoke, 14, 55)
  $gfx.DrawString(("frame {0}" -f $Frame), $fontSmall, [System.Drawing.Brushes]::WhiteSmoke, 14, 210)

  $stream = [System.IO.MemoryStream]::new()
  $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
  $encParams = [System.Drawing.Imaging.EncoderParameters]::new(1)
  $encParams.Param[0] = [System.Drawing.Imaging.EncoderParameter]::new([System.Drawing.Imaging.Encoder]::Quality, [long]$Quality)
  $bmp.Save($stream, $codec, $encParams)

  $bytes = $stream.ToArray()
  $encParams.Dispose()
  $stream.Dispose()
  $fontSmall.Dispose()
  $fontBig.Dispose()
  $bg.Dispose()
  $gfx.Dispose()
  $bmp.Dispose()
  return $bytes
}

$ip = Get-LocalIp
Write-Host "CYD test MJPEG server"
Write-Host "URL: http://$ip`:$Port/"
Write-Host "Press Ctrl+C to stop"

$frame = 0
while ($true) {
  $client = $listener.AcceptTcpClient()
  try {
    $client.NoDelay = $true
    $net = $client.GetStream()
    $reader = [System.IO.StreamReader]::new($net, [System.Text.Encoding]::ASCII, $false, 1024, $true)
    while ($reader.ReadLine()) {
      if ($reader.EndOfStream) { break }
      if ($reader.Peek() -eq 13 -or $reader.Peek() -eq 10) { break }
    }

    $header = "HTTP/1.1 200 OK`r`nCache-Control: no-cache, no-store, private`r`nPragma: no-cache`r`nConnection: close`r`nContent-Type: multipart/x-mixed-replace; boundary=$boundary`r`n`r`n"
    $headerBytes = [System.Text.Encoding]::ASCII.GetBytes($header)
    $net.Write($headerBytes, 0, $headerBytes.Length)

    while ($client.Connected) {
      $jpg = New-TestJpeg $frame
      $part = "--$boundary`r`nContent-Type: image/jpeg`r`nContent-Length: $($jpg.Length)`r`n`r`n"
      $partBytes = [System.Text.Encoding]::ASCII.GetBytes($part)
      $net.Write($partBytes, 0, $partBytes.Length)
      $net.Write($jpg, 0, $jpg.Length)
      $endBytes = [System.Text.Encoding]::ASCII.GetBytes("`r`n")
      $net.Write($endBytes, 0, $endBytes.Length)
      $net.Flush()
      $frame++
      Start-Sleep -Milliseconds $frameDelayMs
    }
  } catch {
  } finally {
    $client.Close()
  }
}
