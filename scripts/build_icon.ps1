# build_icon.ps1
#
# Assembles a single .ico file from the 8 PNG icon sources in icons/.
#
# Each source PNG is normalized to an exact square first: scaled to fit and
# centered on a transparent canvas (no cropping, no distortion - all content
# is preserved). Then each square is encoded as a frame:
#   - sizes < 256px -> BMP DIB frame (BITMAPINFOHEADER + BGRA XOR + AND mask)
#   - 256px         -> PNG frame (stored as-is in the container)
# The ICO container itself is plain container assembly:
#
#   ICONDIR header (6 bytes) + 8 x ICONDIRENTRY (16 bytes each) + frame blobs
#
# Usage:
#   scripts\build_icon.ps1                                # default paths below
#   scripts\build_icon.ps1 -PngDir .\icons -OutFile .\icons\BFWM.ico

param(
    [string]$PngDir  = (Join-Path $PSScriptRoot '..\icons'),
    [string]$OutFile = (Join-Path $PSScriptRoot '..\icons\BFWM.ico')
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

# Expected PNG sources, in the order they are stored in the ICO (and therefore
# in the order the shell reports the embedded sizes).
$IconSizes = @(
    @{ Name = 'BFWM_16.png';   Size = 16 },
    @{ Name = 'BFWM_20.png';   Size = 20 },
    @{ Name = 'BFWM_24.png';   Size = 24 },
    @{ Name = 'BFWM_32.png';   Size = 32 },
    @{ Name = 'BFWM_48.png';   Size = 48 },
    @{ Name = 'BFWM_64.png';   Size = 64 },
    @{ Name = 'BFWM_128.png';  Size = 128 },
    @{ Name = 'BFWM_256.png';  Size = 256 }
)

# Every PNG must start with these magic bytes.
$PngMagic = [byte[]](0x89, 0x50, 0x4E, 0x47)

# 1. Verify all source PNGs exist ---------------------------------------------

$Missing = @(
    foreach ($icon in $IconSizes) {
        $pngPath = Join-Path $PngDir $icon.Name
        if (-not (Test-Path -LiteralPath $pngPath -PathType Leaf)) { $icon.Name }
    }
)
if ($Missing.Count -gt 0) {
    throw "Missing PNG source file(s) in '$PngDir': $($Missing -join ', ')"
}

# 2. Read, validate and square-normalize each PNG ------------------------------
#
# Square normalization: parse the actual dims from the IHDR chunk (big-endian
# width/height at byte offsets 16/20 after the 8-byte PNG signature), compute
# factor = min(target/W, target/H), scale the image to (round(W*factor),
# round(H*factor)) and draw it centered on a target x target transparent
# bitmap. The result is always exactly target x target.

$Squares = @()
foreach ($icon in $IconSizes) {
    $path = Join-Path $PngDir $icon.Name
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt $PngMagic.Length) {
        throw "'$path' is too short to be a PNG (got $($bytes.Length) bytes)"
    }
    for ($i = 0; $i -lt $PngMagic.Length; $i++) {
        if ($bytes[$i] -ne $PngMagic[$i]) {
            throw "'$path' does not start with PNG magic bytes (not a PNG?)"
        }
    }

    # Actual dimensions from the IHDR chunk.
    $srcW = [System.Net.IPAddress]::NetworkToHostOrder([System.BitConverter]::ToInt32($bytes, 16))
    $srcH = [System.Net.IPAddress]::NetworkToHostOrder([System.BitConverter]::ToInt32($bytes, 20))
    if ($srcW -le 0 -or $srcH -le 0) {
        throw "'$path' has invalid dimensions $($srcW)x$($srcH)"
    }

    # Sanity warning: larger side more than 2px off the nominal target.
    $larger = [Math]::Max($srcW, $srcH)
    if ([Math]::Abs($larger - $icon.Size) -gt 2) {
        Write-Host ("WARNING: {0} is {1}x{2}, expected ~{3}x{3} - scaled to fit" -f `
                $icon.Name, $srcW, $srcH, $icon.Size) -ForegroundColor Yellow
    }

    $stream = New-Object System.IO.MemoryStream(, $bytes)
    $src = New-Object System.Drawing.Bitmap($stream)

    $factor = [Math]::Min($icon.Size / $srcW, $icon.Size / $srcH)
    $newW = [Math]::Max(1, [int][Math]::Round($srcW * $factor))
    $newH = [Math]::Max(1, [int][Math]::Round($srcH * $factor))

    $square = New-Object System.Drawing.Bitmap($icon.Size, $icon.Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($square)
    try {
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.Clear([System.Drawing.Color]::Transparent)
        $offsetX = [int][Math]::Round(($icon.Size - $newW) / 2)
        $offsetY = [int][Math]::Round(($icon.Size - $newH) / 2)
        $g.DrawImage($src, $offsetX, $offsetY, $newW, $newH)
    }
    finally {
        $g.Dispose()
        $src.Dispose()
        $stream.Dispose()
    }

    $Squares += [pscustomobject]@{
        Name   = $icon.Name
        Target = $icon.Size
        SrcW   = $srcW
        SrcH   = $srcH
        NewW   = $newW
        NewH   = $newH
        Bitmap = $square
    }
}

# 3. Encode frames: BMP DIB for < 256px, PNG for 256px --------------------------

$Frames = @()
foreach ($sq in $Squares) {
    $target = $sq.Target
    $kind = if ($target -lt 256) { 'BMP' } else { 'PNG' }

    if ($target -lt 256) {
        # --- BMP DIB frame ---------------------------------------------------
        # BITMAPINFOHEADER (40 bytes) + BGRA XOR rows (bottom-up) + AND mask.
        # LockBits on Format32bppArgb yields pixels in memory order B,G,R,A,
        # which is exactly the BGRA layout the DIB needs - no byte shuffling.
        $rect = New-Object System.Drawing.Rectangle(0, 0, $target, $target)
        $data = $sq.Bitmap.LockBits($rect,
            [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $stride = [Math]::Abs($data.Stride)
        $pixels = New-Object byte[] ($stride * $target)
        try {
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
        }
        finally {
            $sq.Bitmap.UnlockBits($data)
        }

        $rowBytes    = $target * 4
        # 1bpp AND mask, each row padded to a 32-bit boundary. Note: [int]
        # casts ROUND in PowerShell, so truncate explicitly with Floor.
        $andRowBytes = 4 * [int][Math]::Floor(($target + 31) / 32)
        $xorSize     = $rowBytes * $target
        $andSize     = $andRowBytes * $target

        $frameMs = New-Object System.IO.MemoryStream
        $fw = New-Object System.IO.BinaryWriter($frameMs)
        $fw.Write([int32]40)                       # biSize
        $fw.Write([int32]$target)                  # biWidth
        $fw.Write([int32]($target * 2))            # biHeight (XOR + AND)
        $fw.Write([int16]1)                        # biPlanes
        $fw.Write([int16]32)                       # biBitCount
        $fw.Write([int32]0)                        # biCompression = BI_RGB
        $fw.Write([int32]($xorSize + $andSize))    # biSizeImage
        $fw.Write([int32]0)                        # biXPelsPerMeter
        $fw.Write([int32]0)                        # biYPelsPerMeter
        $fw.Write([int32]0)                        # biClrUsed
        $fw.Write([int32]0)                        # biClrImportant

        # XOR data: bottom-up rows (last source row first), rowBytes each.
        for ($outRow = $target - 1; $outRow -ge 0; $outRow--) {
            $fw.Write($pixels, $outRow * $stride, $rowBytes)
        }
        # AND mask: 1bpp bottom-up; all zeros because the XOR data carries
        # 32bpp alpha.
        $zeroRow = New-Object byte[] $andRowBytes
        for ($i = 0; $i -lt $target; $i++) {
            $fw.Write($zeroRow)
        }

        $fw.Flush()
        $frame = $frameMs.ToArray()
        $fw.Dispose()
        $frameMs.Dispose()
    }
    else {
        # --- PNG frame (256px) ----------------------------------------------
        $frameMs = New-Object System.IO.MemoryStream
        $sq.Bitmap.Save($frameMs, [System.Drawing.Imaging.ImageFormat]::Png)
        $frame = $frameMs.ToArray()
        $frameMs.Dispose()
    }

    $Frames += [pscustomobject]@{
        Name   = $sq.Name
        Target = $sq.Target
        Frame  = $frame
    }

    $sq.Bitmap.Dispose()

    Write-Host ("  {0}  src {1}x{2}  -> {3}x{3} (content {4}x{5})  {6} frame, {7} bytes" -f `
            $sq.Name, $sq.SrcW, $sq.SrcH, $target, $sq.NewW, $sq.NewH, $kind, $frame.Length) -ForegroundColor Cyan
}

# 4. Assemble the ICO container -------------------------------------------------
#
# ICONDIR header (little-endian):
#   WORD reserved = 0, WORD type = 1, WORD count = N
# ICONDIRENTRY (per image, 16 bytes):
#   BYTE width (0 if size == 256 else size)
#   BYTE height (same rule)
#   BYTE colorCount = 0, BYTE reserved = 0
#   WORD planes = 1, WORD bitCount = 32
#   DWORD bytesInRes = encoded frame length
#   DWORD imageOffset (cumulative offset from file start)
#
# First entry's offset = 6 (header) + 16 * 8 (entries) = 134.

$count       = $Frames.Count
$headerSize  = 6
$entrySize   = 16
$firstOffset = $headerSize + $entrySize * $count

$ms = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($ms)

try {
    # ICONDIR header.
    $writer.Write([uint16]0)          # reserved
    $writer.Write([uint16]1)          # type: icon
    $writer.Write([uint16]$count)     # image count

    # ICONDIRENTRY records; imageOffset is cumulative from file start.
    $offset = $firstOffset
    foreach ($f in $Frames) {
        $dimension = if ($f.Target -eq 256) { 0 } else { $f.Target }
        $writer.Write([byte]$dimension)            # width  (0 == 256)
        $writer.Write([byte]$dimension)            # height (0 == 256)
        $writer.Write([byte]0)                     # colorCount
        $writer.Write([byte]0)                     # reserved
        $writer.Write([uint16]1)                   # planes
        $writer.Write([uint16]32)                  # bitCount
        $writer.Write([uint32]$f.Frame.Length)     # bytesInRes
        $writer.Write([uint32]$offset)             # imageOffset
        $offset += $f.Frame.Length
    }

    # Frame blobs, concatenated in the same order.
    foreach ($f in $Frames) {
        $writer.Write($f.Frame)
    }

    $writer.Flush()
    $ico = $ms.ToArray()
}
finally {
    $writer.Dispose()
    $ms.Dispose()
}

# 5. Write out (creating the destination directory if needed) --------------------

$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path -LiteralPath $outDir -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
[System.IO.File]::WriteAllBytes($OutFile, $ico)

Write-Host "Wrote $($ico.Length) bytes to $OutFile" -ForegroundColor Green
