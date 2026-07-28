# Regenerates the icon's derived files from the two source SVGs in
# assets/brand/. Run it after editing either SVG:
#
#   powershell -ExecutionPolicy Bypass -File packaging\make-icon.ps1
#
# Produces, all in assets/brand/:
#   icon-256.png          -> CMake ICON_BIG   (the 32, 48 and 256 px entries)
#   icon-small-16.png     -> CMake ICON_SMALL (the 16 px entry)
#   guitar-companion.ico  -> the installer, the Start menu shortcut, Explorer
#
# Two drawings, on purpose. A .ico is a directory of independent images, and
# the full icon's three bars are 56/512 units wide - at 16 px that is under two
# pixels and they collapse into one grey smear. So 16 and 24 px use a
# simplified drawing: the pick silhouette plus a single amber stroke.
#
# JUCE picks between ICON_SMALL and ICON_BIG by WIDTH (juce_Icons.cpp,
# getBestIconForSize), not by name. So ICON_SMALL is rendered at 16, not 512
# and not 32: two 512 px files would leave the big one winning everywhere and
# the simplified drawing unused, while a 32 px one would win at 32 too and make
# the executable simpler there than the installer.
#
# These derived files are committed so a normal build needs nothing but CMake;
# only regenerating them needs Chrome.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$repo  = Split-Path -Parent $PSScriptRoot
$brand = Join-Path $repo 'assets\brand'
$full  = Join-Path $brand 'guitar-companion-icon.svg'
$small = Join-Path $brand 'guitar-companion-icon-small.svg'
$tmp   = Join-Path ([System.IO.Path]::GetTempPath()) ("gc-icon-" + [System.IO.Path]::GetRandomFileName())

$chrome = @(
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $chrome) { throw "Chrome or Edge is needed to rasterise the SVGs." }

New-Item -ItemType Directory -Force -Path $tmp | Out-Null

function Render([string]$svgPath, [int]$size, [string]$outPng) {
  $svg = [System.IO.File]::ReadAllText($svgPath)
  $svg = [regex]::Replace($svg, 'width="512" height="512"', "width=""$size"" height=""$size""", 1)
  $html = "<!DOCTYPE html><html><head><meta charset='utf-8'><style>" +
          "html,body{margin:0;padding:0;background:transparent}svg{display:block}" +
          "</style></head><body>$svg</body></html>"
  $page = Join-Path $tmp "r$size.html"
  [System.IO.File]::WriteAllText($page, $html, [System.Text.UTF8Encoding]::new($false))
  # Headless Chrome reports success on STDERR ("N bytes written to file"), and
  # with ErrorActionPreference=Stop that would abort the script - so its
  # streams are merged and swallowed, and success is judged by the file.
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    & $chrome --headless --disable-gpu --force-device-scale-factor=1 `
              --default-background-color=00000000 --window-size="$size,$size" `
              --screenshot="$outPng" ("file:///" + ($page -replace '\\','/')) *>&1 | Out-Null
  } finally { $ErrorActionPreference = $prev }
  if (-not (Test-Path $outPng)) { throw "failed to render $svgPath at $size px" }
}

# .ico entries: the full drawing down to 32, the simplified one below that.
$plan = @(
  @{ size = 256; src = $full  }, @{ size = 128; src = $full  },
  @{ size = 64;  src = $full  }, @{ size = 48;  src = $full  },
  @{ size = 32;  src = $full  }, @{ size = 24;  src = $small },
  @{ size = 16;  src = $small }
)
foreach ($p in $plan) { Render $p.src $p.size (Join-Path $tmp "icon-$($p.size).png") }

# The two CMake inputs. ICON_SMALL is rendered at 16, not 32, so that JUCE's
# width-based choice lands on exactly one size: 16 gets the simplified drawing
# and everything from 32 up gets the full one - the same split as the .ico
# below. At 32 the ICON_SMALL was winning and the executable ended up simpler
# there than the installer, which is the sort of mismatch nobody can name but
# everybody notices.
Copy-Item (Join-Path $tmp 'icon-256.png') (Join-Path $brand 'icon-256.png') -Force
Render $small 16 (Join-Path $brand 'icon-small-16.png')

function Get-BmpEntry([string]$path, [int]$size) {
  $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Transparent)
  $src = [System.Drawing.Image]::FromFile($path)
  $g.DrawImage($src, 0, 0, $size, $size)
  $g.Dispose(); $src.Dispose()

  $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $stride = $data.Stride
  $buf = New-Object byte[] ($stride * $size)
  [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
  $bmp.UnlockBits($data); $bmp.Dispose()

  $ms = New-Object System.IO.MemoryStream
  $bw = New-Object System.IO.BinaryWriter $ms
  # BITMAPINFOHEADER - the height is DOUBLED: colour rows plus mask rows.
  $bw.Write([uint32]40); $bw.Write([int32]$size); $bw.Write([int32]($size * 2))
  $bw.Write([uint16]1);  $bw.Write([uint16]32);   $bw.Write([uint32]0)
  $bw.Write([uint32]($size * $size * 4))
  $bw.Write([int32]0); $bw.Write([int32]0); $bw.Write([uint32]0); $bw.Write([uint32]0)
  for ($y = $size - 1; $y -ge 0; $y--) { $bw.Write($buf, $y * $stride, $size * 4) }
  # AND mask: unused with a real alpha channel, but the rows must exist and be
  # padded to 4 bytes or the loader reads past the end of the entry.
  $maskRow = [Math]::Floor(($size + 31) / 32) * 4
  $zeros = New-Object byte[] $maskRow
  for ($y = 0; $y -lt $size; $y++) { $bw.Write($zeros, 0, $maskRow) }
  $bw.Flush(); $bytes = $ms.ToArray(); $bw.Dispose(); $ms.Dispose()
  return $bytes
}

$entries = @()
foreach ($p in $plan) {
  $png = Join-Path $tmp "icon-$($p.size).png"
  # 256 stays PNG-compressed (the convention since Vista, and it keeps the file
  # small); everything below is the plain 32-bit BMP form every loader reads.
  $bytes = if ($p.size -eq 256) { [System.IO.File]::ReadAllBytes($png) }
           else                 { Get-BmpEntry $png $p.size }
  $entries += ,@{ size = $p.size; bytes = $bytes }
}

$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
  $dim = if ($e.size -ge 256) { 0 } else { $e.size }   # 0 means 256 in this field
  $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
  $w.Write([uint16]1);  $w.Write([uint16]32)
  $w.Write([uint32]$e.bytes.Length); $w.Write([uint32]$offset)
  $offset += $e.bytes.Length
}
foreach ($e in $entries) { $w.Write($e.bytes, 0, $e.bytes.Length) }
$w.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $brand 'guitar-companion.ico'), $out.ToArray())
$w.Dispose(); $out.Dispose()

Remove-Item $tmp -Recurse -Force
"icon-256.png, icon-small-16.png and guitar-companion.ico written to assets/brand/"
