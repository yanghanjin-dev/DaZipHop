$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe' }
& $compiler /nologo /target:winexe /optimize+ /codepage:65001 /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /out:"$PSScriptRoot\DaZipHop.exe" "$PSScriptRoot\DaZipHop.Gui.cs"
if ($LASTEXITCODE -ne 0) { throw 'GUI 빌드 실패' }
Write-Output "빌드 완료: $PSScriptRoot\DaZipHop.exe"
