Add-Type -AssemblyName WindowsBase
Add-Type -AssemblyName System.Xml.Linq

$wns  = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
$wpQN = [System.Xml.Linq.XName]::Get('p', $wns)
$wtQN = [System.Xml.Linq.XName]::Get('t', $wns)
$docUri = [System.Uri]::new('/word/document.xml', [System.UriKind]::Relative)

function Get-ParaText([System.Xml.Linq.XElement]$p) {
    ($p.Descendants($wtQN) | ForEach-Object { $_.Value }) -join ''
}

function Edit-Docx {
    param([string]$path, [string[]]$patterns)

    Write-Host ""
    Write-Host "=== $([IO.Path]::GetFileName($path)) ==="

    $pkg  = [System.IO.Packaging.Package]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)
    $part = $pkg.GetPart($docUri)

    # --- Read into MemoryStream ---
    $rStream = $part.GetStream()
    $ms = New-Object System.IO.MemoryStream
    $rStream.CopyTo($ms)
    $rStream.Close()
    $ms.Position = 0

    # --- Parse XML ---
    $xdoc = [System.Xml.Linq.XDocument]::Load($ms)
    $ms.Dispose()

    $allParas = @($xdoc.Descendants($wpQN))

    # --- Pass 1: find paragraphs matching patterns ---
    $matchedIdx = [Collections.Generic.HashSet[int]]::new()
    for ($i = 0; $i -lt $allParas.Count; $i++) {
        $txt = Get-ParaText $allParas[$i]
        foreach ($pat in $patterns) {
            if ($txt -match $pat) { $matchedIdx.Add($i) | Out-Null; break }
        }
    }

    # --- Pass 2: absorb short orphan paragraphs sandwiched between removed ones ---
    $removeIdx = [Collections.Generic.HashSet[int]]::new($matchedIdx)
    for ($i = 0; $i -lt $allParas.Count; $i++) {
        if ($removeIdx.Contains($i)) { continue }
        $txt = (Get-ParaText $allParas[$i]).Trim()
        if ($txt.Length -lt 25) {
            $prevOk = ($i -eq 0) -or $removeIdx.Contains($i - 1)
            $nextOk = ($i -ge $allParas.Count - 1) -or $removeIdx.Contains($i + 1)
            if ($prevOk -and $nextOk) { $removeIdx.Add($i) | Out-Null }
        }
    }

    # --- Remove matched paragraphs ---
    $removed = 0
    for ($i = 0; $i -lt $allParas.Count; $i++) {
        if ($removeIdx.Contains($i)) {
            $t = Get-ParaText $allParas[$i]
            $display = if ($t.Length -gt 90) { $t.Substring(0, 90) + '...' } else { $t }
            Write-Host "  - $display"
            $allParas[$i].Remove()
            $removed++
        }
    }
    Write-Host "  -> removed: $removed"

    if ($removed -gt 0) {
        # --- Serialize to MemoryStream first (safe: don't truncate until content is ready) ---
        $outMs = New-Object System.IO.MemoryStream
        $xdoc.Save($outMs)
        $outMs.Position = 0

        # --- Now overwrite the package part ---
        $wStream = $part.GetStream([System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
        $outMs.CopyTo($wStream)
        $wStream.Flush()
        $wStream.Close()
        $outMs.Dispose()
    }

    $pkg.Close()
    Write-Host "  done"
}

$base = 'E:\MyGitHub\PublicStudy\NetworkModuleTest\Docs\Reports'

$pat1 = '(?<![A-Za-z])DBServer\.cpp'
$pat2 = 'DBServer/main\.cpp'

Edit-Docx -path "$base\NetworkAsyncDBReport\Network_Async_DB_Report.docx" -patterns @($pat1)

Edit-Docx -path "$base\TeamShare\Package.docx" -patterns @($pat1, $pat2)

Edit-Docx -path "$base\ExecutiveSummary\Package.docx" -patterns @($pat1)
