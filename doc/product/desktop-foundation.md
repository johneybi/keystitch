# 배포 제품의 설정·엔진 경계

2026-11-18. 현재 설치 앱을 교체하지 않은 개발 결과다. 완성된 배포판이나
실제 Windows↔Mac 입력 수용 테스트 결과로 읽지 않는다.

## 이번 구현

제품의 Mac 한영 전환 선택이 실행 환경변수에만 의존하지 않도록 했다.
GUI의 Settings에서 선택한 방식은 기존 앱 프로필의
`keyboard/macInputSourceBackend`에 저장되고 다음 연결 시작 때 엔진에 전달된다.
설정 저장만으로 실행 중인 프로세스를 재시작하거나 Mac 입력 소스를 바꾸지 않는다.

| 구간 | 실제 코드와 책임 |
| --- | --- |
| 설정 계약 | `src/lib/base/MacInputSourceBackend.h`: 허용 값, 명시적 선택/환경변수/기본값 우선순위. Qt·OS 호출 없음 |
| 저장·실행 인수 | `src/lib/desktop/KeyboardSettings`: Qt Core만 사용. 읽기·검증·저장 실패와 실행 인수 생성 |
| 현재 프론트 | Qt Widgets SettingsDialog와 AppConfig가 공통 설정 계층을 사용 |
| 엔진 경계 | `--macos-ime-backend` → ArgsBase → ClientApp/ServerApp → OSXScreen → OSXKeyState |
| 후속 QML | 같은 저장·실행 계약을 QObject 어댑터로 노출할 예정. 기존 QML 미리보기에는 아직 연결하지 않음 |

GUI·설정 라이브러리를 빌드하지 않는 엔진은 Qt를 새로 요구하지 않는다.
Windows/macOS와 서버/클라이언트 역할은 별개다. 설정은 **이 Mac에서 실행할
엔진의 동작**이며 Windows에서 연결된 Mac의 설정을 원격 수정하는 기능이 아니다.

## 선택과 오류 처리

| 상황 | 결과 |
| --- | --- |
| 새 GUI에서 저장 값 없음 | `native-shortcut`를 명시적으로 전달. 조회만으로 설정을 쓰지는 않음 |
| GUI에서 저장 값 있음 | 저장된 `native-shortcut` 또는 `direct-tis` 전달 |
| 오래된 환경변수가 상속됨 | 명시적 CLI 선택이 우선. GUI 선택을 덮어쓰지 않음 |
| GUI의 알 수 없는 저장 값/읽기 오류 | Mac 엔진 시작 중단, 설정 오류 표시. 임의 fallback 없음 |
| 저장 실패 | 성공으로 표시하지 않고 대화상자를 유지. 새 값을 적용하지 않음 |
| Windows 또는 다른 플랫폼 | Mac 전용 인수를 전달하지 않음. Mac 전용 설정 UI 숨김 |
| 엔진을 단독으로 실행하며 옵션 없음 | 이전 호환 유지: 환경변수, 없거나 빈 값이면 direct-tis |
| CLI 값 누락·잘못된 값·중복 지정 | 인수 오류. 다른 방식을 추측하지 않음 |

`native-shortcut`는 사용자에게 효과가 확인된 B + 후속 입력 순서 보존 경로다.
macOS에 설정된 단축키가 지원되지 않으면 기존 B의 거부·진단을 유지한다.
구름 설치를 요구하거나 macOS 단축키를 강제로 변경하지 않는다.
시작 로그의 `backend`와 `source`는 설정 선택의 근거이며 IME 입력 성공 판정이 아니다.

QSettings 저장 후 `sync()`와 `status()`를 확인하고 기본 원자적 동기화 요건을
유지한다. [Qt QSettings 문서](https://doc.qt.io/qt-6/qsettings.html).
저장 성공과 실행 중 적용 성공은 다른 상태다. 현재 제품은 다음 연결 시작 시 적용하며,
향후 QML에는 저장 값·실행 값·재시작 필요 상태를 따로 노출해야 한다.

새 GUI와 엔진은 **같은 빌드의 묶음**으로 배포해야 한다. 새 옵션을 모르는 오래된
엔진에 새 GUI만 붙이는 혼합 배포는 지원하지 않는다. 현재 설치본은 변경하지 않았다.

## 검증 증거

로컬 macOS의 별도 `build-product`에서 Qt 5.15.19, C++17로:

- GUI, 클라이언트, 서버와 세 unit-test 실행 파일 빌드 성공.
- 엔진 **237/237**, 저장·실행 계약 **6/6**, 기존 GUI **58/58** 통과.
- 기존 F19·단축키 해석·전환 순서 회귀 검사 포함.
- 저장 재개방, 양방향 선택, 기존 설정 보존, 잘못된 값, 쓰기 실패,
  Windows 실행 인수 제외, CLI 우선순위와 실제 OSXKeyState 전달 확인.
- 테스트는 임시 INI 파일과 대체 입력 경계를 사용한다. 실제 OS 입력 이벤트,
  설치 앱 재실행, Safari/Chrome 또는 두 컴퓨터 입력을 실행하지 않았다.

기존 SecureUtils 두 실패의 원인은 fixture 생성에 사용한
`uniform_int_distribution`의 표준 라이브러리별 매핑 차이였다.
지정된 `mt19937_64` 출력의 상위 바이트를 쓰도록 fixture만 바꾸어 **기존 정답
문자열을 수정하지 않고** 두 검사를 통과시켰다. 인증·암호화·실제 난수 코드는 변경하지 않았다.

재현 명령(저장소 루트, 로컬 개발 환경):

```sh
cmake -S . -B build-product -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5 \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
  -DQT_DEFAULT_MAJOR_VERSION=5 \
  -DINPUTLEAP_BUILD_GUI=ON -DINPUTLEAP_BUILD_TESTS=ON \
  -DINPUTLEAP_BUILD_AUDIO_DAEMON=OFF
cmake --build build-product --parallel 2 --target \
  input-leap input-leapc input-leaps unittests desktopunittests guiunittests
ctest --test-dir build-product --output-on-failure \
  -R '^(unittests|desktopunittests|guiunittests)$'
```

macOS 26.0은 이번 로컬 검증 바이너리의 빌드 대상이며 제품의 지원 하한을
정한 값이 아니다. 기존 API deprecation·중복 라이브러리 경고는 남아 있다.

`.github/workflows/keystitch-product-ci.yml`은 현재 개발 브랜치의 Mac/Windows
GUI·엔진 빌드와 전체 unit suite를 실행하도록 추가했다. 두 CI 플랫폼에서
Qt 6.10.3으로 공통 설정 계층을 검증하며, 로컬에서는 Qt 5도 확인했다.
Mac CI의 실행기는 macOS 26이다. 기존 macos-14 실행기는 지원 종료로 작업이
시작되지 않아 교체했다. CI 성공 여부와 실제 입력 수용은 별도다.
이 workflow는 설치 파일을 공개하거나 서비스를 설치하지 않는다.

전체 CI를 처음 연결하며 기존 Windows 재접속 코드의 `min` 매크로 충돌도
수정했다. 기존 GUI 저장 테스트는 임시 파일 경로에 NativeFormat을 사용해
Windows 레지스트리로 잘못 해석하던 부분을 IniFormat으로 바꾸고 I/O 상태를
검사한다. `--daemon` 검사는 Windows에서의 명시적 거부를 확인하도록 고쳤다.
검사를 제외하거나 제품의 저장 형식을 변경하지 않았다. Mac SDK에서 제거된
AGL을 참조하던 Qt 6.8.3 대신 AGL 의존성이 없는 Qt 6.10.3을 CI에 고정했다.

패키징 보완: [Mac 묶음 검사](../../tools/release/README.md)를 추가했다.
실제 바이너리/의존성보다 낮은 최소 OS를 선언한 패키지를 거부하고, 서명 후의
파일 해시를 외부 receipt에 기록한다. 로컬 개발 앱 묶음에서 50개 Mach-O 파일을
검사했으며 설치·실행하지 않았다. 이 검사는 소스 provenance나 출시 서명을
증명하지 않으므로 아래 1번 이정표가 전부 완료된 것은 아니다.

## 배포까지 남은 순서

프론트 시각 디자인은 뒤에 교체할 수 있도록 다음 기능부터 완성한다.

1. **일관된 빌드·패키지:** GUI/서버/클라이언트의 빌드 revision과 해시를 묶고,
   실제 Mach-O·의존성 최소 OS와 plist를 대조한다. Windows도 같은 revision의
   설치 파일을 만든다. CI 실행 결과와 artifact를 릴리스 후보에 연결한다.
2. **실행 상태 계층:** stopped/starting/connected/retrying/error, 실행 버전,
   저장/실행 설정 차이와 오류를 프론트와 독립적으로 노출한다. 프로세스 수명은
   한 소유자만 관리한다. 화면 교체 시 새로운 프로세스 제어를 중복 구현하지 않는다.
3. **설치·연결:** 최초 권한 안내, 인증서 신뢰/페어링, 암호화 기본값,
   자동 실행·업그레이드·복구를 두 OS에서 확인한다.
4. **입력 수용:** 실제 Windows 설치본을 식별하고 한영·modifier·마우스 추가 버튼,
   큰 클립보드·끊김·절전 복귀를 같은 시나리오로 검증한다.
5. **출시 조건:** 지원 OS/아키텍처 확정, 서명·notarization/Windows 서명,
   라이선스·소스 제공·업데이트 정책과 설치/제거 검증.

현재는 1차 개발 기반이며 M0~M4 전체 완료가 아니다. 실제 입력·설치 수용과
서명 계정이 필요한 단계는 그 환경을 확인한 뒤 진행한다.
