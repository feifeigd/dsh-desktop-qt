$dsh = 'C:\dsh-test-install4\runtime\dsh'
$nm = Join-Path $dsh 'node_modules'
$items = (Get-ChildItem $nm -Directory -ErrorAction SilentlyContinue | Measure-Object).Count
Write-Output ("node_modules top dirs: " + $items)
$ds = Join-Path $nm '@deepseek-ai'
Write-Output ("@deepseek-ai packages: " + (Get-ChildItem $ds -Directory | Measure-Object).Count)
Write-Output ("dsh bin.js: " + (Test-Path (Join-Path $ds 'dsh\lib\bin.js')))
Write-Output ("koffi-win32: " + (Test-Path (Join-Path $nm '@koromix\koffi-win32-x64')))
Write-Output ("sharp-win32: " + (Test-Path (Join-Path $nm '@img\sharp-win32-x64')))
Write-Output ("node.exe: " + (Test-Path 'C:\dsh-test-install4\runtime\node\node.exe'))
Write-Output ("npm-cli: " + (Test-Path 'C:\dsh-test-install4\runtime\node\node_modules\npm\bin\npm-cli.js'))
Write-Output ("provision.js: " + (Test-Path 'C:\dsh-test-install4\runtime\tools\provision.js'))
Write-Output ("update.js: " + (Test-Path 'C:\dsh-test-install4\runtime\tools\update.js'))
Write-Output ("demo_plugin.dll: " + (Test-Path 'C:\dsh-test-install4\plugins\demo_plugin.dll'))
Write-Output ("tsplugin: " + (Test-Path 'C:\dsh-test-install4\tsplugins\dsh-desktop-demo\lib\index.js'))
