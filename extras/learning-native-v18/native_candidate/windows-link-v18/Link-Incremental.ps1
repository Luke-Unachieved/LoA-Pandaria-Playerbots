[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$PlanPath,
  [Parameter(Mandatory=$true)][string]$SourceRoot,
  [Parameter(Mandatory=$true)][string]$CompileRoot,
  [Parameter(Mandatory=$true)][string]$CandidateRoot,
  [string]$OverlayPath,
  [string]$WorkingDir,
  [string]$SpellEffectsAcceptance,
  [string]$PlayerbotAcceptance,
  [switch]$Link
)
# Windows PowerShell 5.1+. No installation, source overwrite, deployment,
# service start, database action, or formal-directory output.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Transform-LinkResponse.ps1')
function FullPath([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path) -or $Path -match '["%!?&|<>^\r\n]') { throw "Unsupported path: $Path" }
  return [IO.Path]::GetFullPath($Path)
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function RequireHash([string]$Path,[string]$Expected) {
  if ($Expected -notmatch '^[a-fA-F0-9]{64}$') { throw "Invalid expected SHA256: $Path" }
  if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing input: $Path" }
  if ((Hash $Path) -ne $Expected.ToLowerInvariant()) { throw "Input hash changed: $Path" }
}
function Under([string]$Path,[string]$Root) {
  return $Path.Equals($Root,[StringComparison]::OrdinalIgnoreCase) -or $Path.StartsWith(($Root.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)
}
function RejectOutputReparsePoints([string]$Path) {
  for ($current=$Path;$current;$current=Split-Path -Parent $current) {
    if ((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Output path traverses a reparse point: $current" }
  }
}
function ReadJson([string]$Path) { return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
function WriteJson([string]$Path,$Value) { [IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 18),$script:utf8) }
$script:locks=@{}
function LockFile([string]$Path,[string]$Kind,[string]$Expected='') {
  $Path=FullPath $Path
  if ($Expected) { RequireHash $Path $Expected }
  if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing $Kind input: $Path" }
  $hash=Hash $Path
  if ($script:locks.ContainsKey($Path) -and $script:locks[$Path].sha256 -ne $hash) { throw "Input changed during validation: $Path" }
  $script:locks[$Path]=[pscustomobject]@{path=$Path;sha256=$hash;kind=$Kind}
  return $hash
}
function ResolveInput([string]$Token,[string]$Root,[string[]]$LibDirs) {
  $value=$Token.Trim('"')
  if ([IO.Path]::IsPathRooted($value)) { return FullPath $value }
  $local=FullPath (Join-Path $Root $value)
  if ($value -match '[\\/]' -or (Test-Path -LiteralPath $local -PathType Leaf)) { return $local }
  foreach ($dir in $LibDirs) {
    $path=FullPath (Join-Path $dir $value)
    if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
  }
  throw "Cannot resolve linker input through cwd/LIB: $value"
}
function PlayerbotHeaderBinding($SourceObject,[string]$BindingHeader,[string]$LiteralInclude,[bool]$AllowForced) {
  $forced=@(Get-LinkTokens ([IO.File]::ReadAllText($SourceObject.compile_response)) | Where-Object { $_ -match '^[-/]FI' } | ForEach-Object { FullPath (([regex]::Replace($_,'^[-/]FI','','IgnoreCase')).Trim('"')) })
  if ($AllowForced -and $forced.Count -eq 1 -and $forced[0] -eq $BindingHeader) { return 'compiler-forced-include' }
  $text=[IO.File]::ReadAllText($SourceObject.source)
  $includes=[regex]::Matches($text,'(?m)^[ \t]*#[ \t]*include[ \t]+(?:"(?<quoted>[^"\r\n]+)"|<(?<system>[^>\r\n]+)>)')
  if ($forced.Count -ne 0 -or $includes.Count -eq 0 -or $includes[0].Groups['quoted'].Value -cne $LiteralInclude) { throw "Expected first literal $LiteralInclude include with no forced header: $($SourceObject.source)" }
  $included=FullPath (Join-Path (Split-Path -Parent $SourceObject.source) $includes[0].Groups['quoted'].Value)
  if ($included -ne $BindingHeader -or $text.Substring(0,$includes[0].Index) -notmatch '(?m)^[ \t]*class[ \t]+Creature[ \t]*;[ \t]*$') { throw "The first native header must be the accepted PlayerbotMgr.h after the Creature declaration: $($SourceObject.source)" }
  return 'source-first-relative-include'
}

$utf8=New-Object Text.UTF8Encoding($false)
$PlanPath=FullPath $PlanPath;$SourceRoot=FullPath $SourceRoot
$CompileRoot=FullPath $CompileRoot;$CandidateRoot=FullPath $CandidateRoot
if (!$OverlayPath) { $OverlayPath=Join-Path (Split-Path -Parent $PlanPath) 'link-overlay.json' }
$OverlayPath=FullPath $OverlayPath
$plan=ReadJson $PlanPath;$overlay=ReadJson $OverlayPath
if ($plan.schema -ne 'native-shadow-incremental-compile-v1' -or @($plan.jobs).Count -ne 16) { throw 'Expected the complete 16-job V18 compile plan.' }
$formal=FullPath $plan.formal_exe
if ($formal -ne 'E:\WoW548\Server\worldserver.exe' -or $plan.formal_sha256 -ne 'f509e005ddf59360f8c3f7eedb4c1396a2b145f6ad4a17c54fe553fb1dbc0223') { throw 'Expected the protected F509 formal path and baseline.' }
if ((Under $CandidateRoot (Split-Path -Parent $formal)) -or (Under $CandidateRoot $SourceRoot) -or $CandidateRoot -eq $CompileRoot) { throw 'Use a dedicated candidate directory outside formal/source/compile roots.' }
foreach ($name in @('Link.rsp','validation-receipt.json','link.log','link-receipt.json','worldserver-v18.exe','worldserver-v18.map','worldserver-v18.lib','worldserver-v18.exp')) { RejectOutputReparsePoints (Join-Path $CandidateRoot $name) }
[void](LockFile $formal 'formal-exe' $plan.formal_sha256)
[void](LockFile $PlanPath 'compile-plan');[void](LockFile $OverlayPath 'link-overlay')
[void](LockFile $PSCommandPath 'link-entry');[void](LockFile (Join-Path $PSScriptRoot 'Transform-LinkResponse.ps1') 'transform')
$baseRsp=FullPath $overlay.base_link_response
$baseInputs=FullPath $overlay.base_explicit_inputs
if ($overlay.base_link_response_sha256 -ne '146c90c86994c130519b2379713a9dfb49aaf49263830321caa8dda0c0bada2a' -or $overlay.base_explicit_inputs_sha256 -ne '919e3a658fa14f5e80ae2103762f6194d0bc9ffe895df9da7225bc1820fb7fbf') { throw 'Expected the audited original V15 response/input manifest.' }
[void](LockFile $baseRsp 'original-response' $overlay.base_link_response_sha256)
[void](LockFile $baseInputs 'original-object-manifest' $overlay.base_explicit_inputs_sha256)
$original=[IO.File]::ReadAllText($baseRsp);$originalTokens=Get-LinkTokens $original
$manifest=@(ReadJson $baseInputs)
if ($manifest.Count -ne 88) { throw 'Expected 88 original explicit objects.' }
$originalSeen=@{}
foreach ($item in $manifest) {
  if ($originalSeen.ContainsKey($item.path)) { throw 'Duplicate original object manifest entry.' }
  $originalSeen[$item.path]=$true
  if (@($originalTokens | Where-Object { $_ -ceq ('"'+$item.path+'"') }).Count -ne 1) { throw "Original object must appear exactly once, quoted: $($item.path)" }
  [void](LockFile $item.path 'original-explicit-object' $item.sha256)
}
$compiled=ReadJson (Join-Path $CompileRoot 'compile-results.json')
[void](LockFile (Join-Path $CompileRoot 'compile-results.json') 'compile-receipt')
if ($compiled.schema -ne 'native-shadow-compile-receipt-v1' -or $compiled.formal_unchanged -ne $true -or @($compiled.jobs).Count -ne 16) { throw 'A merged successful receipt for all 16 compile jobs is required.' }
foreach ($file in $plan.source_files) {
  $path=FullPath (Join-Path $SourceRoot $file.path)
  if (!(Under $path $SourceRoot)) { throw 'Reviewed source escapes SourceRoot.' }
  [void](LockFile $path 'reviewed-source-or-header' $file.sha256)
}
if ($plan.PSObject.Properties.Name -contains 'native_headers') {
  foreach ($header in $plan.native_headers) { [void](LockFile $header.path 'native-header' $header.sha256) }
}
$sourceObjects=@();$jobsSeen=@{}
foreach ($job in $plan.jobs) {
  if ($job.name -notmatch '^[a-zA-Z0-9_-]+$' -or $jobsSeen.ContainsKey($job.name)) { throw 'Invalid/duplicate compile job.' }
  $jobsSeen[$job.name]=$true
  $receipt=@($compiled.jobs | Where-Object { $_.name -ceq $job.name })
  $entry=@($overlay.object_overlays | Where-Object { $_.job -ceq $job.name })
  if ($receipt.Count -ne 1 -or $entry.Count -ne 1) { throw "Missing/duplicate receipt or overlay for $($job.name)" }
  if ($receipt[0].exit -ne 0 -or $receipt[0].compiled -ne $true -or $receipt[0].source_sha256 -ne $job.source_sha256) { throw "Unsuccessful or stale compile receipt: $($job.name)" }
  $source=FullPath (Join-Path $SourceRoot $job.source)
  $object=FullPath (Join-Path $CompileRoot $entry[0].new_object)
  if (!(Under $source $SourceRoot) -or !(Under $object $CompileRoot)) { throw 'Source/object escapes its reviewed root.' }
  [void](LockFile $source 'compiled-source' $job.source_sha256)
  [void](LockFile $object 'compiled-object' $receipt[0].object_sha256)
  [void](LockFile $job.base_source 'original-compile-source' $job.base_source_sha256)
  [void](LockFile $job.base_response 'original-compile-response' $job.base_response_sha256)
  $rsp=FullPath (Join-Path $CompileRoot ($job.name+'.rsp'))
  if (($receipt[0].PSObject.Properties.Name -notcontains 'response_sha256') -or $receipt[0].response_sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw "Missing/invalid compile response hash in receipt: $($job.name)" }
  [void](LockFile $rsp 'actual-compile-response' $receipt[0].response_sha256)
  $compileText=[IO.File]::ReadAllText($rsp)
  if (@(Get-LinkTokens $compileText | Where-Object { $_ -ceq ('"'+$source+'"') }).Count -ne 1 -or @([regex]::Matches($compileText,'(?i)(?<!\S)[-/]Fo"'+[regex]::Escape($object)+'"(?!\S)')).Count -ne 1) { throw "Compile response source/object mismatch: $rsp" }
  $sourceObjects += [pscustomobject]@{job=$job.name;source=$source;source_sha256=$job.source_sha256;object=$object;object_sha256=$receipt[0].object_sha256;compile_response=$rsp;compile_response_sha256=$receipt[0].response_sha256}
}

# Import only the already installed, pinned toolchain environment; nothing is installed.
$vcvars=FullPath $plan.vcvars
[void](LockFile $vcvars 'existing-vcvars')
$environmentLines=& $env:ComSpec /d /c ('call "'+$vcvars+'" -vcvars_ver=14.44 10.0.22621.0 >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'Existing VS14.44/SDK10.0.22621 environment failed.' }
$buildEnv=@{}
foreach ($line in $environmentLines) { if ($line -match '^([^=]+)=(.*)$') { $buildEnv[$matches[1]]=$matches[2] } }
if ($buildEnv['VCToolsVersion'] -notmatch '^14\.44\.' -or $buildEnv['WindowsSDKVersion'].TrimEnd('\') -ne '10.0.22621.0') { throw 'Wrong installed compiler/SDK version.' }
$linkExe=$null
foreach ($dir in ($buildEnv['PATH'] -split ';')) {
  if ($dir -and (Test-Path -LiteralPath (Join-Path $dir 'link.exe') -PathType Leaf)) { $linkExe=FullPath (Join-Path $dir 'link.exe');break }
}
if (!$linkExe -or $linkExe -notmatch '\\14\.44\.[^\\]+\\bin\\Hostx64\\x64\\link\.exe$') { throw 'Expected installed x64 VS14.44 link.exe.' }
[void](LockFile $linkExe 'existing-linker')
$libDirs=@($buildEnv['LIB'] -split ';' | Where-Object { $_ } | ForEach-Object { FullPath $_ })
$relative=@($originalTokens | Where-Object { $_ -notmatch '^[-/]' -and $_.Trim('"') -match '[\\/].*\.(obj|res|lib)$' -and ![IO.Path]::IsPathRooted($_.Trim('"')) })
$cwdEvidence=Join-Path (Split-Path -Parent (Split-Path -Parent $baseInputs)) 'persistent-constraints-v1\isolated-link-plan.json'
[void](LockFile $cwdEvidence 'historic-working-directory-plan' 'e014d70426985cb5eb6faae936542b45a9fd390ec4cc5b7c0bdecaab322c0210')
$historical=ReadJson $cwdEvidence
$historicalRelative=@(Get-LinkTokens $historical.command | Where-Object { $_ -notmatch '^[-/]' -and $_.Trim('"') -match '[\\/].*\.(obj|res|lib)$' -and ![IO.Path]::IsPathRooted($_.Trim('"')) })
if (($relative -join "`n") -cne ($historicalRelative -join "`n")) { throw 'Historical working-directory plan has different relative input tokens.' }
if (!$WorkingDir) {
  # The second root is only a compile include clue, never an assumed link cwd.
  $roots=@($historical.cwd)
  foreach ($job in $plan.jobs) {
    foreach ($match in [regex]::Matches([IO.File]::ReadAllText($job.base_response),'(?im)(?<!\S)-I"?([^"\r\n]*\\Build\\[^"\r\n\s]+)"?(?!\S)')) { $roots += $match.Groups[1].Value }
  }
  $resolvers=@($roots | Select-Object -Unique | Where-Object {
    $root=$_;@($relative | Where-Object { !(Test-Path -LiteralPath (Join-Path $root $_.Trim('"')) -PathType Leaf) }).Count -eq 0
  })
  if ($resolvers.Count -ne 1 -or $resolvers[0] -ne $historical.cwd) { throw 'Link cwd is missing or ambiguous. Supply -WorkingDir using the actual local V15 recipe and verify its inputs.' }
  $WorkingDir=$resolvers[0];$cwdSelection='historic-plan-and-complete-local-resolution'
} else { $cwdSelection='explicit-operator-selection' }
$WorkingDir=FullPath $WorkingDir
if (!(Test-Path -LiteralPath $WorkingDir -PathType Container) -or (Under $CandidateRoot $WorkingDir)) { throw 'WorkingDir must exist; CandidateRoot must be outside it.' }
$transformed=Convert-LinkResponse $original $overlay $CompileRoot $CandidateRoot
$linkInputs=@();$resolvedObjects=@{}
foreach ($token in $transformed.tokens) {
  if ($token -match '^[-/]' -or $token.Trim('"') -notmatch '(?i)\.(obj|res|lib)$') { continue }
  $path=ResolveInput $token $WorkingDir $libDirs
  if ($path -match '(?i)\.obj$') {
    if ($resolvedObjects.ContainsKey($path)) { throw "Duplicate resolved object: $path" };$resolvedObjects[$path]=$true
  }
  $hash=LockFile $path 'resolved-link-input'
  $linkInputs += [pscustomobject]@{token=$token;path=$path;sha256=$hash}
}
$gamePath=FullPath (Join-Path $WorkingDir 'src\server\game\game.lib')
if (@($linkInputs | Where-Object { $_.path -eq $gamePath }).Count -ne 2) { throw 'Expected both original game.lib references.' }

# Absence of an acceptance file allows preparation, but never native LINK.
$acceptanceVerified=$false
if ($SpellEffectsAcceptance) {
  $SpellEffectsAcceptance=FullPath $SpellEffectsAcceptance
  [void](LockFile $SpellEffectsAcceptance 'spelleffects-acceptance')
  $accept=ReadJson $SpellEffectsAcceptance
  if ($accept.schema -ne 'native-v18-spelleffects-acceptance-v1' -or $accept.decision -ne 'accepted' -or !$accept.reviewer -or !$accept.rationale -or $accept.formal_sha256 -ne $plan.formal_sha256) { throw 'Invalid SpellEffects audit acceptance.' }
  if ((FullPath $accept.game_library.path) -ne $gamePath) { throw 'SpellEffects audit refers to another game.lib resolver.' }
  [void](LockFile $gamePath 'accepted-game-library' $accept.game_library.sha256)
  [void](LockFile $accept.current_source.path 'accepted-current-spelleffects-source' $accept.current_source.sha256)
  $spell=@($sourceObjects | Where-Object job -eq 'native_spell_effects')[0]
  if ($accept.prepared_source_sha256 -ne $spell.source_sha256) { throw 'SpellEffects acceptance predates the compiled candidate source.' }
  foreach ($kind in @('current-source-to-library','minimal-delta-review')) {
    $evidence=@($accept.evidence | Where-Object kind -eq $kind)
    if ($evidence.Count -ne 1) { throw "One $kind evidence artifact is required." }
    [void](LockFile $evidence[0].path ('audit-'+$kind) $evidence[0].sha256)
  }
  foreach ($evidence in $accept.evidence) { [void](LockFile $evidence.path ('audit-'+$evidence.kind) $evidence.sha256) }
  $binding=ReadJson (@($accept.evidence | Where-Object kind -eq 'current-source-to-library')[0].path)
  if ((FullPath $binding.game_library.path) -ne $gamePath -or $binding.game_library.sha256 -ne $accept.game_library.sha256 -or !$binding.archive_member.name -or $binding.archive_member.sha256 -ne $binding.standalone_object.sha256) { throw 'Archive member/standalone object identity is not established.' }
  [void](LockFile $binding.archive_member.extracted_path 'accepted-extracted-member' $binding.archive_member.sha256)
  [void](LockFile $binding.standalone_object.path 'accepted-standalone-object' $binding.standalone_object.sha256)
  if ($binding.source_binding.kind -notin @('recompiled-object-byte-match','recompiled-linkable-content-match','embedded-codeview-source-checksum-match')) { throw 'Unaccepted source-to-object proof method.' }
  [void](LockFile $binding.source_binding.report.path 'accepted-source-binding-report' $binding.source_binding.report.sha256)
  $proof=ReadJson $binding.source_binding.report.path
  if ((FullPath $proof.source.path) -ne (FullPath $accept.current_source.path) -or $proof.source.sha256 -ne $accept.current_source.sha256 -or $proof.object_sha256 -ne $binding.archive_member.sha256 -or $proof.method -ne $binding.source_binding.kind -or $proof.accepted -ne $true) { throw 'Source-binding proof identities do not match the accepted source/member.' }
  $baseline=[IO.File]::ReadAllText($accept.current_source.path).Replace("`r`n","`n")
  $reconstructed=[IO.File]::ReadAllText($spell.source).Replace("`r`n","`n")
  foreach ($addedLine in @('#include "InterruptObserver.h"','                InterruptObserverV17::EffectScope interruptObservation(this, unitTarget, spell, i);','                interruptObservation.Complete(); // evidence only; never changes spell outcome')) {
    $pattern='(?m)^'+[regex]::Escape($addedLine)+"`n"
    if ([regex]::Matches($reconstructed,$pattern).Count -ne 1) { throw 'Expected exactly the three reviewed SpellEffects additions.' }
    $reconstructed=[regex]::Replace($reconstructed,$pattern,'')
  }
  if ($reconstructed -cne $baseline) { throw 'Removing the three observer additions does not reproduce current SpellEffects source.' }
  $acceptanceVerified=$true
}
$playerbotVerified=$false;$playerbotBinding=$null;$playerbotOldMemberLeaf=$null
$playerbotTargets=@(
  [pscustomobject]@{name='PlayerbotHolder::IsManagedLogin';symbol='\?IsManagedLogin@PlayerbotHolder@@\S+'},
  [pscustomobject]@{name='PlayerbotHolder::IsRaidOwnedBot';symbol='\?IsRaidOwnedBot@PlayerbotHolder@@\S+'},
  [pscustomobject]@{name='PlayerbotHolder::PlayerbotHolder';symbol='\?\?0PlayerbotHolder@@\S+'},
  [pscustomobject]@{name='PlayerbotHolder::AddPlayerBot';symbol='\?AddPlayerBot@PlayerbotHolder@@\S+'},
  [pscustomobject]@{name='PlayerbotHolder::GetPlayerBotsSnapshot';symbol='\?GetPlayerBotsSnapshot@PlayerbotHolder@@\S+'}
)
if ($PlayerbotAcceptance) {
  $PlayerbotAcceptance=FullPath $PlayerbotAcceptance
  [void](LockFile $PlayerbotAcceptance 'playerbot-api-acceptance')
  $api=ReadJson $PlayerbotAcceptance
  $modulesPath=FullPath (Join-Path $WorkingDir 'modules\modules.lib')
  if ($api.schema -ne 'native-v18-playerbot-api-acceptance-v2' -or $api.decision -ne 'accepted' -or !$api.reviewer -or !$api.rationale -or (FullPath $api.modules_library.path) -ne $modulesPath) { throw 'Invalid explicit PlayerbotMgr implementation acceptance.' }
  [void](LockFile $modulesPath 'accepted-modules-library' $api.modules_library.sha256)
  [void](LockFile $api.binding_header.path 'accepted-playerbot-api-header' $api.binding_header.sha256)
  [void](LockFile $api.active_header.path 'accepted-active-playerbot-header' $api.active_header.sha256)
  [void](LockFile $api.provided_source.path 'accepted-provided-playerbot-source' $api.provided_source.sha256)
  [void](LockFile $api.baseline_source.path 'accepted-module-baseline-playerbot-source' $api.baseline_source.sha256)
  [void](LockFile $api.baseline_object.path 'accepted-module-baseline-playerbot-object' $api.baseline_object.sha256)
  if (!$api.baseline_object.archive_member) { throw 'The reviewed modules.lib baseline member name is required.' }
  $playerbotOldMemberLeaf=[IO.Path]::GetFileName($api.baseline_object.archive_member.Replace('/','\'))
  if (!$playerbotOldMemberLeaf -or $playerbotOldMemberLeaf -notmatch '(?i)\.obj$' -or $playerbotOldMemberLeaf -eq 'native_playerbot_mgr.obj') { throw 'Invalid baseline PlayerbotMgr archive member name.' }
  $bindingHeader=FullPath $api.binding_header.path
  $scheduler=@($sourceObjects | Where-Object job -eq 'NativeBackgroundTraining')[0]
  $manager=@($sourceObjects | Where-Object job -eq 'native_playerbot_mgr')
  if ($manager.Count -ne 1 -or $api.implementation.job -cne 'native_playerbot_mgr') { throw 'The complete explicit native_playerbot_mgr implementation is required.' }
  $manager=$manager[0]
  foreach ($key in @('source','object','compile_response')) {
    $accepted=$api.implementation.$key
    if ((FullPath $accepted.path) -ne $manager.$key -or $accepted.sha256 -ne $manager.($key+'_sha256')) { throw "Playerbot implementation acceptance predates the actual compiled $key." }
  }
  # The genuine provided source must reproduce the complete TU after removing
  # only the include-order prelude; the parent separately reviews its active baseline delta.
  $provided=[IO.File]::ReadAllText($api.provided_source.path).Replace("`r`n","`n")
  $reconstructed=[IO.File]::ReadAllText($manager.source).Replace("`r`n","`n")
  $prelude="// Complete Creature before Player.h -> ConditionMgr.h in this native include order.`nclass Creature;`n"
  $pattern='(?m)^'+[regex]::Escape($prelude)+'(?=#include "PlayerbotMgr.h")'
  if ([regex]::Matches($reconstructed,$pattern).Count -ne 1) { throw 'Expected exactly the reviewed PlayerbotMgr include-order prelude immediately before its first native include.' }
  $reconstructed=[regex]::Replace($reconstructed,$pattern,'')
  if ($reconstructed -cne $provided) { throw 'Prepared PlayerbotMgr implementation does not reproduce the accepted provided source after removing its prelude.' }
  $bindingMode=PlayerbotHeaderBinding $scheduler $bindingHeader '../native-api/PlayerbotMgr.h' $true
  $managerBindingMode=PlayerbotHeaderBinding $manager $bindingHeader 'PlayerbotMgr.h' $false
  $playerbotBinding=[pscustomobject]@{mode=$bindingMode;header=$bindingHeader;header_sha256=$api.binding_header.sha256;source=$scheduler.source;source_sha256=$scheduler.source_sha256;compile_response=$scheduler.compile_response;
    implementation=[pscustomobject]@{mode=$managerBindingMode;job=$manager.job;source=$manager.source;source_sha256=$manager.source_sha256;object=$manager.object;object_sha256=$manager.object_sha256;compile_response=$manager.compile_response;compile_response_sha256=$manager.compile_response_sha256};
    baseline_source=$api.baseline_source;baseline_object=$api.baseline_object;provided_source=$api.provided_source}
  foreach ($kind in @('native-api-symbols','class-layout-review','baseline-source-binding','module-member-preservation')) {
    $evidence=@($api.evidence | Where-Object kind -eq $kind)
    if ($evidence.Count -ne 1) { throw "One $kind PlayerbotMgr audit artifact is required." }
    [void](LockFile $evidence[0].path ('audit-'+$kind) $evidence[0].sha256)
  }
  foreach ($evidence in $api.evidence) { [void](LockFile $evidence.path ('audit-'+$evidence.kind) $evidence.sha256) }
  $symbolReport=[IO.File]::ReadAllText(@($api.evidence | Where-Object kind -eq 'native-api-symbols')[0].path)
  foreach ($target in $playerbotTargets) {
    # Real dumpbin /symbols definitions have a section and External storage;
    # archive-index strings and UNDEF references cannot satisfy this gate.
    $pattern='(?m)^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+SECT[0-9A-Fa-f]+\s+[^|\r\n]*\bExternal\s+\|\s+'+$target.symbol+'(?=\s|$)'
    if ([regex]::Matches($symbolReport,$pattern).Count -ne 1) { throw "Explicit PlayerbotMgr object lacks a unique actual definition record for $($target.name)." }
  }
  $playerbotVerified=$true
}

$rspOut=Join-Path $CandidateRoot 'Link.rsp'
$validationPath=Join-Path $CandidateRoot 'validation-receipt.json'
$lockList=@($locks.Values | Sort-Object path)
$snapshot=[pscustomobject]@{schema='native-v18-link-validation-v1';working_dir=$WorkingDir;cwd_selection=$cwdSelection;
  historic_cwd_evidence=$cwdEvidence;historic_record_is_execution_proof=$false;toolchain=[pscustomobject]@{link_exe=$linkExe;vc_tools=$buildEnv['VCToolsVersion'];sdk=$buildEnv['WindowsSDKVersion'];lib_search_paths=$libDirs};
  original_explicit_objects=88;replacement_objects=8;added_objects=8;candidate_explicit_objects=96;candidate_total_objects=102;
  source_objects=$sourceObjects;link_inputs=$linkInputs;input_locks=$lockList;spelleffects_acceptance_verified=$acceptanceVerified;playerbot_api_acceptance_verified=$playerbotVerified;playerbot_api_binding=$playerbotBinding;
  native_link_executed=$false;deployed=$false;server_started=$false;runtime_collection_verified=$false;model_activated=$false}
if (!$Link) {
  foreach ($file in $lockList) { if (Under $file.path $CandidateRoot) { throw 'CandidateRoot overlaps an input/evidence file.' } }
  [IO.Directory]::CreateDirectory($CandidateRoot)|Out-Null
  [IO.File]::WriteAllText($rspOut,$transformed.text,$utf8)
  $snapshot | Add-Member NoteProperty transformed_response_sha256 (Hash $rspOut)
  WriteJson $validationPath $snapshot
  Write-Output "Validated 16 compiled jobs; wrote canonical Link.rsp and input locks. No LINK executed. SpellEffects acceptance: $acceptanceVerified; explicit PlayerbotMgr acceptance: $playerbotVerified"
  return
}
if (!$acceptanceVerified) { throw 'LINK requires tool-supported SpellEffects provenance/minimal-delta audit acceptance; no boolean bypass exists.' }
if (!$playerbotVerified) { throw 'LINK requires explicit PlayerbotMgr definitions, actual source/member provenance, preservation and class-layout audit acceptance.' }
$prior=ReadJson $validationPath
if ($prior.schema -ne $snapshot.schema -or $prior.working_dir -ne $WorkingDir -or $prior.spelleffects_acceptance_verified -ne $true -or $prior.playerbot_api_acceptance_verified -ne $true) { throw 'Run validation with these same parameters and accepted evidence before -Link.' }
if (($prior.input_locks|ConvertTo-Json -Depth 5 -Compress) -cne ($lockList|ConvertTo-Json -Depth 5 -Compress)) { throw 'Inputs changed after validation; review and rerun validation.' }
RequireHash $rspOut $prior.transformed_response_sha256
if ([IO.File]::ReadAllText($rspOut) -cne $transformed.text) { throw 'Prepared response differs from the canonical transformation.' }
foreach ($extension in @('exe','map','lib','exp')) {
  if (Test-Path -LiteralPath (Join-Path $CandidateRoot ('worldserver-v18.'+$extension))) { throw 'Candidate output already exists. Use a fresh CandidateRoot to avoid stale output claims.' }
}
$linkReceipt=[ordered]@{schema='native-v18-link-receipt-v1';started_utc=[DateTime]::UtcNow.ToString('o');working_dir=$WorkingDir;
  response=$rspOut;response_sha256=(Hash $rspOut);validation_sha256=(Hash $validationPath);native_link_executed=$false;exit=$null;
  linked=$false;outputs=@();formal_unchanged=$false;input_locks_unchanged=$false;deployed=$false;server_started=$false;runtime_collection_verified=$false;model_activated=$false}
$linkReceipt.spelleffects_providers_verified=$false
$linkReceipt.spelleffects_map_lines=@()
$linkReceipt.playerbot_providers_verified=$false
$linkReceipt.playerbot_map_lines=@()
$linkReceipt.playerbot_archive_provider_lines=@()
$linkReceipt.candidate_accepted=$false
$linkReceipt.failure=$null
try {
  $start=New-Object Diagnostics.ProcessStartInfo
  $start.FileName=$linkExe;$start.Arguments='@"'+$rspOut+'" /VERBOSE:LIB';$start.WorkingDirectory=$WorkingDir
  $linkReceipt.arguments=$start.Arguments
  $start.UseShellExecute=$false;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true;$start.CreateNoWindow=$true
  foreach ($key in $buildEnv.Keys) { $start.EnvironmentVariables[$key]=$buildEnv[$key] }
  $process=New-Object Diagnostics.Process;$process.StartInfo=$start
  [void]$process.Start();$linkReceipt.native_link_executed=$true
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $process.WaitForExit();$linkReceipt.exit=$process.ExitCode
  [IO.File]::WriteAllText((Join-Path $CandidateRoot 'link.log'),($stdout.Result+"`r`n"+$stderr.Result),$utf8)
  foreach ($extension in @('exe','map','lib','exp')) {
    $path=Join-Path $CandidateRoot ('worldserver-v18.'+$extension)
    if (Test-Path -LiteralPath $path -PathType Leaf) { $linkReceipt.outputs += [pscustomobject]@{path=$path;sha256=(Hash $path);bytes=(Get-Item -LiteralPath $path).Length} }
  }
  $linkReceipt.linked=$process.ExitCode -eq 0 -and (Test-Path -LiteralPath (Join-Path $CandidateRoot 'worldserver-v18.exe') -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $CandidateRoot 'worldserver-v18.map') -PathType Leaf)
  if (!$linkReceipt.linked) { throw 'Native LINK failed or required EXE/MAP outputs are missing. Inspect link.log.' }
  $mapLines=Get-Content -LiteralPath (Join-Path $CandidateRoot 'worldserver-v18.map')
  $targets=@(
    [pscustomobject]@{name='Spell::EffectInterruptCast';pattern='^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]+\s+\?EffectInterruptCast@Spell@@\S+\s+[0-9A-Fa-f]+\s+f(?:\s|$)'},
    [pscustomobject]@{name='SpellEffects';pattern='^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]+\s+\?SpellEffects@@\S+\s+[0-9A-Fa-f]+(?:\s|$)'}
  )
  foreach ($target in $targets) {
    # Match the decorated public-symbol column itself; $unwind$/$pdata$ rows are different symbols.
    $provider=@($mapLines | Where-Object { $_ -match $target.pattern })
    if ($provider.Count -ne 1 -or $provider[0] -notmatch '(?i)(?:^|[\\/\s:])native_spell_effects\.obj(?:\s|$)') { throw "Candidate MAP does not uniquely select native_spell_effects.obj for $($target.name). Inspect MAP and VERBOSE:LIB output; do not accept candidate." }
    $linkReceipt.spelleffects_map_lines += $provider[0]
  }
  $linkReceipt.spelleffects_providers_verified=$true
  foreach ($target in $playerbotTargets) {
    $pattern='^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]+\s+'+$target.symbol+'\s+[0-9A-Fa-f]+\s+f(?:\s|$)'
    $provider=@($mapLines | Where-Object { $_ -match $pattern })
    if ($provider.Count -ne 1 -or $provider[0] -notmatch '(?i)(?:^|[\\/\s:])native_playerbot_mgr\.obj(?:\s|$)') { throw "Candidate MAP does not uniquely select native_playerbot_mgr.obj for $($target.name). Inspect MAP and VERBOSE:LIB output; do not accept candidate." }
    $linkReceipt.playerbot_map_lines += $provider[0]
  }
  # Reject Holder public functions from the reviewed old archive member. Match
  # public symbols only, excluding $unwind$/$pdata$ metadata and other members' inline COMDATs.
  $oldProvider='(?i)(?:^|[\\/\s:])'+[regex]::Escape($playerbotOldMemberLeaf)+'(?:\s|$)'
  $linkReceipt.playerbot_archive_provider_lines=@($mapLines | Where-Object {
    $_ -match '^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]+\s+\?(?:\S*@PlayerbotHolder@@|\?(?:[0-9A-Z]|_[A-Z])PlayerbotHolder@@)\S*\s+[0-9A-Fa-f]+\s+f(?:\s|$)' -and $_ -match $oldProvider
  })
  if ($linkReceipt.playerbot_archive_provider_lines.Count -ne 0) { throw 'Candidate MAP mixes explicit PlayerbotHolder implementation with the old modules.lib Holder member. Candidate is not accepted.' }
  $linkReceipt.playerbot_providers_verified=$true
} catch {
  $linkReceipt.failure=$_.Exception.Message
  throw
} finally {
  $linkReceipt.formal_unchanged=(Test-Path -LiteralPath $formal -PathType Leaf) -and (Hash $formal) -eq $plan.formal_sha256
  $changed=@($lockList | Where-Object { !(Test-Path -LiteralPath $_.path -PathType Leaf) -or (Hash $_.path) -ne $_.sha256 })
  $linkReceipt.input_locks_unchanged=$changed.Count -eq 0
  if (!$linkReceipt.formal_unchanged -or !$linkReceipt.input_locks_unchanged) { $linkReceipt.failure='Inputs/formal EXE changed during link.' }
  $linkReceipt.candidate_accepted=$linkReceipt.linked -and $linkReceipt.spelleffects_providers_verified -and $linkReceipt.playerbot_providers_verified -and $linkReceipt.formal_unchanged -and $linkReceipt.input_locks_unchanged -and !$linkReceipt.failure
  $linkReceipt.finished_utc=[DateTime]::UtcNow.ToString('o')
  WriteJson (Join-Path $CandidateRoot 'link-receipt.json') $linkReceipt
  if (!$linkReceipt.formal_unchanged -or !$linkReceipt.input_locks_unchanged) { throw 'Inputs/formal EXE changed during link. Candidate is not accepted; coordinate with the input owner.' }
}
Write-Output 'Native candidate LINK completed; inspect the map/warnings/PE before any later runtime work. No deployment or server start occurred.'
