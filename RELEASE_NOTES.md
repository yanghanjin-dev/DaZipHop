# DaZipHop v0.0.2

## 변경 사항

- GUI에 관리자 권한 설치·제거 버튼 추가
- UAC 승인 후 모든 사용자용 Program Files 설치
- 시작 메뉴 바로가기 및 Windows 설치된 앱 등록
- Windows 설정 또는 GUI에서 관리자 권한으로 제거
- 제거 시 프로그램 파일과 등록 정보만 삭제하고 추가 파일 보존
- 명령줄 설치·제거 옵션: `--install`, `--uninstall`

## Windows 실행 방법

`DaZipHop-v0.0.2-windows-x64.zip`을 풀고 `DaZipHop.exe`를 실행합니다.
`dazip.exe`는 같은 폴더에 두세요. GUI는 .NET Framework 4.x가 필요합니다.
설치는 선택 사항이며, 설치·제거 버튼을 사용할 때 관리자 권한 승인을 요청합니다.

포함된 압축 엔진 자체 버전은 dazip 1.0.0 / libarchive 3.8.1입니다.
암호화된 7z·RAR 해제는 지원하지 않습니다.

## 검증

- Windows GUI 빌드 및 14개 압축 형식의 한글·공백·하이픈 경로 왕복 테스트 통과
- ZIP 암호 검증, 잘못된 암호 거부, 공통 상위 폴더 계산 테스트 통과
- 실제 UAC 승인 및 시스템 설치·제거 동작은 미검증
