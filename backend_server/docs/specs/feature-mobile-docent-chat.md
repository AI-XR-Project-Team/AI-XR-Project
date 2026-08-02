# Feature Spec: 모바일 AI 도슨트 챗봇 (멀티턴 + SSE 스트리밍 + UE5 연동)

- **Branch**: `feature/mobile-docent-chat`
- **Status**: Implemented  <!-- Draft | Approved | Implemented | Merged -->
- **선행**: `feature/ai-docent-service`(PR #18), `feature/hybrid-ar-tracking`(PR #19)
- **아키텍처 근거**: `Documents/ADR-001-core-server-deferred.md`

## 1. 목표 / 배경

`feature/ai-docent-service` 로 서버의 AI 도슨트(`POST /docent/ask`)는 완성됐지만,
**UE5 클라이언트에 네트워킹 코드가 한 줄도 없어 아무도 그 API 를 부르지 못하는 상태**였다
(`Build.cs` 에 HTTP 모듈조차 없었다). 서버와 AR 파이프라인이 각자 완성돼 있고 둘을 잇는
다리만 없었다.

이 브랜치는 그 다리를 놓고, 그 위에 **모바일 화면에서 쓰는 챗봇**을 올린다.

또한 실제 타깃이 Meta Quest 3 에서 **Android 폰(GoogleARCore)** 으로 바뀐 것을 서버 문구에
반영한다. `uproject` 의 OculusXR 플러그인은 이미 비활성화돼 있었으나 서버 프롬프트가
"Quest 3 를 착용하고" 를 전제로 남아 있었다.

## 2. 범위

- 포함(In scope):
  - 대상 기기 문구 정정 (Quest 3 → 모바일 기기). **PERSONA 는 LLM 프롬프트에 직접 들어간다**
  - 대화 세션/메시지 스키마 (`chat_sessions`, `chat_messages`)
  - `LlmClient.stream()` — 멀티턴 + 토큰 스트리밍 추상화, Gemini 어댑터 구현
  - 엔드포인트 4종 (세션 생성 / SSE 대화 / 비스트리밍 대화 / 이력 조회)
  - UE5 `UDocentClient` — 단발 질의 + 대화 스트리밍
  - UE5 `FSseParser` — 바이트 단위 SSE 파서
  - Android 실기기 연결 설정 (INTERNET 권한, cleartext HTTP)
- 제외(Out of scope):
  - **UMG 챗 위젯** — 블루프린트 작업. §7
  - **POI 마커 스폰 + 터치 레이캐스트** — 좌표 원점 규약 확정 선행. §7
  - TTS 음성 합성, Redis 캐싱, 인증/rate limiting

## 3. 설계

### 3.1 데이터 흐름

```
UE5 (채팅창 열기)
  └─ POST /docent/sessions {device_uuid, exhibit_id?}  →  {session_id}

UE5 (질문 전송)
  └─ POST /docent/chat/stream {session_id, message, poi_id?}
       ├─ _prepare_turn()  : 세션·POI·전시물·이력 조회 + 사용자 질문 저장
       │                     (스트리밍 시작 전에 DB 작업을 전부 끝낸다)
       ├─ build_chat_prompt(): poi 유무로 프롬프트 분기
       ├─ stream_chat()    : LlmClient.stream() → ChatDelta* → ChatDone/ChatError
       └─ SSE 프레임으로 변환해 흘려보냄
            ↓
UE5 FSseParser (바이트 버퍼링) → HandleSseEvent (게임 스레드) → OnChatDelta/OnChatCompleted
```

### 3.2 SSE 프레임 규약

클라이언트 파서가 이 계약에 맞춰 구현돼 있다. 변경 시 양쪽을 함께 고쳐야 한다.

| event | data | 의미 |
|---|---|---|
| `meta` | `{session_id, poi_id}` | 생성 시작 |
| `delta` | `{text}` | 응답 조각. 이어 붙이면 전체 응답 |
| `done` | `{message_id, source, ttft_ms, total_ms}` | 정상 종료 |
| `error` | `{detail, partial, message_id}` | 실패. `partial=true` 면 앞선 `delta` 는 유효 |

**`data` 는 반드시 JSON 으로 감싼다.** 응답 본문의 개행이 그대로 나가면 프레임이 중간에
끊겨 파서가 깨진다. `test_stream_data_is_single_line_even_with_newlines` 가 이 경로를 지킨다.

### 3.3 폴백 규약의 재정의

기존 `POST /docent/ask` 는 "LLM 이 죽어도 500 대신 `docent_text`" 를 보장했다. 스트리밍은
첫 조각을 보낸 뒤에도 실패할 수 있어 그대로는 성립하지 않는다.

| 실패 시점 | 동작 |
|---|---|
| 첫 조각 **이전** | 폴백 문구를 정상 응답처럼 흘려보낸다 (`source="fallback"`). **기존 보장 유지** |
| 첫 조각 **이후** | 받은 만큼 남기고 `error{partial:true}` |

후자에서 폴백으로 덮어쓰지 않는 이유: 이미 나간 텍스트는 회수할 수 없어, 화면에 서로 다른
답변 두 개가 이어 붙는다.

### 3.4 POI 는 세션이 아니라 메시지에 붙는다

관람객은 부위를 탭했다 안 탭했다 하며 대화하므로 포커스가 턴마다 바뀐다. 따라서
`chat_messages.poi_id` 는 nullable 이고 턴마다 기록된다.

부위 미지정 시에는 POI 목록을 배경지식에 실어, "꼬리는 왜 길어요?" 처럼 말로만 지목한
질문에도 답할 수 있게 한다.

### 3.5 동시성

SSE 라우터는 `async def` 인데 SQLAlchemy 와 LLM 호출은 동기다. 그대로 부르면 이벤트 루프가
막힌다.

- DB 준비: `run_in_threadpool(_prepare_turn, ...)`
- LLM 스트림: `iterate_in_threadpool(...)`
- 스트리밍 시작 후에는 DB 를 만지지 않는다 (필요한 값을 `_prepare_turn` 에서 전부 확정)
- 사후 저장은 주입된 세션 대신 `SessionLocal()` 로 새 세션을 연다. 스트리밍 응답과 `yield`
  의존성의 정리 순서는 FastAPI 0.140 에서 "스트림 완료 후" 임을 확인했으나, 이 동작은 버전에
  따라 바뀐 이력이 있고 `requirements.txt` 가 버전을 고정하지 않는다.

### 3.6 UE5 수신 방식

`IHttpRequest::SetResponseBodyReceiveStreamDelegate` (UE 5.4) 를 쓴다. `OnRequestProgress64`
로 전체 본문을 폴링하는 방식보다 낫고, 실제 구현이 `FHttpRequestCommon` 에 있어 Windows·
Android 모두 동작한다.

엔진 주석이 명시하는 제약 두 가지를 코드가 전제한다:

1. **델리게이트가 게임 스레드가 아닌 곳에서 호출된다.** → 파싱까지만 수신 스레드에서 하고
   브로드캐스트는 `AsyncTask` 로 게임 스레드에 넘긴다.
2. **스트림을 설정하면 응답 본문이 캐시되지 않는다.** → 완료 콜백의 `GetContentAsString()`
   이 빈 값이다. 정상 종료는 `done`/`error` 프레임 수신 여부로 판별한다.

`FSseParser` 는 **바이트 단위로 버퍼링하고 완성된 프레임만 문자열로 바꾼다.** 응답이 한글
(UTF-8 멀티바이트)인데 수신 청크는 임의 지점에서 잘리므로, 도착하는 대로 문자열로 바꾸면
문자가 갈려 깨진다. 프레임 구분자 `\n\n` 은 ASCII 라 바이트 상태로 안전하게 찾을 수 있다.

### 3.7 Android 실기기 연결

| 항목 | 설정 |
|---|---|
| INTERNET 권한 | `+ExtraPermissions="android.permission.INTERNET"` |
| 평문 HTTP | `+ExtraApplicationNodeTags="android:usesCleartextTraffic=\"true\""` |
| 서버 주소 | `DefaultGame.ini` 의 `ServerBaseUrl` (재빌드 없이 변경) |

`MinSDKVersion=32` 라 API 28+ 의 평문 차단이 적용된다. UE 5.4 에는 전용 설정이 없어
`UEDeployAndroid.cs` 의 `ExtraApplicationNodeTags` 로 `<application>` 노드에 속성을 직접
주입한다. **배포 시에는 서버에 TLS 를 붙이고 이 줄을 제거할 것.**

> **`ServerBaseUrl` 값은 반드시 따옴표로 감싼다.** UE 설정 파서는 따옴표 밖의 `//` 를 주석
> 시작으로 처리하므로(`ConfigCacheIni.cpp:1252`), 따옴표가 없으면 `http://127.0.0.1:8000`
> 이 `http:` 로 잘린다. 실제로 이 문제로 실기기 연결이 실패했다.

## 4. 완료 조건 (Acceptance Criteria)

- [x] PERSONA 등 서버 문구가 모바일 기준으로 수정됨
- [x] 대화 세션/메시지 테이블 DDL + 기존 볼륨용 적용 스크립트
- [x] `LlmClient.stream()` 추가. 미구현 어댑터도 기본 구현으로 동작(벤더 추가 = 파일 1개 유지)
- [x] 세션 생성 / SSE 대화 / 비스트리밍 대화 / 이력 조회 4종 동작
- [x] 후속 질문에 이전 대화 맥락이 반영됨
- [x] `poi_id` 유무에 따라 프롬프트가 분기됨
- [x] 첫 조각 이전 실패 → 폴백, 이후 실패 → 부분 응답 유지
- [x] SSE `data` 가 개행 포함 응답에도 한 줄 JSON 으로 유지됨
- [x] UE5 에서 `GET /health` 왕복 성공 (에디터 PIE 실측)
- [x] `pytest` 45건 통과
- [ ] **UE5 C++ 컴파일** — 이 개발 환경에 UE 빌드 환경이 없어 미검증
- [ ] **실제 PostgreSQL 조회 경로** — Docker 부재로 미검증 (SQLite 로 대체 검증)
- [ ] **실기기 SSE 수신** — APK 빌드 후 확인 필요
- [ ] UMG 챗 위젯 / POI 터치 (§7)

### 검증 메모 (2026-08-02)

**pytest 45건 통과** (기존 28 + 신규 17). 챗 테스트는 SQLite 인메모리로 실제 테이블을 만들어
INSERT·정렬 조회까지 통과시킨다. 기존 `conftest.client` 의 스텁 세션은 `db.get()` 만
지원해 대화 저장을 검증할 수 없다.

> **테스트 UUID 에는 16진 문자(a-f)를 반드시 섞을 것.** SQLAlchemy 가 SQLite 에도 컬럼
> 타입명을 `UUID` 로 내보내는데, SQLite 는 모르는 타입명에 NUMERIC 친화성을 준다. 숫자로만
> 이뤄진 UUID(`2222...2222`)는 저장 시 숫자로 강제 변환돼 읽을 때 float 이 돌아오고
> `AttributeError: 'float' object has no attribute 'replace'` 로 터진다. 운영 PostgreSQL 은
> 네이티브 UUID 라 무관하다.

**SSE 파서 알고리즘 검증**: C++ 을 컴파일할 수 없어, 동일 알고리즘을 파이썬으로 옮겨 실제
서버 SSE 본문(1942바이트 / 32프레임 / delta 30개)으로 확인했다. 1·2·3·5·7·13·64·500바이트
청크로 나눠 먹여도 결과가 동일하고, 프레임 구분자 중간에서 끊어도 동일하다. 1바이트 청크는
한글 3바이트가 반드시 갈리는 조건이다.

**UE5 ↔ 서버 왕복**: 에디터 PIE 에서 `GET /health` → `200 {"status":"ok"}` 확인.

## 5. 테스트 방법

```powershell
# 서버
cd backend_server
python scripts/apply_chat_schema.py          # 기존 DB 볼륨이면 필요
uvicorn app.main:app --host 0.0.0.0 --port 8000
pytest

# UE5 에디터
# 1) uproject 우클릭 → Generate Visual Studio project files (모듈 3개 추가됨)
# 2) Development Editor / Win64 빌드
# 3) 레벨 BP: BeginPlay → Get Docent Client → Check Health
# 4) Output Log 에서 LogDocent 확인

# 실기기 (adb reverse — 방화벽/IP 신경 쓰지 않아도 된다)
$adb = "C:\Program Files (x86)\Android\android-sdk\platform-tools\adb.exe"
& $adb reverse tcp:8000 tcp:8000    # 폰의 127.0.0.1:8000 → PC 로 터널링
& $adb logcat -s UE | Select-String "LogDocent"
```

## 6. 리스크

- **실기기 SSE 수신 미검증** — 알고리즘과 API 선택은 근거를 확인했으나 APK 에서 실제로
  조각이 흘러오는지는 확인이 남았다. 실패 시 대비책은 이미 코드에 있다
  (`SetResponseBodyReceiveStreamDelegate` 실패 → `/docent/chat` 비스트리밍 전환).
- **실 PostgreSQL 미검증** — SQLite 로 전 경로를 돌렸으나 방언 차이가 남는다.
- **프롬프트 길이** — 부위 미지정 대화는 POI 목록이 모두 들어가 전시물이 커지면 프롬프트가
  길어진다. TTFT 가 나빠지면 상위 N 개만 싣도록 조정한다.
- **동시 접속** — 스트리밍은 요청당 워커 스레드를 오래 점유한다. 관람객이 늘면 async
  SQLAlchemy + `client.aio` 이관 검토.

## 7. 남은 작업 (후속 브랜치)

### 7.1 UMG 챗 위젯
- `WBP_DocentChat` — ScrollBox + EditableTextBox + 전송 버튼 + POI 컨텍스트 칩
- `WBP_ChatBubble` — user/assistant 2스타일
- `OnChatDelta` 마다 마지막 말풍선에 텍스트 append + 자동 하단 스크롤
- 모바일: `SafeZone` 래핑, 가상 키보드가 입력창을 가리지 않게 처리,
  채팅창이 열려 있는 동안 AR 터치 레이캐스트 비활성화

### 7.2 POI 마커 + 터치
- **선행: 좌표 원점 규약 확정.** `pois.pos_*_cm` 은 "UE5 Z-up, cm" 까지만 정의돼 있고
  **무엇을 원점으로 하는지가 스키마에 없다.** 마커 이미지 앵커 기준 상대좌표로 확정하고
  시드 데이터를 그에 맞게 검수해야 한다. 이게 틀어지면 마커가 공룡에서 벗어난 곳에 뜬다.
- `APoiMarkerActor` — 빌보드 + 콜리전, `PoiId` 보유
- `AARTrackingManager` 가 앵커 확정 후 `GET /exhibits/{id}` 로 POI 를 받아 스폰
- 터치 → `GetHitResultUnderFinger` → 히트한 `PoiId` 를 채팅창에 전달
