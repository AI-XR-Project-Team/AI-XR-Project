# 실행 가이드 — 모바일 AI 도슨트 챗봇

처음 받아서 돌려보는 사람을 위한 순서다. 위에서부터 그대로 따라가면 된다.

설계와 근거는 `backend_server/docs/specs/feature-mobile-docent-chat.md` 에 있다.
여기는 "어떻게 띄우는가" 만 다룬다.

---

## ⚠️ 먼저 알아둘 것 — 전시물 UUID 는 각자 다르다

`scripts/seed.py` 가 UUID 를 고정하지 않아 **머신마다 전시물 ID 가 다르다.**
`WBP_DocentChat` 클래스 디폴트에 박혀 있는 `bf32d486-...` 은 만든 사람 PC 의 값이라,
그대로 두면 다른 PC 에서는 대화가 되지 않는다. 아래 4단계에서 자기 값으로 바꾼다.

(AR 마커 인식 시 동적으로 넘기는 것은 후속 작업이다.)

---

## 1. 서버 환경 준비

```bash
cd backend_server
cp .env.example .env
```

`.env` 에서 LLM 을 고른다.

| 목적 | 설정 |
|---|---|
| API 키 없이 UI·스트리밍만 확인 | `LLM_PROVIDER=mock` |
| 실제 응답을 받고 싶다 | `LLM_PROVIDER=gemini` + `LLM_API_KEY=<발급받은 키>` |

Gemini 키는 https://aistudio.google.com/apikey 에서 받는다. 무료 티어가 있고 카드 등록은 필요 없다.

> `.env` 는 `.gitignore` 대상이다. **API 키를 `.env.example` 이나 커밋 대상 파일에 복사하지 말 것.**

가상환경과 의존성:

```bash
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

---

## 2. DB 기동

```bash
docker compose up -d
docker compose ps        # STATUS 가 healthy 가 될 때까지 10~20초 걸린다
```

테이블 7개가 생겼는지 확인한다.

```bash
docker exec dino_ar_db psql -U dino -d dino_ar -c "\dt"
```

```
users, dinosaurs, exhibits, pois, view_logs, chat_sessions, chat_messages
```

`chat_sessions` 가 없다면 **이 브랜치 이전에 만든 DB 볼륨이 이미 있는 경우**다.
`db/init/*.sql` 은 볼륨이 처음 생길 때만 실행되기 때문이다. 그때만 이걸 돌린다.

```bash
.venv\Scripts\python.exe scripts\apply_chat_schema.py
```

시드 투입 (멱등이라 여러 번 돌려도 안전하다):

```bash
.venv\Scripts\python.exe scripts\seed.py
```

---

## 3. 서버 기동

붙이는 방식에 따라 host 가 다르다.

```bash
# 실기기를 USB(adb reverse)로 붙일 때 — 권장
.venv\Scripts\python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 8000

# 같은 공유기의 다른 기기에서 LAN 으로 붙을 때
.venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

동작 확인:

```bash
curl http://127.0.0.1:8000/health       # {"status":"ok"}
curl http://127.0.0.1:8000/dinosaurs    # 여기서 500 이면 DB 가 안 떠 있는 것
```

`/health` 는 DB 를 건드리지 않는다. **`/health` 만 보고 정상이라고 판단하면 안 된다.**
`/dinosaurs` 까지 확인해야 DB 경로가 살아 있는 것이다.

### SSE 스트리밍 확인

Swagger(`/docs`)로는 스트리밍인지 알 수 없다. 응답을 전부 모아서 한 번에 보여주기 때문이다.
`curl -N` (버퍼링 끄기)을 쓴다.

```bash
# 1) 세션 만들기
curl -X POST http://127.0.0.1:8000/docent/sessions ^
  -H "Content-Type: application/json" -d "{}"

# 2) 위에서 받은 session_id 로 스트리밍
curl -N -X POST http://127.0.0.1:8000/docent/chat/stream ^
  -H "Content-Type: application/json" ^
  -d "{\"session_id\":\"<session_id>\",\"message\":\"설명해줘\"}"
```

`event: delta` 프레임이 여러 번 나뉘어 오면 정상이다.

### 테스트

```bash
.venv\Scripts\python.exe -m pytest -q     # 45 passed
```

---

## 4. UE5 에디터에서 확인

1. `TimeMachineAR.uproject` 우클릭 → **Generate Visual Studio project files**
   (C++ 클래스가 추가됐으므로 필요하다)
2. **Development Editor / Win64** 로 빌드한 뒤 에디터를 연다
3. 자기 PC 의 전시물 UUID 를 확인한다

   ```bash
   docker exec dino_ar_db psql -U dino -d dino_ar -c "SELECT id, label FROM exhibits;"
   ```

4. `Content/UI/Docent/WBP_DocentChat` 을 열고
   → 상단 **클래스 디폴트** → **Exhibit Id** 를 3번에서 확인한 값으로 교체
5. `AR_MainMap` 에서 **플레이**

### 기대 동작

| 순서 | 화면 |
|---|---|
| 시작 | 오른쪽 아래에 렉시 버튼만 |
| 렉시 버튼 클릭 | 하단에 채팅창 + 인사말 + 빠른 질문 칩 4개 |
| 칩 클릭 | 사용자 말풍선 + 글자가 흘러나오는 도슨트 말풍선 |
| X 클릭 → 다시 열기 | 이전 대화가 그대로 (세션 재사용) |

---

## 5. 실기기 테스트

### 5-1. Android 툴체인

UE 5.4 의 `SetupAndroid.bat` 은 Android Studio 설치를 전제한다.
없다면 아래 버전을 직접 맞춘다. **버전이 다르면 빌드가 실패한다.**

| 항목 | 버전 |
|---|---|
| NDK | `25.1.8937393` (r25b) |
| platform | `android-33` |
| build-tools | `33.0.1` |
| cmake | `3.22.1` |
| JDK | **17 이상** |

JDK 는 UBT 가 `$JAVA_HOME/release` 의 `JAVA_VERSION` 을 읽어 17 미만이면 거부한다
(`AndroidPlatformSDK.cs`). JDK 8 로는 안 된다.

환경변수로 찾는다.

```
ANDROID_HOME = <SDK 경로>
NDKROOT      = <SDK 경로>\ndk\25.1.8937393
JAVA_HOME    = <JDK 17 경로>
```

> 관리자 권한이 없어 `C:\Program Files (x86)\Android\android-sdk` 를 못 건드린다면,
> `%LOCALAPPDATA%\Android\Sdk` 에 따로 구성해도 된다. `cmdline-tools` 는 폴더 이름이
> `latest` 여야 `sdkmanager` 가 인식한다.

### 5-2. USB 터널

폰의 `127.0.0.1:8000` 을 PC 로 연결한다. 방화벽·IP·공유기를 신경 쓰지 않아도 된다.

```bash
adb reverse tcp:8000 tcp:8000
```

> **USB 를 뽑거나 폰을 재부팅하면 사라진다.** 매번 다시 걸어야 한다.

이 방식이면 `DefaultGame.ini` 의 `ServerBaseUrl` 을 `http://127.0.0.1:8000` 그대로 두면 된다.

### 5-3. 패키징

도슨트 UI 는 `AR_MainMap` 에 있다. **`-cmdline` 으로 부팅 맵을 넘겨야 한다.**

```bash
RunUAT.bat BuildCookRun -project=<경로>\TimeMachineAR.uproject -noP4 ^
  -platform=Android -cookflavor=ASTC -clientconfig=Development ^
  -map="/Game/Stuff/Maps/AR_MainMap" -cmdline="/Game/Stuff/Maps/AR_MainMap" ^
  -build -cook -stage -pak -package -archive -archivedirectory=<출력경로>
```

> 에디터를 켠 채로 돌리려면 `-nocompileeditor` 를 붙인다.
> 그리고 **에디터에서 저장한 뒤에 패키징해야 한다.** 저장하지 않은 변경은 쿡되지 않는다.

### 5-4. 설치

```bash
adb install -r <출력경로>\Android_ASTC\TimeMachineAR-arm64.apk
```

`INSTALL_FAILED_VERIFICATION_FAILURE` 가 나면 ADB 설치 검증을 끈다.
개발자 옵션의 "USB 를 통해 설치한 앱 확인" 과 같은 값이고, adb 설치에만 적용된다.

```bash
adb shell settings put global verifier_verify_adb_installs 0    # 되돌리기: ... 1
```

### 5-5. 로그

```bash
adb logcat -c
adb shell monkey -p com.YourCompany.TimeMachineAR -c android.intent.category.LAUNCHER 1
adb logcat -s UE | findstr LogDocent
```

정상이면 이런 로그가 나온다.

```
LogDocent: 도슨트 클라이언트 초기화. 서버=http://127.0.0.1:8000 device=android-...
LogDocent: [chat] 세션 준비됨: 7da00af4-...
LogDocent: [chat] POST http://127.0.0.1:8000/docent/chat/stream poi=(전체) q=...
LogDocent: [chat] 완료 source=llm ttft=706ms total=1339ms len=194
```

### 5-6. UI 없이 대화만 찔러보기

콘솔 명령이 등록돼 있다. Shipping 이 아닌 빌드에서만 열린다.

```bash
adb shell "am broadcast -a android.intent.action.RUN -e cmd 'Log LogDocent Verbose'"
adb shell "am broadcast -a android.intent.action.RUN -e cmd 'Docent.Health'"
adb shell "am broadcast -a android.intent.action.RUN -e cmd 'Docent.StartSession <ExhibitId>'"
adb shell "am broadcast -a android.intent.action.RUN -e cmd 'Docent.Ask 이 공룡 뭐 먹었어?'"
```

`Log LogDocent Verbose` 를 켜면 조각이 도착할 때마다 로그가 남는다.
조각이 실제로 나뉘어 오는지는 이 줄들의 **타임스탬프와 프레임 번호**로만 판별할 수 있다.

---

## 문제가 생겼을 때

### 서버

| 증상 | 원인 |
|---|---|
| `/health` 는 되는데 `/dinosaurs` 가 500 | DB 컨테이너가 안 떠 있다. `docker compose up -d` |
| `psycopg2.OperationalError ... port 5432 ... Connection refused` | 같은 원인 |
| `relation "chat_sessions" does not exist` | 기존 볼륨. `scripts\apply_chat_schema.py` |
| 응답이 항상 똑같은 더미 문장 | `.env` 의 `LLM_PROVIDER` 가 `mock` 이다 |

### UE 에디터

| 로그 / 증상 | 원인 |
|---|---|
| `ServerBaseUrl 이 유효하지 않습니다(값="http:")` | `DefaultGame.ini` 의 값이 따옴표로 감싸져 있는지 확인. UE 설정 파서는 따옴표 밖의 `//` 를 주석으로 처리해 `http://...` 를 `http:` 로 자른다 |
| `Could not resolve host: health` | 위와 같은 원인 |
| `[chat] 세션 생성 실패` | 서버 미기동 또는 주소 오류 |
| `BubbleClass 또는 ChatScroll 이 없습니다` | WBP 클래스 디폴트에 `Bubble Class` / `Chip Class` 미지정 |
| 인사말은 뜨는데 답이 없다 | `Exhibit Id` 가 자기 DB 값과 다르다 (4단계 참고) |
| 말풍선이 안 보인다 | `ChatScroll` 슬롯 사이즈가 **채우기** 인지 확인. `자동` 이면 높이가 0 이 된다 |
| C++ 클래스가 목록에 안 뜬다 | 에디터를 닫고 에디터 타깃을 다시 빌드해야 한다. 새 `UCLASS`·`UPROPERTY` 는 Live Coding 으로 반영되지 않는다 |

### 실기기

| 증상 | 원인 |
|---|---|
| `adb devices` 에 아무것도 없다 | 충전 전용 케이블이거나 USB 모드가 "충전만" 이다. 폰 알림창에서 파일 전송으로 바꾼다 |
| `unauthorized` | 폰의 USB 디버깅 허용 팝업을 놓쳤다. `adb kill-server` 후 재시도 |
| 앱은 뜨는데 서버에 못 붙는다 | `adb reverse` 가 풀렸다. 다시 건다 |
| AR 카메라가 안 보인다 | 채팅 패널이 화면을 덮고 있다. `WBP_DocentChat` 의 스페이서 슬롯이 **채우기** 인지 확인 |
| 최신 수정이 반영되지 않았다 | 에디터에서 저장한 시각보다 쿡이 먼저였다. 저장 후 다시 패키징 |

---

## 배포 전 되돌릴 것

`TimeMachineAR/Config/DefaultEngine.ini` 의 이 줄은 개발 편의용이다.

```ini
+ExtraApplicationNodeTags="android:usesCleartextTraffic='true'"
```

Android 9(API 28)+ 는 평문 HTTP 를 기본 차단하는데, 개발 중 PC 의 FastAPI 에 붙기 위해 열어 둔 것이다.
**배포 시에는 서버에 TLS 를 붙이고 이 줄을 제거해야 한다.**
