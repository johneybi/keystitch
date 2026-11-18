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

**원격 검증 완료:** 코드 커밋 `166c4353c8ad8437e507e788a93ca85e136fb804`의
[CI 실행 44240488619](https://github.com/johneybi/keystitch/actions/runs/44240488619)는
Windows와 macOS 모두 성공했다. 업로드된 `LastTest.log`에서 Mac은
237 core + 6 desktop + 58 GUI = **301개**, Windows는
172 core + 6 desktop + 58 GUI = **236개** 통과를 확인했다.
패키지 parser/정책 테스트 **5개도 각 OS에서 통과**했다.
OS별 platform suite가 달라 개수에 차이가 있다. 설치·실제 입력 수용의 증거는 아니다.

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

## 동일 소스의 내부 배포 후보 생성 — 2026-11-18

`f709f47c`는 실행 초기화 이전의 `--build-info`와 빌드 묶음 검사를 추가했다.
`ac6a3ace`는 Mac 앱 ZIP과 Windows Inno EXE 생성·업로드를 CI에 연결했다.
`ade651a1`은 Qt 설치 도구가 남기는 `aqtinstall.log`를 소스 변경 판정에서
제외하고 실제 임시 Git 저장소로 metadata 갱신을 검증한다. 다른 untracked
소스나 수정 파일은 계속 dirty이며, dirty 후보는 기본 거부한다.

**코드 revision `ade651a12adf6d48b7d8d7399a76443f94ad88ce`의
[CI 44243335394](https://github.com/johneybi/keystitch/actions/runs/44243335394)가
두 OS 모두 성공했다.**

| 산출물/검증 | 확인 결과 |
| --- | --- |
| Mac unit suites | 240 core + 6 desktop + 58 GUI = 304개 통과 |
| Windows unit suites | 175 core + 6 desktop + 58 GUI = 239개 통과 |
| 패키지·metadata 검사 | 각 OS에서 5 audit + 6 verifier + 4 Git fixture = 15개 통과 |
| Mac 후보 | GUI/client/server 같은 build ID, clean revision, 19개 Mach-O 검사, ad-hoc 서명 검증, ZIP 업로드 |
| Windows 후보 | GUI/client/server/daemon 같은 build ID, clean revision, 개발 Qt PATH 없이 probe, Inno 컴파일·EXE 업로드 |

- [Mac 내부 후보 ZIP와 receipt](https://github.com/johneybi/keystitch/actions/runs/44243335394/artifacts/14400169199)
- [Windows 내부 후보 EXE와 receipt](https://github.com/johneybi/keystitch/actions/runs/44243335394/artifacts/14400184706)

두 artifact를 `build-product/candidates-ade651a1/`로 내려받아 ZIP/EXE SHA256을
receipt와 대조했다. Mac ZIP 내부의 Info.plist·19개 Mach-O 해시도 일치했다.
다운로드한 실행 파일이나 설치 프로그램은 실행하지 않았다.

각 OS 안의 실행 파일들은 같은 build ID여야 하며 두 OS 사이에는 같은 소스
revision을 사용한다. CI artifacts는 보존 기간이 있는 개발 산출물이고 정식
Release가 아니다. 로컬 Qt 5에서도 별도 clean 후보의 50개 Mach-O와 서명을
검사했다. `--build-info` 조회 외에 GUI/입력 엔진 일반 실행·설치는 하지 않았다.

남은 제한: Mac 후보는 macOS 26 빌드이며 Developer ID/notarization 없음,
Windows 설치 파일은 미서명·설치 미검증이다. Inno의 기존 관리자 설치와 HKCU
처리 경고, 방화벽 정책, 자동 실행 이전·업그레이드는 설치 수용 단계에서 정리한다.
전체 라이선스 묶음과 깨끗한 VM의 의존성/GUI 초기화 검증도 남아 있다.
현재 사용자 앱·OS 한영 설정을 변경하지 않았다.

## 배포까지 남은 순서

프론트 시각 디자인은 뒤에 교체할 수 있도록 다음 기능부터 완성한다.

1. **일관된 빌드·패키지:** 위 CI에서 내부 후보 생성까지 확인했다. 다음에는
   깨끗한 설치 환경의 실행·의존성 검증과 제3자 라이선스 묶음을 추가한다.
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

### 다음 구현: 화면과 실행 제어 분리

현재 `MainWindow`가 QProcess, 재시도 타이머, 시작/중지, 로그 기반 연결 표시를
모두 소유한다. `started server`도 CONNECTED로 처리하고, 프로세스 종료 후
재시도를 예약할 때 연결 표시를 즉시 갱신하지 않는다. 이 상태를 그대로 QML로
복제하면 화면마다 다른 실행·재시도 동작이 생긴다. 다음 구현의 경계는 아래다.
이 절은 **설계이며 아직 추출을 완료했다는 뜻이 아니다.**

- Qt Core 실행 controller 한 개가 desktop 프로세스와 재시도 타이머를 소유한다.
  Widgets/QML/메뉴바·트레이는 start/stop/restart 명령과 상태 snapshot을 공유한다.
  Windows service는 기존 IPC를 별도 transport로 연결하며 desktop process를
  동시에 시작하지 않는다. 화면의 소멸을 엔진 소유권 변경과 동일시하지 않는다.
- 프로세스 상태(`stopped/starting/running/stopping/retrying/error`)와 연결 상태
  (`unknown/listening/connecting/connected/disconnected`)를 분리한다.
  서버가 시작했다는 로그만으로 상대가 연결되었다고 표시하지 않는다.
- 각 실행에 generation을 부여하고 이전 프로세스의 늦은 출력·종료·timer를
  무시한다. stop은 예약된 재시도를 취소한다. 실행 실패는 오류로 남기며
  무제한 실행 루프에 넣지 않는다. 비정상 종료 재시도는 단일 timer로 제한한다.
- 실행에 사용한 불변 설정 snapshot과 저장된 설정 revision을 따로 노출한다.
  저장 성공, 실행 반영, 상대 연결을 구분하고 변경은 명시적 restart 때 적용한다.
  Mac 입력 소스 설정을 쓰거나 구름 입력기를 요구하는 동작은 넣지 않는다.
- 현재 텍스트 로그 파싱은 경계 adapter로 격리하고 줄이 여러 read에 나뉘는
  경우를 처리한다. 인증서 승인과 연결 상태를 섞지 않는다. 구조화된 engine
  status를 도입하기 전까지 로그로 확인할 수 없는 상태는 unknown으로 둔다.

검증은 대체 process/timer와 실제 무해한 fixture child를 사용한다. 중복 start,
stop 중 exit, 실패한 launch, 재시도 중 설정 변경, 옛 generation 이벤트,
분할 로그를 검증하며 실제 입력 공유 엔진을 시험 삼아 시작하지 않는다.
첫 적용은 desktop transport부터 하고 Windows service 경로는 기존 동작과
분리해 검증한 뒤 통합한다. 시각 디자인 변경은 이 계약 위에서 진행한다.

### Desktop 실행 제어 구현

애플리케이션이 Qt Core의 `DesktopEngine`을 생성하고 Widgets 메인 창에 전달한다.
컨트롤러가 자식 프로세스와 단일 재시도 타이머를 소유한다. 실행 중이거나
재시도를 기다릴 때 추가 start 요청을 무시하고, 재시도에는 처음 받은 실행 파일과
인수를 사용한다. 외부 설정 파일을 지정한 경우 같은 파일 경로를 사용하므로
외부에서 파일 내용을 바꾼 경우까지 고정되지는 않는다. 실행 실패는 Failed 상태와
오류 메시지로 전달하고 사용자가 다시 시작할 수 있다.

Widgets가 stop 또는 reload를 요청하면 먼저 재시도를 취소하고 자식을 종료한다.
내장 서버 설정은 실행 때 임시 파일을 한 번 만들고 재시도 동안 유지한다.
Windows 서비스의 IPC 시작·중지 경로는 기존 구현을 사용한다.

`desktopunittests`는 별도의 시험 프로세스에 분할 로그 출력과 종료를 요청한다.
중복 start, 두 번의 재시도 간격과 최초 인수 유지, 대기 중 stop, 실패한 실행 후
재시작을 확인한다. 프로세스 상태는 Qt 신호로 노출했다. 상대 연결 상태는 아래 엔진 보고로 제공한다.
저장된 설정과 실행 중 설정의 차이를 표시하는 기능은 후속 작업이다.

### 데스크톱 엔진의 상대 연결 상태

`DesktopEngine::state()`는 프로세스 상태를, `peerStatus()`와
`peerStatusChanged`는 상대 연결 상태를 제공한다. 서버는 리스너 준비,
첫 원격 클라이언트 등록, 마지막 원격 클라이언트 제거 시점을 보고한다.
공유에 참여하는 로컬 화면도 서버 목록에 들어 있으므로 첫 원격 연결에서
목록 크기는 2가 된다. 클라이언트는 연결 시도, 핸드셰이크 완료, 실패와
연결 종료를 보고한다. 프로세스가 끝나면 상대 상태는 Unknown으로 돌아간다.

데스크톱 컨트롤러가 자식을 시작할 때마다 새 식별자를 자식 환경에 넣는다.
엔진은 그 식별자를 붙인 한 줄짜리 상태 레코드를 표준 출력으로 보낸다.
수신 측은 현재 자식의 완성된 레코드만 사용하고 반복 상태를 합친다.
이 값은 화면 표시용이다. 페어링과 인증서 신뢰 결정은 기존 엔진 경로에서 처리한다.
서비스 모드의 IPC 로그에는 이 데스크톱 상태 채널이 적용되지 않는다.

시험용 자식으로 실행 식별자, 일반 로그, 오류 출력, 마지막 줄 잘림, 중복 보고,
종료 후 상태 초기화를 검증한다. 두 컴퓨터 사이의 실제 핸드셰이크와 장치 입력은
설치 수용 단계에서 확인해야 한다.
