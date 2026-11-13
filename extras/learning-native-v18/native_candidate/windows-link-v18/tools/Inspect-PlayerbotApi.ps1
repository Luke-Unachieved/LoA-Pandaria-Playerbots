[CmdletBinding()]
param(
  [string]$ModulesLibrary='E:\WoW548\Build\modules\modules.lib',
  [string]$ActiveHeader='E:\WoW548\Source\modules\mod_playerbots\src\Manager\PlayerbotMgr.h',
  [string]$BindingHeader=(Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'integrated-v18\native-api\PlayerbotMgr.h'),
  [string]$OutputDir='E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18\playerbot-api-audit'
)
# Evidence collection only, Windows PowerShell 5.1+. Inputs are read-only.
# No build, link, installation, archive extraction, acceptance, or server action.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function FullPath([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path) -or $Path -match '["\r\n]') { throw "Unsupported path: $Path" }
  return [IO.Path]::GetFullPath($Path)
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function RejectReparsePoints([string]$Path) {
  for ($current=$Path;$current;$current=Split-Path -Parent $current) {
    if ((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      throw "Audit output traverses a reparse point: $current"
    }
  }
}
function HeaderExcerpt([string[]]$Lines) {
  # Preserve ordering, bases, type aliases, virtuals, access labels and fields.
  # These numbered source excerpts require human/agent review; they are not an ABI parser.
  for ($i=0;$i -lt $Lines.Count;$i++) {
    $line=$Lines[$i];$trim=$line.Trim()
    if (!$trim -or $trim.StartsWith('//')) { continue }
    '{0,4}: {1}' -f ($i+1),$line
  }
}
$ModulesLibrary=FullPath $ModulesLibrary;$ActiveHeader=FullPath $ActiveHeader
$BindingHeader=FullPath $BindingHeader;$OutputDir=FullPath $OutputDir
$auditRoot=FullPath 'E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18\playerbot-api-audit'
if ($OutputDir -ne $auditRoot) { throw "Use the dedicated audit directory: $auditRoot" }
foreach ($path in @($ModulesLibrary,$ActiveHeader,$BindingHeader)) {
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing read-only input: $path" }
}
$expectedBinding='49050bb78713278eee9b0f2513b3b2ba7cf7c37c4852b1ed3835faf35aa79a5a'
$libraryBefore=Hash $ModulesLibrary;$activeBefore=Hash $ActiveHeader;$bindingBefore=Hash $BindingHeader
if ($bindingBefore -ne $expectedBinding) { throw 'Binding header differs from the audited supplied PlayerbotMgr.h.' }
$activeLines=[IO.File]::ReadAllLines($ActiveHeader)
$bindingLines=[IO.File]::ReadAllLines($BindingHeader)
if ($activeLines.Count -gt 1000 -or $bindingLines.Count -gt 1000) { throw 'Unexpected header size; review before generating excerpts.' }

$dumpbin=$null
$found=Get-Command dumpbin.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($found) { $dumpbin=FullPath $found.Source }
if (!$dumpbin) {
  $msvcRoot='E:\WoW548\BuildTools\VS2022\VC\Tools\MSVC'
  if (Test-Path -LiteralPath $msvcRoot -PathType Container) {
    foreach ($version in @(Get-ChildItem -LiteralPath $msvcRoot -Directory | Where-Object { $_.Name -match '^14\.44\.\d+$' } | Sort-Object Name -Descending)) {
      $candidate=Join-Path $version.FullName 'bin\Hostx64\x64\dumpbin.exe'
      if (Test-Path -LiteralPath $candidate -PathType Leaf) { $dumpbin=FullPath $candidate;break }
    }
  }
}
if (!$dumpbin) { throw 'Existing dumpbin.exe was not found on PATH or in the installed VS14.44 x64 toolchain. Nothing was installed.' }
$dumpbinHash=Hash $dumpbin
$methods=@('IsRaidOwnedBot','IsManagedLogin','IsRaidProvisioningAccount')
$selected=New-Object 'System.Collections.Generic.List[string]'
$symbols=New-Object 'System.Collections.Generic.List[string]'
$diagnostics=New-Object 'System.Collections.Generic.List[string]'
$symbolPattern='(?<!\S)(\?(?:IsRaidOwnedBot|IsManagedLogin|IsRaidProvisioningAccount)@PlayerbotHolder@@[^\s]+)(?=\s|$)'
# Stream the archive index through the filter. Never store the whole-library dump.
# PS5.1 represents native stderr as ErrorRecord; retain a report on native failure.
$nativePreference=$ErrorActionPreference;$dumpbinExit=-1
try {
  $ErrorActionPreference='Continue'
  $LASTEXITCODE=-1
  & $dumpbin /nologo /linkermember:2 $ModulesLibrary 2>&1 | ForEach-Object {
    $line=[string]$_
    if ($line -match $symbolPattern) {
      $selected.Add($line);$symbols.Add($matches[1])
    } elseif ($line -match '(?i)\b(?:fatal error|error LNK|warning LNK)\b' -and $diagnostics.Count -lt 12) {
      $diagnostics.Add($line)
    }
  }
  $dumpbinExit=$LASTEXITCODE
} catch {
  if ($diagnostics.Count -lt 12) { $diagnostics.Add($_.Exception.Message) }
} finally { $ErrorActionPreference=$nativePreference }
$libraryAfter=Hash $ModulesLibrary;$activeAfter=Hash $ActiveHeader;$bindingAfter=Hash $BindingHeader
$unchanged=($libraryBefore -eq $libraryAfter -and $activeBefore -eq $activeAfter -and $bindingBefore -eq $bindingAfter)
$methodEvidence=@(foreach ($method in $methods) {
  $prefix='?'+$method+'@PlayerbotHolder@@'
  $matchesForMethod=@($symbols | Where-Object { $_.StartsWith($prefix,[StringComparison]::Ordinal) } | Select-Object -Unique)
  [pscustomobject]@{method=$method;archive_public_index_present=($matchesForMethod.Count -gt 0);decorated_symbols=$matchesForMethod}
})
$differences=@(Compare-Object -ReferenceObject $activeLines -DifferenceObject $bindingLines -SyncWindow 1000 -CaseSensitive | ForEach-Object {
  [pscustomobject]@{side=if ($_.SideIndicator -eq '<=') { 'active-only' } else { 'binding-only' };line=$_.InputObject}
})
$report=[ordered]@{
  schema='native-v18-playerbot-api-inspection-v1'
  collected_utc=[DateTime]::UtcNow.ToString('o')
  evidence_only=$true
  modules_library=[ordered]@{path=$ModulesLibrary;sha256_before=$libraryBefore;sha256_after=$libraryAfter}
  active_header=[ordered]@{path=$ActiveHeader;sha256_before=$activeBefore;sha256_after=$activeAfter;line_count=$activeLines.Count}
  binding_header=[ordered]@{path=$BindingHeader;sha256_before=$bindingBefore;sha256_after=$bindingAfter;expected_sha256=$expectedBinding;matches_supplied_header=($bindingBefore -eq $expectedBinding);line_count=$bindingLines.Count}
  dumpbin=[ordered]@{path=$dumpbin;sha256=$dumpbinHash;file_version=(Get-Item -LiteralPath $dumpbin).VersionInfo.FileVersion;arguments=@('/nologo','/linkermember:2',$ModulesLibrary);exit=$dumpbinExit;diagnostics=@($diagnostics)}
  inputs_unchanged=$unchanged
  api_symbols=$methodEvidence
  selected_archive_index_lines=@($selected)
  header_content_differences=$differences
  active_header_review_excerpt=@(HeaderExcerpt $activeLines)
  binding_header_review_excerpt=@(HeaderExcerpt $bindingLines)
  class_layout_review_status='required: compare type aliases, bases, virtuals, fields and ordering in both excerpts; inspect suspicious differences in the original headers'
  limitations=@('Archive index presence does not establish actual linker selection or source-to-object identity.','Content differences and source excerpts do not establish ABI compatibility.','This report is not an acceptance receipt and does not authorize linking.')
}
$reportPath=Join-Path $OutputDir 'playerbot-api-inspection.json'
RejectReparsePoints $reportPath
if (Test-Path -LiteralPath $reportPath) { throw "Preserve the existing evidence report before repeating this audit: $reportPath" }
[void][IO.Directory]::CreateDirectory($OutputDir)
$utf8=New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 12),$utf8)
Write-Output "Evidence report: $reportPath"
Write-Output "dumpbin exit=$dumpbinExit; input hashes unchanged=$unchanged; class layout review remains required."
if ($dumpbinExit -ne 0 -or !$unchanged) { throw 'Inspection did not complete with stable inputs and a successful dumpbin exit; see the evidence report.' }
