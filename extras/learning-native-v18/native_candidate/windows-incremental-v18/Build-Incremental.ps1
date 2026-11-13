[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$PlanPath,
  [Parameter(Mandatory=$true)][string]$SourceRoot,
  [Parameter(Mandatory=$true)][string]$OutputRoot,
  [switch]$Compile
)
# Compile reviewed text sources with the actual V15 response flags. No link,
# deployment, service, database, download, Python or source overwrite occurs.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function CheckedPath([string]$p) {
  if ([string]::IsNullOrWhiteSpace($p) -or $p -match '["%!?&|<>^\r\n]') { throw "Unsupported path: $p" }
  return [IO.Path]::GetFullPath($p)
}
function Hash([string]$p) { return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function RequireHash([string]$p,[string]$expected) {
  if (!(Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing input: $p" }
  if ((Hash $p) -ne $expected.ToLowerInvariant()) { throw "Input changed: $p" }
}
$PlanPath=CheckedPath $PlanPath; $SourceRoot=CheckedPath $SourceRoot; $OutputRoot=CheckedPath $OutputRoot
if ($OutputRoot -match '^[cC]:') { throw 'Use a non-C drive for output.' }
$plan=Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
if ($plan.schema -ne 'native-shadow-incremental-compile-v1') { throw 'Unknown plan schema.' }
$vcvars=CheckedPath $plan.vcvars
if (!(Test-Path -LiteralPath $vcvars -PathType Leaf)) { throw 'Existing vcvars64.bat is missing. Do not install another compiler.' }
$formal=CheckedPath $plan.formal_exe
RequireHash $formal $plan.formal_sha256
$protected=(Split-Path -Parent $formal).TrimEnd('\')+'\'
if (($OutputRoot.TrimEnd('\')+'\').StartsWith($protected,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must not be the formal server directory.' }
foreach ($dep in $plan.source_files) {
  $p=CheckedPath (Join-Path $SourceRoot $dep.path)
  if (!$p.StartsWith(($SourceRoot.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Source escapes reviewed root.' }
  RequireHash $p $dep.sha256
}
$prepared=@();$seen=@{}
foreach ($job in $plan.jobs) {
  if ($job.name -notmatch '^[a-zA-Z0-9_-]+$' -or $seen.ContainsKey($job.name)) { throw 'Invalid/duplicate job name.' }
  $seen[$job.name]=$true
  $rsp=CheckedPath $job.base_response; RequireHash $rsp $job.base_response_sha256
  $oldSource=CheckedPath $job.base_source; RequireHash $oldSource $job.base_source_sha256
  $source=CheckedPath (Join-Path $SourceRoot $job.source)
  RequireHash $source $job.source_sha256
  $lines=[IO.File]::ReadAllLines($rsp)
  $oldCount=0;$objectCount=0;$flags=@()
  foreach ($line in $lines) {
    $trimmed=$line.Trim()
    if ($trimmed.Trim('"') -eq $oldSource) { $oldCount++;continue }
    if ($trimmed -match '^[-/]Fo') { $objectCount++;continue }
    if ($trimmed -match '^[-/]Fd') { continue }
    if ($trimmed -match '^[-/]F[arp]') { throw "Review unexpected side-output flag in $rsp : $trimmed" }
    if ($trimmed -match '^[-/]Y[uc]') { throw 'Unexpected PCH dependency; use the V15 no-PCH response.' }
    if ($trimmed -match '^@' -or $trimmed -match '\.(cpp|cc|cxx)(\.candidate)?"?$') { throw "Unexpected source/nested response token: $trimmed" }
    $flags += $line
  }
  if ($oldCount -ne 1 -or $objectCount -ne 1) { throw "Ambiguous base response: $rsp" }
  foreach ($required in @('-std:c++20','-MD','-c')) {
    if ($flags -notcontains $required) { throw "Missing expected ABI/compile flag: $required" }
  }
  $includes=@()
  foreach ($dir in $job.include_dirs) {
    $resolved=CheckedPath (Join-Path $SourceRoot $dir)
    if (!(Test-Path -LiteralPath $resolved -PathType Container)) { throw "Missing include directory: $resolved" }
    $includes += '-I"'+$resolved+'"'
  }
  $obj=CheckedPath (Join-Path $OutputRoot ($job.name+'.obj'))
  $pdb=CheckedPath (Join-Path $OutputRoot ($job.name+'.pdb'))
  $prepared += [pscustomobject]@{name=$job.name;source=$source;object=$obj;
    response=(($includes+$flags+@('-Fo"'+$obj+'"','-Fd"'+$pdb+'"','"'+$source+'"')) -join "`r`n")+"`r`n"}
}
if (!$Compile) { Write-Output "Validated $($prepared.Count) compile jobs; no files were written and nothing was compiled.";return }
[IO.Directory]::CreateDirectory($OutputRoot)|Out-Null
$utf8=New-Object Text.UTF8Encoding($false)
$results=@()
try {
  foreach ($job in $prepared) {
    $rspOut=Join-Path $OutputRoot ($job.name+'.rsp')
    $cmdOut=Join-Path $OutputRoot ($job.name+'.cmd')
    $logOut=Join-Path $OutputRoot ($job.name+'.log')
    [IO.File]::WriteAllText($rspOut,$job.response,$utf8)
    $command=@('@echo off','setlocal DisableDelayedExpansion',
      ('call "'+$vcvars+'" -vcvars_ver=14.44 10.0.22621.0'),
      'if errorlevel 1 exit /b 1',
      ('cl.exe @"'+$rspOut+'" >"'+$logOut+'" 2>&1'),'exit /b %errorlevel%') -join "`r`n"
    [IO.File]::WriteAllText($cmdOut,$command+"`r`n",$utf8)
    $watch=[Diagnostics.Stopwatch]::StartNew()
    & $env:ComSpec /d /c ('"'+$cmdOut+'"')
    $code=$LASTEXITCODE;$watch.Stop()
    $ok=$code -eq 0 -and (Test-Path -LiteralPath $job.object -PathType Leaf)
    $results += [pscustomobject]@{name=$job.name;exit=$code;compiled=$ok;seconds=$watch.Elapsed.TotalSeconds;
      source_sha256=(Hash $job.source);object_sha256=$(if($ok){Hash $job.object}else{$null});log=$logOut}
    if (!$ok) { throw "Compile failed: $($job.name). Inspect $logOut" }
  }
} finally {
  $unchanged=(Hash $formal) -eq $plan.formal_sha256.ToLowerInvariant()
  $receipt=[pscustomobject]@{schema='native-shadow-compile-receipt-v1';
    jobs=$results;formal_unchanged=$unchanged;linked=$false;deployed=$false;server_started=$false;
    runtime_collection_verified=$false;model_activated=$false}
  [IO.File]::WriteAllText((Join-Path $OutputRoot 'compile-results.json'),($receipt|ConvertTo-Json -Depth 10),$utf8)
  if (!$unchanged) { throw 'Formal EXE changed during compile; coordinate with its owner before continuing.' }
}
Write-Output 'Compile complete. No link, deployment, server startup or model activation was performed.'
