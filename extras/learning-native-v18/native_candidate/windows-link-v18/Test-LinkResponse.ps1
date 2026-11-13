[CmdletBinding()]
param(
  [string]$BaseResponse='E:\WRL-1112v1\RuntimeCheck-v1\RaidParallel-v1\action-runtime-v15\isolated-link-no-pdb\Link.rsp',
  [string]$OverlayPath=(Join-Path $PSScriptRoot '..\integrated-v18\link-overlay.json')
)
# Pure transform test using the actual original V15 response; no LINK or writes.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Transform-LinkResponse.ps1')
if ((Get-FileHash -LiteralPath $BaseResponse -Algorithm SHA256).Hash -ne '146C90C86994C130519B2379713A9DFB49AAF49263830321CAA8DDA0C0BADA2A') { throw 'Wrong original response.' }
$original=[IO.File]::ReadAllText($BaseResponse)
$overlay=Get-Content -LiteralPath $OverlayPath -Raw | ConvertFrom-Json
$compile='E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18\build-v18'
$candidate='E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18\link-v18'
$result=Convert-LinkResponse $original $overlay $compile $candidate
$before=Get-LinkTokens $original;$after=$result.tokens
$oldInputs=@($before | Where-Object { $_ -notmatch '^[-/]' -and $_.Trim('"') -match '(?i)\.(obj|res|lib)$' })
if (@($oldInputs | Where-Object { $_.Trim('"') -match '(?i)\.obj$' }).Count -ne 94) { throw 'Wrong original total object count.' }
$beforeLibs=@($before | Where-Object { $_ -notmatch '^[-/]' -and $_.Trim('"') -match '(?i)\.lib$' })
$afterLibs=@($after | Where-Object { $_ -notmatch '^[-/]' -and $_.Trim('"') -match '(?i)\.lib$' })
if (($beforeLibs -join "`n") -cne ($afterLibs -join "`n") -or $beforeLibs.Count -ne 37) { throw 'Library ordering/tokens changed.' }
$expected=$before.Clone()
foreach ($entry in $overlay.object_overlays) {
  if (!$entry.replaces_explicit_object) { continue }
  $index=[Array]::IndexOf($expected,('"'+$entry.replaces_explicit_object+'"'))
  $expected[$index]='"'+(Join-Path $compile $entry.new_object)+'"'
}
$unchangedBefore=@($expected | Where-Object { $_ -notmatch '^[-/](OUT|MAP|IMPLIB):' })
$unchangedAfter=@($after[7..($after.Count-1)] | Where-Object { $_ -notmatch '^[-/](OUT|MAP|IMPLIB):' })
if (($unchangedBefore -join "`n") -cne ($unchangedAfter -join "`n")) { throw 'Unapproved original token change.' }
if ($after[0..6] -contains $null -or $result.explicit_objects -ne 95 -or $result.total_objects -ne 101) { throw 'Wrong candidate object composition.' }
$rejects=0
try { [void](Convert-LinkResponse ($original+' '+$before[0]) $overlay $compile $candidate) } catch { $rejects++ }
try { [void](Convert-LinkResponse ($original.Replace('"'+$overlay.object_overlays[0].replaces_explicit_object+'"','')) $overlay $compile $candidate) } catch { $rejects++ }
if ($rejects -ne 2) { throw 'Duplicate/missing-object rejection failed.' }
Write-Output 'PASS: actual V15 response -> eight exact replacements + seven prepended objects; 95 explicit/101 total objects; all 37 library tokens and other original tokens preserved; duplicate/missing objects rejected. No native LINK executed.'
