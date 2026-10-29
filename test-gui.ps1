$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
[void][Reflection.Assembly]::LoadFrom((Join-Path $PSScriptRoot 'DaZipHop.exe'))
$taskRoot = Join-Path ([IO.Path]::GetTempPath()) ('dazip-gui-' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $taskRoot '한글 폴더'
New-Item -ItemType Directory -Path $source -Force | Out-Null
$inputFile = Join-Path $source '-테스트 파일.txt'
[IO.File]::WriteAllText($inputFile, 'DaZipHop GUI 왕복 검증')
$engine = Join-Path $PSScriptRoot 'dazip.exe'
function Invoke-Engine([string[]]$Values) {
    $argsList = New-Object 'System.Collections.Generic.List[string]'
    $argsList.AddRange($Values)
    $result = [MainWindow]::RunEngine($engine, $source, $argsList)
    if ($result.Item1 -ne 0) { throw $result.Item2 }
}
foreach ($ext in @('zip', '7z', 'tar', 'tar.gz', 'tar.bz2', 'tar.xz', 'tar.zst', 'tar.lz4', 'tar.lzma', 'gz', 'bz2', 'xz', 'zst', 'lz4')) {
    $archive = Join-Path $taskRoot ('검증.' + $ext)
    Invoke-Engine @('c', '-f', $ext, '--', $archive, [IO.Path]::GetFileName($inputFile))
    $out = Join-Path $taskRoot ('out-' + $ext)
    Invoke-Engine @('x', '-C', $out, '--', $archive)
    $restored = @(Get-ChildItem -LiteralPath $out -File -Recurse)
    if ($restored.Count -ne 1 -or [IO.File]::ReadAllText($restored[0].FullName) -ne [IO.File]::ReadAllText($inputFile)) { throw "내용 불일치: $ext" }
    Write-Output "PASS $ext (한글/공백/하이픈 경로)"
}
$archive = Join-Path $taskRoot '암호.zip'
Invoke-Engine @('c', '-p', 'test secret', '--', $archive, [IO.Path]::GetFileName($inputFile))
Invoke-Engine @('t', '-p', 'test secret', '--', $archive)
$wrong = New-Object 'System.Collections.Generic.List[string]'
$wrong.AddRange([string[]]@('t', '-p', 'wrong', '--', $archive))
if ([MainWindow]::RunEngine($engine, $source, $wrong).Item1 -eq 0) { throw '잘못된 암호가 허용됨' }
Write-Output 'PASS ZIP 암호 및 잘못된 암호 거부'
if ([MainWindow]::CommonParent([string[]]@($inputFile, (Join-Path $taskRoot 'other.txt'))) -ne $taskRoot) { throw '공통 상위 폴더 오류' }
Write-Output 'PASS 공통 상위 폴더 계산'
Write-Output "테스트 결과 폴더: $taskRoot"
