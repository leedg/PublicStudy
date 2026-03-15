param()
$src = \042E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Reports/Network_Async_DB_Report_img\042
$dst = \042E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Reports/Network_Async_DB_Report_img_C\042
function Transform($file, $newW, $newH, $extraCells) {
    $t = [System.IO.File]::ReadAllText(\042$src/$file\042, [System.Text.Encoding]::UTF8)
    $t = [regex]::Replace($t, \042fontSize=8(?=[^0-9])\042, \042fontSize=11\042)
    $t = [regex]::Replace($t, \042fontSize=9(?=[^0-9])\042, \042fontSize=12\042)
    if($newW) { $t = [regex]::Replace($t, \042pageWidth=\d+\042, \042pageWidth=\042 + $newW + \042\042) }
    if($newH) { $t = [regex]::Replace($t, \042pageHeight=\d+\042, \042pageHeight=\042 + $newH + \042\042) }
    $closeTag = \042<\042 + \042/root\042 + \042>\042
    $t = $t.Replace($closeTag, $extraCells + [System.Environment]::NewLine + \042  \042 + $closeTag)
    [System.IO.File]::WriteAllText(\042$dst/$file\042, $t, [System.Text.Encoding]::UTF8)
    Write-Output (\042Created: \042 + $file)
}
Write-Output \042PS1 transform function ready\042
