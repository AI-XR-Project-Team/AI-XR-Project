# Feature Spec: AI 도슨트 서비스 (LLM Provider 추상화 + POI 질의 API)

- **Branch**: `feature/ai-docent-service`
- **Status**: Implemented  <!-- Draft | Approved | Implemented | Merged -->
- **Author**: 현우
- **관련 이슈/태스크**: Roadmap W4 "FastAPI 백엔드 및 Gemini API 연동" (통합 문서 §5)
- **선행**: `feature/db-postgres-setup`, `feature/fastapi-skeleton`, `feature/seed-data`
- **아키텍처 근거**: `Documents/ADR-001-core-server-deferred.md` (C++ 코어서버 보류 결정)

## 1. 목표 / 배경

통합 문서의 원안 아키텍처는 `UE5 → C++ IOCP 코어서버(PKT_AI_TRIGGER 0x06) → FastAPI` 3계층이었으나,
**ADR-001에 따라 코어서버를 보류하고 FastAPI를 단일 서버로 운영하기로 확정**했다.
AI 도슨트는 30Hz 실시간 동기화와 무관한 **요청-응답형** 기능이라 이 구성에서 손실이 없다.

ADR-001 §3.3 기준, 9개 패킷 타입 중 HTTP로 대체되는 것은 `PKT_AI_TRIGGER(0x06)` 하나뿐이며
**이 브랜치가 바로 그 대체 구현**이다. Gaze 2초 임계 판정(`GazeDuration >= 2.0f`)은
서버가 아닌 UE5 클라이언트가 수행하고, 임계를 넘겼을 때 이 엔드포인트를 호출한다.

이 브랜치는 그 첫 단계로, 이미 시드된 DB 데이터(`dinosaurs.ai_prompt_context`, `pois.docent_text`)를
컨텍스트로 삼아 LLM 응답을 돌려주는 `POST /docent/ask` 를 만든다.

LLM 벤더는 **아직 확정되지 않았다**. 따라서 이 브랜치에서는 특정 SDK에 코드를 묶지 않고
**Provider 추상화 계층 + Mock 구현**까지만 만들어, 벤더가 확정되면 어댑터 파일 1개 추가와
환경변수 1줄 변경만으로 전환되도록 한다.

## 2. 범위

- 포함(In scope):
  - LLM Provider 추상 인터페이스(`LlmClient`) + 팩토리(`get_llm_client()`)
  - `GeminiLlmClient` — Google Gemini API 어댑터 (실사용 구현, 무료 티어)
  - `MockLlmClient` — API 키 없이 동작하는 개발/테스트용 구현
  - 프롬프트 조립 서비스 — DB(dinosaur/exhibit/poi)에서 컨텍스트를 모아 system prompt 생성
  - 엔드포인트 `POST /docent/ask` (Swagger 문서화 포함: tags/summary/examples/responses)
  - 실패·타임아웃 시 `pois.docent_text` 로 폴백
  - pytest 스모크 테스트 (이 레포 최초 테스트 도입)
- 제외(Out of scope):
  - Claude/OpenAI 어댑터 — 필요 시 `app/services/llm/` 에 파일 1개 추가
  - Cloud TTS 연동 — 별도 브랜치
  - Redis 캐싱 — 별도 브랜치 (통합 문서 R3). 본 브랜치는 `docent_text` 폴백이 대체
  - Q&A 로그 적재용 `ai_docent_logs` 테이블 — 별도 브랜치 (§7)
  - C++ 코어서버 경유 경로 — ADR-001에 따라 **보류**. 부활 시에도 본 엔드포인트는 그대로 재사용
  - 인증/인가, rate limiting

## 3. 설계

### 파일 / 모듈 구조
```
backend_server/
  app/
    core/
      config.py          # (수정) LLM_PROVIDER, LLM_API_KEY, LLM_MODEL, LLM_TIMEOUT_SEC 추가
    services/
      __init__.py
      docent.py          # build_docent_prompt(), ask_docent() — DB 컨텍스트 조립 + 폴백
      llm/
        __init__.py
        base.py          # class LlmClient(ABC), class LlmError(Exception)
        mock.py          # class MockLlmClient(LlmClient)
        factory.py       # get_llm_client() — LLM_PROVIDER 값으로 분기
    schemas/
      docent.py          # DocentAskRequest, DocentAskResponse
    routers/
      docent.py          # POST /docent/ask
    main.py              # (수정) docent 라우터 등록
  tests/
    __init__.py
    conftest.py          # TestClient fixture
    test_docent.py       # 프롬프트 조립 / 폴백 / 엔드포인트 스모크
  requirements.txt       # (수정) pytest, httpx 추가
  .env.example           # (수정) LLM_* 변수 추가
```

### 인터페이스

**LLM 추상 계층** (`app/services/llm/base.py`)
```python
class LlmClient(ABC):
    @abstractmethod
    def generate(self, system_prompt: str, user_prompt: str) -> str:
        """실패 시 LlmError 를 raise 한다."""
```
- 벤더별 구현은 이 메서드만 채우면 된다. 스트리밍·툴콜 등은 지금 인터페이스에 넣지 않는다(YAGNI).
- `get_llm_client()` 는 `settings.LLM_PROVIDER` 를 보고 인스턴스를 반환. 미지원 값이면 기동 시점에 명시적 에러.

**API 엔드포인트**
- `POST /docent/ask` → `200 DocentAskResponse` / poi 없으면 `404`

Request (`DocentAskRequest`):
| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| `poi_id` | UUID | O | 관람객이 응시(Gaze)한 POI |
| `question` | str \| None | X | 자유 질문. 없으면 해당 부위 기본 해설 생성 |
| `device_uuid` | str \| None | X | Quest 3 익명 기기 식별자(향후 로깅용, 현재는 미사용) |

Response (`DocentAskResponse`):
| 필드 | 타입 | 설명 |
|------|------|------|
| `poi_id` | UUID | 요청한 POI |
| `exhibit_id` | UUID | 소속 전시물 |
| `answer` | str | 도슨트 응답 본문 |
| `source` | `"llm"` \| `"fallback"` | LLM 응답인지 DB 폴백인지 |
| `elapsed_ms` | int | 서버 처리 시간(KPI ≤2s 계측용) |

- 외부 의존성: 신규 런타임 의존성 없음(Mock 단계). 개발 의존성으로 `pytest`, `httpx` 추가.

**환경변수**

| 변수 | 기본값 | 설명 |
|---|---|---|
| `LLM_PROVIDER` | `mock` | `gemini` \| `mock` \| `failing` |
| `LLM_API_KEY` | (빈값) | **`.env` 에만 둔다.** `.env.example`·코드에 실제 키 금지 |
| `LLM_MODEL` | `gemini-2.5-flash` | 무료 티어는 flash 계열 한도가 넉넉 |
| `LLM_MAX_TOKENS` | `2048` | 응답 텍스트 상한 |
| `LLM_THINKING_BUDGET` | `0` | Gemini 2.5 사고 토큰. `0`=끔 / `-1`=자동 |
| `LLM_TIMEOUT_SEC` | `4.0` | 초과 시 `docent_text` 폴백 |

> `.env` 는 `backend_server/.gitignore:4` 로 커밋에서 제외된다.
> `.env.example` 은 커밋되므로 `LLM_API_KEY` 를 **반드시 빈 값으로 유지**한다.

### 데이터 흐름
```
UE5 (Gaze로 POI 확정)
  └─ HTTP POST /docent/ask {poi_id, question?}
       └─ routers/docent.py
            ├─ DB 조회: Poi → Exhibit → Dinosaur  (poi 없으면 404)
            ├─ services/docent.build_docent_prompt()
            │     system = 도슨트 페르소나
            │            + dinosaur.ai_prompt_context   (종 배경지식)
            │            + exhibit.label                (전시물 맥락)
            │            + poi.part_name / docent_text  (부위 해설)
            │     user   = question ?? "이 부위를 관람객에게 설명해줘"
            ├─ get_llm_client().generate(system, user)
            │     ├─ 성공        → source="llm"
            │     └─ 실패/타임아웃 → poi.docent_text 반환, source="fallback"
            └─ DocentAskResponse (JSON)
```

**폴백 근거**: 통합 문서 리스크 R3(LLM 응답 지연)과 KPI(도슨트 응답 ≤2s)에 대응.
`pois.docent_text` 는 이미 시드된 사전 작성 해설이므로, LLM이 죽어도 관람 경험이 끊기지 않는다.
`docent_text` 마저 NULL이면 `part_name` 기반 최소 문장을 생성한다.

### 동기/비동기 결정
엔드포인트는 기존 라우터와 동일하게 **sync `def`** 로 작성한다. FastAPI가 이를 threadpool에서
실행하므로 동기 SQLAlchemy 세션·동기 LLM 호출이 이벤트 루프를 막지 않는다.
동시 접속이 커지면(50 CCU 목표) async SQLAlchemy + httpx.AsyncClient 로 이관한다(§7).

### 코드 스타일 (통합 문서 §1.6 Python Backend 명명 규칙)
- 클래스: `PascalCase` (`class MockLlmClient(LlmClient):`)
- 함수/변수: `snake_case` (`def build_docent_prompt():`)
- 상수: `UPPER_SNAKE_CASE` (`LLM_TIMEOUT_SEC`)
- private 멤버: `_` 접두사 (`self._api_key`)

## 4. 완료 조건 (Acceptance Criteria)

- [x] `LLM_PROVIDER=mock` 상태에서 **API 키 없이** 서버가 기동되고 `/docent/ask` 가 200을 반환한다
- [x] `POST /docent/ask` 에 POI id를 주면 티라노 배경지식이 반영된 응답이 온다
- [x] `question` 을 생략하면 해당 부위 기본 해설이, 주면 질문에 대한 답이 반환된다
- [x] 존재하지 않는 `poi_id` 는 `404 {"detail": "poi not found"}` 를 반환한다
- [x] LLM이 예외/타임아웃일 때 `source="fallback"` 과 함께 `poi.docent_text` 가 반환된다 (500 아님)
- [x] Swagger UI(`/docs`)에 `docent` 태그로 그룹핑되어 노출된다
- [x] `openapi.json` 에 요청/응답 스키마와 404 응답이 문서화된다
- [x] `pytest` 스모크 테스트가 통과한다
- [x] 벤더 확정 시 **`app/services/llm/<vendor>.py` 추가 + factory 분기 1줄 + `.env` 1줄** 외에
      다른 파일 수정이 필요 없다 (추상화 검증)

### 검증 메모 (2026-07-26)

**pytest 22개 전부 통과** (`backend_server/` 에서 `pytest`). 프롬프트 조립 8건, 폴백 5건,
LLM 팩토리 5건, 엔드포인트 6건. PostgreSQL 없이 돌도록 `get_db` 를 스텁 세션으로 override 했다
(`tests/conftest.py`) — `load_poi_context` 가 `db.get()` 만 쓰기 때문에 가능.

**실서버 기동 검증** (`uvicorn app.main:app --port 8011`):
- `GET /health` → 200, `GET /docs` → 200
- `openapi.json` 에 `POST /docent/ask` 가 `tags=["docent"]`, `summary="POI 기반 AI 도슨트 질의"`,
  응답 코드 `200 / 404 / 422` 로 노출됨
- `DocentAskResponse.source` 가 `enum: ["llm", "fallback"]` 으로 문서화됨
- 한글 description/example 이 UTF-8 로 정상 직렬화됨

> **미검증**: 실제 PostgreSQL 조회 경로. 이 개발 머신에 Docker·로컬 Postgres 가 없어
> `docker compose up -d` + `scripts/seed.py` 를 실행하지 못했다. 라우터~서비스~직렬화는
> 스텁 세션으로 전 구간 검증했으나, **시드된 실제 UUID 로 Swagger 수동 확인은 DB 가 있는
> 환경에서 한 번 더 필요**하다.

## 5. 작업 분해 (Task Breakdown)

- [ ] `docs/specs/feature-ai-docent-service.md` 커밋 (`docs(spec): add ai-docent-service spec`)
- [ ] `core/config.py` 에 `LLM_*` 설정 추가 + `.env.example` 갱신
- [ ] `services/llm/base.py` — `LlmClient` ABC, `LlmError`
- [ ] `services/llm/mock.py` — 컨텍스트를 요약해 그럴듯한 해설을 돌려주는 결정론적 구현
- [ ] `services/llm/factory.py` — `get_llm_client()`
- [ ] `services/docent.py` — `build_docent_prompt()`, `ask_docent()` (폴백 포함)
- [ ] `schemas/docent.py` — Request/Response + Swagger `examples`
- [ ] `routers/docent.py` — `POST /docent/ask` (docstring, `tags`, `summary`, `responses={404:...}`)
- [ ] `main.py` 라우터 등록
- [ ] `requirements.txt` 에 pytest, httpx 추가
- [ ] `tests/` 작성 (프롬프트 조립 단위 / 폴백 / 엔드포인트 스모크)
- [ ] Swagger UI 수동 검증 + 검증 메모 기록

## 6. 테스트 방법

- 로컬:
  1. `docker compose up -d` (DB 기동), `python scripts/seed.py` (시드)
  2. `uvicorn app.main:app --reload`
  3. `GET /exhibits/{id}` 로 POI id 확보 → Swagger `/docs` 에서 `POST /docent/ask` 실행
  4. `question` 유/무 두 경우, 없는 UUID(404), `LLM_PROVIDER` 를 일부러 실패시켜 폴백 확인
  5. `pytest`
- 멀티플레이 / Quest 3 검증 여부: 이 브랜치에서는 해당 없음(Swagger/curl 로 대체).
  UE5 `FHttpModule` 연동 검증은 클라이언트 측 별도 태스크에서 수행.

## 7. 리스크 / 열린 질문

- **LLM 벤더 미정** — 본 브랜치는 Mock까지만. 벤더별 응답 품질/지연/비용은 어댑터 추가 후 비교 필요.
- **응답 지연 KPI(≤2s)** — Mock에서는 계측이 무의미. 실제 벤더 붙인 뒤 `elapsed_ms` 로 측정하고,
  초과하면 Redis 캐싱(R3 대응) 브랜치를 진행.
- **Q&A 로그 미적재** — 현행 `view_logs` 는 (user_id, poi_id, viewed_at) 만 저장해 질문/응답 본문을
  남길 수 없다. 필요 시 `ai_docent_logs` 테이블 신설 스펙을 별도로 작성. `device_uuid` 필드는
  그 확장을 미리 받아두기 위해 요청 스키마에만 넣고 지금은 사용하지 않는다.
- **동시성** — sync 엔드포인트라 threadpool 크기가 상한. ADR-001로 50 CCU 부하 테스트가 보류되어
  당장은 위험이 낮으나, 다중 사용자 요구가 부활하면 async 이관을 함께 검토.
- **ID 타입** — 본 API는 `poi_id` 를 UUID로 받는다. 코어서버의 `FAiTriggerPacket.ObjectId` 는
  `uint32_t` 라 부활 시 불일치가 발생한다(ADR-001 §7). HTTP 직결 단계에서는 문제없음.
- **프롬프트 품질** — `ai_prompt_context` / `docent_text` 는 `feature/seed-data` §7 기준 임시 문안이며
  사실 검증이 필요하다. 프롬프트 개선은 데이터 갱신과 함께 진행.
- **CLAUDE.md 부재** — 기존 스펙들이 `CLAUDE.md` 를 명명 규칙 근거로 인용하지만 레포에 파일이 없다.
  본 스펙은 통합 문서 §1.6을 근거로 삼았다. 별도로 `CLAUDE.md` 생성을 검토할 것.
