#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Reports whether the architecture diagrams in docs/diagrams are still based on
    current code.

.DESCRIPTION
    Each .drawio file carries a provenance block: an <object id="prov"> cell with
    basedOn="<sha>" and covers="<path>;<path>;..." attributes. This script reads
    those attributes and asks git whether anything under the covered paths has
    changed since the recorded commit.

    A diagram is STALE when commits after its basedOn commit touched one of its
    covered paths. Staleness is not an error - it just means the diagram needs a
    review - but the script exits non-zero by default so CI can nag.

.PARAMETER Dir
    Directory holding the .drawio files. Defaults to <repo>/docs/diagrams.

.PARAMETER NoFail
    Always exit 0, even when diagrams are stale.

.EXAMPLE
    pwsh scripts/check_diagrams.ps1
#>
[CmdletBinding()]
param(
    [string]$Dir,
    [switch]$NoFail
)

$ErrorActionPreference = 'Stop'

function Invoke-GitArgs {
    param([string[]]$GitArgs)
    $out = & git @GitArgs 2>&1
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($out | Out-String)
}

# draw.io stores a page either as plain <mxGraphModel> XML or as
# base64(raw-deflate(URI-encoded XML)). Saving from the app usually compresses,
# so decompress before searching for the provenance block.
function Get-DiagramXml {
    param([string]$Raw)
    if ($Raw -match '<object\b[^>]*id="prov"') { return $Raw }

    $m = [regex]::Match($Raw, '(?s)<diagram[^>]*>([A-Za-z0-9+/=\s]+)</diagram>')
    if (-not $m.Success) { return $Raw }

    try {
        $b64 = $m.Groups[1].Value -replace '\s', ''
        $bytes = [Convert]::FromBase64String($b64)
        $input = New-Object System.IO.MemoryStream(, $bytes)
        $deflate = New-Object System.IO.Compression.DeflateStream($input, [System.IO.Compression.CompressionMode]::Decompress)
        $out = New-Object System.IO.MemoryStream
        $deflate.CopyTo($out)
        $deflate.Dispose()
        $out.Dispose()
        $xml = [System.Text.Encoding]::UTF8.GetString($out.ToArray())
        return [System.Uri]::UnescapeDataString($xml)
    }
    catch {
        return $Raw
    }
}

$rootRaw = Invoke-GitArgs @('rev-parse', '--show-toplevel')
if (-not $rootRaw) {
    Write-Error 'not inside a git repository'
    exit 2
}
$root = $rootRaw.Trim()

if (-not $Dir) { $Dir = Join-Path $root 'docs/diagrams' }
if (-not (Test-Path $Dir)) {
    Write-Error "diagram directory not found: $Dir"
    exit 2
}

$files = Get-ChildItem -Path $Dir -Filter '*.drawio' | Sort-Object Name
if (-not $files) {
    Write-Host "no .drawio files in $Dir"
    exit 0
}

$results = foreach ($file in $files) {
    $text = Get-DiagramXml ([System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8))

    $provMatch = [regex]::Match($text, '<object\b[^>]*\bid="prov"[^>]*>')
    if (-not $provMatch.Success) {
        [pscustomobject]@{
            Diagram = $file.Name
            Status  = 'NO-PROV'
            BasedOn = '-'
            Detail  = 'missing <object id="prov"> provenance block'
        }
        continue
    }

    $prov = $provMatch.Value
    $basedOn = [regex]::Match($prov, '\bbasedOn="([^"]+)"').Groups[1].Value
    $covers = [regex]::Match($prov, '\bcovers="([^"]+)"').Groups[1].Value

    if (-not $basedOn -or -not $covers) {
        [pscustomobject]@{
            Diagram = $file.Name
            Status  = 'NO-PROV'
            BasedOn = '-'
            Detail  = 'provenance block lacks basedOn= or covers='
        }
        continue
    }

    $paths = @($covers -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })

    $resolved = Invoke-GitArgs @('rev-parse', '--verify', '--quiet', "$basedOn^{commit}")
    if (-not $resolved) {
        [pscustomobject]@{
            Diagram = $file.Name
            Status  = 'BAD-SHA'
            BasedOn = $basedOn
            Detail  = "commit $basedOn not found (shallow clone or history rewrite?)"
        }
        continue
    }

    $countRaw = Invoke-GitArgs (@('rev-list', '--count', "$basedOn..HEAD", '--') + $paths)
    if (-not $countRaw) {
        [pscustomobject]@{
            Diagram = $file.Name
            Status  = 'ERROR'
            BasedOn = $basedOn
            Detail  = 'git rev-list failed for the covered paths'
        }
        continue
    }

    $count = [int]$countRaw.Trim()
    if ($count -eq 0) {
        [pscustomobject]@{
            Diagram = $file.Name
            Status  = 'OK'
            BasedOn = $basedOn
            Detail  = 'covered paths untouched since this commit'
        }
    }
    else {
        $newest = Invoke-GitArgs ((@('log', '-1', '--format=%h %s', '--') + $paths))
        [pscustomobject]@{
            Diagram = $file.Name
            Status  = 'STALE'
            BasedOn = $basedOn
            Detail  = "$count commit(s) since; newest: $($newest.Trim())"
        }
    }
}

$results | Format-Table -AutoSize -Wrap

$bad = @($results | Where-Object { $_.Status -ne 'OK' })
if ($bad.Count -gt 0) {
    Write-Host ''
    Write-Host "$($bad.Count) diagram(s) need review:" -ForegroundColor Yellow
    foreach ($s in $bad) { Write-Host "  $($s.Diagram)  ($($s.Status))  $($s.Detail)" }
    if (-not $NoFail) { exit 1 }
}
else {
    Write-Host ''
    Write-Host 'All diagrams are current.' -ForegroundColor Green
}
exit 0
