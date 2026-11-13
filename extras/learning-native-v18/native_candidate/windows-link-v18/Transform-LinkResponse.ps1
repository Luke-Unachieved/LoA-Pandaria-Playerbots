# Pure transformation: preserve the original token stream except eight exact
# quoted object replacements, seven prepended objects and three output switches.
function Get-LinkTokens([string]$Text) {
  return @([regex]::Matches($Text, '(?:[^\s"]+|"[^"\r\n]*")+') | ForEach-Object { $_.Value })
}
function Convert-LinkResponse([string]$Text, $Overlay, [string]$CompileRoot, [string]$CandidateRoot) {
  if ($Overlay.schema -ne 'native-v18-link-overlay-v1' -or @($Overlay.object_overlays).Count -ne 15) { throw 'Unexpected overlay schema/job count.' }
  $tokens=Get-LinkTokens $Text
  if (@($tokens | Where-Object { $_ -match '^@|^[-/](PDB|ILK|MANIFESTFILE|LTCGOUT|PGD|DEF|ORDER|WHOLEARCHIVE|LIBPATH):' }).Count) { throw 'Unexpected nested input, search path or side output in base response.' }
  if (@($tokens | Where-Object { $_ -match '^[-/]FORCE(?::|$)' }).Count) { throw 'Duplicate/unresolved-symbol override flags are forbidden.' }
  if ($Text -notmatch '(?i)(?<!\S)/DEBUG:NONE(?!\S)' -or $Text -notmatch '(?i)(?<!\S)/INCREMENTAL:NO(?!\S)') { throw 'Expected V15 no-PDB, non-incremental linker flags.' }
  $seen=@{};$replaced=0;$added=@();$changes=@()
  foreach ($entry in $Overlay.object_overlays) {
    if ($entry.job -notmatch '^[a-zA-Z0-9_-]+$' -or $entry.new_object -cne ($entry.job+'.obj') -or $seen.ContainsKey($entry.job)) { throw 'Invalid/duplicate overlay job.' }
    $seen[$entry.job]=$true
    $new='"'+(Join-Path $CompileRoot $entry.new_object)+'"'
    if ($entry.replaces_explicit_object) {
      $old='"'+$entry.replaces_explicit_object+'"'
      $matches=@(0..($tokens.Count-1) | Where-Object { $tokens[$_] -ceq $old })
      if ($matches.Count -ne 1) { throw "Expected one exact quoted token: $old" }
      $tokens[$matches[0]]=$new;$replaced++
    } else { $added += $new }
    $changes += [pscustomobject]@{job=$entry.job;old=$entry.replaces_explicit_object;new=$new.Trim('"')}
  }
  if ($replaced -ne 8 -or $added.Count -ne 7) { throw 'Expected eight replacements and seven additions.' }
  foreach ($option in @('OUT','MAP','IMPLIB')) {
    $matches=@(0..($tokens.Count-1) | Where-Object { $tokens[$_] -match ('^[-/]'+$option+':') })
    if ($matches.Count -ne 1) { throw "Expected one /$option token." }
    $extension=@{OUT='exe';MAP='map';IMPLIB='lib'}[$option]
    $tokens[$matches[0]]='/'+$option+':"'+(Join-Path $CandidateRoot ('worldserver-v18.'+$extension))+'"'
  }
  # Prepended objects precede every library, including the game.lib member they override.
  $tokens=@($added)+@($tokens)
  $objects=@($tokens | Where-Object { $_.Trim('"') -match '(?i)\.obj$' })
  if (@($objects | Group-Object { $_.Trim('"').ToLowerInvariant() } | Where-Object Count -ne 1).Count) { throw 'Duplicate object tokens.' }
  $explicit=@($objects | Where-Object { $_ -match '^"[A-Za-z]:[\\/]' })
  if ($Overlay.preserve_explicit_input_count -ne 88 -or $Overlay.new_explicit_count -ne 95 -or $explicit.Count -ne 95 -or $objects.Count -ne 101) { throw 'Expected 95 explicit and six relative core objects.' }
  return [pscustomobject]@{text=(($tokens -join "`r`n")+"`r`n");tokens=$tokens;changes=$changes;explicit_objects=$explicit.Count;total_objects=$objects.Count}
}
