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
바이너리 자체의 build identity는 아래 별도 검사로 대조한다.

## 실행 파일 묶음의 빌드 정보

GUI·client·server·Windows daemon은 `--build-info`를 단독 인자로 받으면
UI/설정/서비스/입력 엔진을 초기화하기 전에 JSON을 출력하고 종료한다.
다른 인자를 함께 주면 오류로 종료하며 정상 실행으로 이어지지 않는다.

```sh
python3 tools/release/test_verify_build.py
python3 tools/release/verify_build.py build-product/bin \
  --expected-revision "$(git rev-parse HEAD)" --platform Darwin \
  --output build-product/build-info.json
```

Windows는 실행 파일이 모인 staging 폴더와 `--platform Windows`를 사용한다.
빌드마다 필요한 구성 요소, 전체 Git revision, configure 시 생성한 build ID,
플랫폼·아키텍처·컴파일러·Release 설정이 모두 일치해야 한다. 빌드 때 Git 상태를
갱신하므로 configure 이후 소스를 수정한 개발 빌드도 dirty로 기록된다.
dirty는 기본 거부하며 개발용 `--allow-dirty`를 명시해도 receipt에 그대로 남는다.
커밋 후에는 다시 빌드해야 clean 상태와 새 revision이 실행 파일에 반영된다.

이 도구는 신뢰하는 소스로 **직접 빌드한 실행 파일에만** 사용한다. marker가 없는
옛 실행 파일은 실행하지 않지만, marker는 악성 파일을 방어하는 sandbox가 아니다.
metadata는 자기 보고이므로 소스의 암호학적 증명이나 재현 빌드 인증이 아니다.
검사 후 각 실행 파일 SHA256을 기록하며, 실제 설치·OS 입력 수용은 별도다.

GUI Mac 패키징은 복사 직후와 최종 서명 직후에 이 검사를 실행한다. 최종 결과는
`APP.build.json`에 기록한다. 개발 중 dirty 소스 묶음을 만들려면
`KEYSTITCH_ALLOW_DIRTY=1`을 명시해야 하며 CI는 이를 사용하지 않는다.

## CI 내부 후보 파일

product workflow는 테스트 후 다음 산출물을 만든다. GitHub Release를 발행하거나
설치 프로그램을 실행하지 않으며 사용자 컴퓨터의 앱을 교체하지 않는다.

- Mac: 동일 빌드 3개 실행 파일을 포함한 앱 ZIP, Mach-O audit, build receipt,
  ZIP SHA256. macOS 26 / Qt 6.10.3, 기존 ad-hoc 서명이며 notarization 없음.
- Windows: 동일 빌드 4개 실행 파일로 만든 Inno 설치 EXE, build receipt, EXE SHA256.
  CMake staging에 Qt와 MSVC runtime을 포함하고 개발용 PATH 없이 build probe 실행.
  Inno 컴파일 시 주요 MSVC DLL과 Qt platform plugin 누락을 거부한다.
  오래된 VC2017 런타임 HTTP 다운로드는 제거했다. 설치 파일은 아직 서명하지 않는다.

Windows app-local runtime은 CMake의 `InstallRequiredSystemLibraries`로 모은다.
[Microsoft 배포 방식](https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method)
중 app-local 방식이며, 이 파일의 보안 업데이트를 제품 업데이트에서 관리해야 한다.
Qt 파일은 [windeployqt](https://doc.qt.io/qt-6.10/windows-deployment.html)가 모은다.
DLL 존재 검사와 `--build-info` 통과는 모든 동적 plugin·GUI 초기화·깨끗한 PC에서의
설치 성공을 입증하지 않는다. 기존 PC에 설치된 라이브러리의 영향을 배제하는
깨끗한 VM 수용 테스트, 제3자 라이선스 묶음, 서명은 공개 배포 전 남은 조건이다.

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

Windows CI는 `stage_bonjour_sdk.py`로 Bonjour SDK를 준비한다. 원본은
[mDNSResponder v2019.05.08.1](https://github.com/nelsonjchen/mDNSResponder/releases/tag/v2019.05.08.1)의
x64 RelWithDebInfo ZIP(자산 ID 12460054)이다. 저장소에 고정한 해시는 이
자산을 내려받아 계산했다. 다음 빌드에서 같은 바이트를 사용하는지 확인하고,
크기나 SHA256이 다르면 파일을 풀기 전에 중단한다. 보관해 둔 ZIP도
`python tools/release/stage_bonjour_sdk.py deps/BonjourSDKLike --archive SDK.zip`으로
같은 검사를 거쳐 사용할 수 있다.
