# Check the Windows gasm bundle from tools/package-gasm.sh without game data (CI and release.yml run it on
# Windows): tools/smoke-gasm-bundle.ps1 <dir with openoni-gasm-...-windows-x86_64.zip>
# Unpacks it, checks the files, runs the bundled gasm-run without data (OpenOni's clean error, exit code 1)
# and the launcher's --help and --dry-run paths against fake data, as tools/smoke-gasm-bundle.sh does.
param([string]$Dist = 'dist')
$env:OPENONI_NO_PAUSE = '1'
$zip = Get-ChildItem (Join-Path $Dist 'openoni-gasm-*-windows-x86_64.zip') | Select-Object -First 1
Expand-Archive $zip.FullName -DestinationPath unpacked
$d = (Get-ChildItem unpacked -Directory | Select-Object -First 1).FullName
foreach ($f in 'gasm-run.exe', 'openoni.wasm', 'OpenOni.cmd', 'openoni.ps1', 'README.txt', 'LICENSE.txt', 'LICENSE-gasm.txt') {
  if (-not (Test-Path (Join-Path $d $f))) { throw "missing $f" }
}
# No data: OpenOni's clean error, exit code 1 (not a crash).
$out = & "$d\gasm-run.exe" "$d\openoni.wasm" --headless 5 --mute 2>&1 | Out-String
$rc = $LASTEXITCODE
$out
if ($rc -ne 1) { throw "gasm-run without data: exit $rc, expected 1" }
if ($out -notmatch 'game data not found') { throw "no 'game data not found' message" }
# The launcher (no dialogs: OPENONI_GAME / --dry-run, scratch APPDATA and LOCALAPPDATA).
$env:APPDATA = Join-Path $PWD 'appdata'; $env:LOCALAPPDATA = Join-Path $PWD 'localappdata'
$help = & "$d\OpenOni.cmd" --help | Out-String
if ($LASTEXITCODE -ne 0 -or $help -notmatch 'OpenOni') { throw '--help' }
# fakes: the CD's files (a space in the path), an installed game, an image (goes to the game as asset cd), and what must be refused
New-Item -ItemType Directory -Force 'my cd', installed\GameDataFolder, empty, wrongexe\GameDataFolder, no002, nogdf | Out-Null
New-Item -ItemType File -Force 'my cd\ONISETUP.EXE', 'my cd\ONISETUP.002', 'wrongexe\Oni.exe', 'no002\ONISETUP.EXE', 'Oni CD.iso', Oni.cue | Out-Null
foreach ($e in 'installed\Oni.exe', 'nogdf\Oni.exe') { $f = [IO.File]::Create((Join-Path $PWD $e)); $f.SetLength(1527808); $f.Close() }
$env:OPENONI_GAME = Join-Path $PWD 'installed'
$cmd = & "$d\OpenOni.cmd" --dry-run | Out-String
if ($LASTEXITCODE -ne 0 -or $cmd -notmatch '--asset-dir') { throw 'dry run with an installed folder' }
$env:OPENONI_GAME = Join-Path $PWD 'my cd'
$cmd = & "$d\OpenOni.cmd" --dry-run --mute | Out-String
$cmd
if ($LASTEXITCODE -ne 0 -or $cmd -notmatch '--asset-dir' -or $cmd -notmatch '--mute') { throw 'dry run with the CD files' }
$env:OPENONI_GAME = Join-Path $PWD 'Oni CD.iso'
$cmd = & "$d\OpenOni.cmd" --dry-run | Out-String
$cmd
if ($LASTEXITCODE -ne 0 -or $cmd -notmatch 'cd=' -or $cmd -match '--asset-dir') { throw 'dry run with an .iso' }
foreach ($bad in 'empty', 'wrongexe', 'no002', 'nogdf', 'Oni.cue', 'missing') {
  $env:OPENONI_GAME = Join-Path $PWD $bad
  & "$d\OpenOni.cmd" --dry-run 2>&1 | Out-String | Write-Host
  if ($LASTEXITCODE -ne 2) { throw "$bad accepted (exit $LASTEXITCODE, expected 2)" }
}
if (Test-Path (Join-Path $env:APPDATA 'OpenOni\data-location')) { throw 'a dry run saved the data location' }
$global:LASTEXITCODE = 0
'PASS'
exit 0
