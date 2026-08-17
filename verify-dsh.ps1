$dsh = 'C:\dsh-desktop-qt\deploy\runtime\dsh'
$nm = Join-Path $dsh 'node_modules'
$items = (Get-ChildItem $nm -Directory -ErrorAction SilentlyContinue | Measure-Object).Count
Write-Output ("node_modules top dirs: " + $items)
$ds = Join-Path $nm '@deepseek-ai'
if (Test-Path $ds) {
    $pkgs = (Get-ChildItem $ds -Directory | Measure-Object).Count
    Write-Output ("@deepseek-ai packages: " + $pkgs)
    Write-Output ("dsh pkg: " + (Test-Path (Join-Path $ds 'dsh\lib\bin.js')))
} else {
    Write-Output "@deepseek-ai MISSING"
}
$total = (Get-ChildItem $nm -Recurse -File -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
Write-Output ("node_modules size MB: " + [math]::Round($total/1MB))
