# DaZipHop v0.0.1

DaZipHop의 첫 공개 릴리즈입니다.

- Windows GUI에서 파일·폴더 압축 및 여러 압축파일 해제
- ZIP, 7z, TAR 및 gzip, bzip2, xz, zstd, lz4 등 지원
- ZIP AES-256 암호 압축 및 해제
- 한글·공백·하이픈 파일 이름 처리, 끌어다 놓기, 작업 기록
- Windows PowerShell 빌드·테스트 스크립트 한글 인코딩 수정

## Windows 실행 방법

`DaZipHop-v0.0.1-windows-x64.zip`을 풀고 `DaZipHop.exe`를 실행합니다.
`dazip.exe`는 같은 폴더에 두세요. GUI는 .NET Framework 4.x가 필요합니다.

배포 버전은 v0.0.1이며, 포함된 압축 엔진 자체 버전은 dazip 1.0.0 / libarchive 3.8.1입니다.
암호화된 7z·RAR 해제는 지원하지 않습니다.

## 검증

Windows GUI 빌드 및 14개 압축 형식의 한글·공백·하이픈 경로 왕복 테스트 통과.
ZIP 암호 검증, 잘못된 암호 거부, 공통 상위 폴더 계산 테스트 통과.
