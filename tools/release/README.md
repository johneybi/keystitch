# Mac 배포 묶음 검사

`audit_macos_bundle.py`는 앱을 실행하지 않고 `Info.plist`와 포함된 Mach-O 파일의
load command를 검사한다. macOS의 `otool`과 Python 3이 필요하다.

```sh
python3 tools/release/test_audit_macos_bundle.py
python3 tools/release/audit_macos_bundle.py '/path/to/staged.app' \
  --manifest '/path/to/staged.app.audit.json'
```

- 각 Mach-O slice와 framework의 최소 macOS가 plist의 선언보다 높으면 실패.
- `/opt/homebrew`, 사용자 빌드 폴더 등 외부 절대 경로에 의존하면 실패.
- 주 실행 파일이 없거나 실제 Mach-O가 아니면 실패.
- 서명 이후 생성한 외부 JSON에는 검사한 파일의 SHA256과 최소 OS·의존성 기록.
- receipt를 앱 내부에 저장하는 것은 거부한다. 서명된 앱 내용을 바꾸지 않는다.

`@rpath` 해석, 누락된 상대 의존성, ABI, 서명·notarization, 소스 revision 일치,
실제 구형 OS 실행은 이 도구가 증명하지 않는다. 결과 JSON의 `notVerified`에도 남긴다.

`dist/macos/package_keymap_gui.sh`와 `package_keymap_client.sh`는 서명 전에
검사하고, 기존 ad-hoc 서명 검증 후 receipt를 작성한다. 기존 출력 경로가 있으면
덮어쓰지 않으므로 새로운 **staging 경로**를 지정해야 한다.

`INPUTLEAP_MINIMUM_MACOS`는 실제 바이너리·의존성과 일치하는 값으로 지정한다.
기본값 11.0으로 검사에 실패하면 plist만 억지로 낮추지 않는다. 지원 하한을
낮추려면 해당 SDK·컴파일 옵션·의존성으로 다시 빌드하고 실제 OS에서 검증한다.

패키징 스크립트를 실행한 체크아웃의 revision은 GUI plist에
`KeyStitchPackagingCheckoutRevision`으로 기록한다. 이것은 **바이너리를 빌드한
revision의 증거가 아니다.** 이전 `KeyStitchSourceRevision` 표기를 사용하지 않는다.
서로 다른 빌드의 실행 파일 혼합을 검출할 build provenance는 후속 작업이다.

2026-11-18 로컬 검증:

- 순수 parser/정책 검사 5개 통과. universal slice, 구형 load command,
  잘못된 최소 버전, 외부 의존성, 비-macOS load command 포함.
- 실제 로컬 클라이언트 Mach-O의 26.0 선언 통과, 11.0 선언 실패 확인.
- 기존 시험용 앱 덮어쓰기 거부 및 원본 내용 보존 확인.
- GUI·클라이언트·서버를 모은 별도 개발 앱에서 50개 Mach-O 검사 통과,
  기존 ad-hoc 서명 방식의 `codesign --verify --deep --strict` 통과.

시험 묶음 위치는 `build-product/staging/KeyStitch-development.app`이며 실행하지
않았다. Qt 5 로컬 개발 산출물로, 소스 provenance·구형 OS·설치 수용·Developer ID
서명·notarization이 확인된 출시 파일이 아니다. 현재 `/Applications` 앱은 보존했다.
