# Feature Spec: FastAPI 스켈레톤 (계층 분리 + ORM)

- **Branch**: `feature/fastapi-skeleton`
- **Status**: Implemented  <!-- Draft | Approved | Implemented | Merged -->
- **Author**: 현우
- **관련 이슈/태스크**: `docs/tasks/task-2-week1-python-server.md` Step 2
- **선행**: `feature/db-postgres-setup` (DB 스키마 존재 전제)

## 1. 목표 / 배경

DB 위에 계층 분리된 FastAPI 앱을 얹어, 서버 기동 시 Swagger UI(`/docs`)로 API를 즉시 테스트할 수 있게 한다. `feature/db-postgres-setup` 의 5개 테이블을 ORM 모델로 매핑하고 최소 조회 엔드포인트를 노출한다. 이 스켈레톤이 있어야 2주차부터 Gemini 도슨트 등 실제 기능을 얹을 자리가 생긴다.

## 2. 범위

- 포함(In scope):
  - 계층 구조 프로젝트(`core / models / schemas / routers`)
  - SQLAlchemy 엔진·세션, 5개 테이블 중 최소 `dinosaurs / exhibits / poi / user` ORM 매핑
  - 엔드포인트: `GET /health`, `GET /dinosaurs`, `GET /exhibits/{id}`
  - `requirements.txt`, `.env` 로드(config), Swagger UI 자동 노출
- 제외(Out of scope):
  - 쓰기(POST/PUT/DELETE) 엔드포인트 — 2주차
  - Gemini 도슨트, view_logs 집계 로직 — 2주차 이후
  - 인증/인가, 페이지네이션, 배포 설정

## 3. 설계

### 파일 / 모듈 구조
```
backend_server/
  app/
    main.py            # FastAPI 앱 엔트리, 라우터 등록, /docs 노출
    core/
      config.py        # pydantic-settings 로 .env 로드 (DATABASE_URL 등)
      db.py            # SQLAlchemy 엔진/세션, get_db 의존성
    models/
      __init__.py
      user.py          # class User(Base)
      dinosaur.py      # class Dinosaur(Base)
      exhibit.py       # class Exhibit(Base)
      poi.py           # class Poi(Base)
    schemas/
      dinosaur.py      # DinosaurRead(BaseModel)
      exhibit.py       # ExhibitRead, PoiRead
    routers/
      health.py        # GET /health
      dinosaurs.py     # GET /dinosaurs
      exhibits.py      # GET /exhibits/{id}
  requirements.txt     # fastapi, uvicorn[standard], sqlalchemy, psycopg2-binary, pydantic-settings
  .env.example         # DATABASE_URL (db 브랜치와 공유)
```

### 인터페이스
- API 엔드포인트:
  - `GET /health` → `200 {"status": "ok"}`
  - `GET /dinosaurs` → `200 [DinosaurRead, ...]`
  - `GET /exhibits/{id}` → `200 ExhibitRead` (POI 목록 포함) / 없으면 `404`
- 데이터 스키마 / 모델:
  - ORM `Base` 는 `feature/db-postgres-setup` DDL과 컬럼·타입 일치. 마이그레이션은 이 브랜치에서 하지 않음(DDL이 진실 소스, ORM은 매핑만).
  - Pydantic `*Read` 스키마는 `from_attributes=True` (ORM 객체 직렬화).
- 외부 의존성: fastapi, uvicorn[standard], sqlalchemy, psycopg2-binary, pydantic-settings.

### 데이터 흐름
클라이언트(언리얼/Postman) → `GET /dinosaurs` → 라우터가 `get_db` 세션 주입 → SQLAlchemy 쿼리(`select(Dinosaur)`) → ORM 객체 → Pydantic `DinosaurRead` 직렬화 → JSON 응답. `/exhibits/{id}` 는 exhibit + 연결된 pois 를 함께 반환.

### 코드 스타일 (CLAUDE.md 준수)
- 클래스: `PascalCase` (`class Dinosaur(Base):`)
- 함수/변수: `snake_case` (`def get_dinosaurs():`)
- 상수: `UPPER_SNAKE_CASE` (`DATABASE_URL`)
- private 멤버: `_` 접두사

## 4. 완료 조건 (Acceptance Criteria)

- [x] `uvicorn app.main:app --reload` 로 서버가 기동된다
- [x] `http://localhost:8000/docs` 에서 Swagger UI 가 3개 엔드포인트를 노출한다
- [x] `GET /health` 가 `200 {"status":"ok"}` 를 반환한다
- [x] `GET /dinosaurs` 가 DB 조회 결과를 JSON 배열로 반환한다 (데이터 없으면 `[]`)
- [x] `GET /exhibits/{id}` 가 존재 시 200, 미존재 시 404 를 반환한다
- [x] 코드가 CLAUDE.md 명명 규칙을 따른다 (PascalCase 클래스 / snake_case 함수)

### 검증 메모 (2026-07-18, 실행 검증 완료)

실행 중인 PostgreSQL(Colima) 위에서 uvicorn 기동 후 curl 로 호출 검증:
- `GET /health` → `{"status":"ok"}`
- `GET /dinosaurs` → 티라노사우루스 1건 JSON 반환(name_ko/name_sci/period/length_m 등)
- `GET /exhibits/{id}` → 전시물 + POI 3건(두개골 z=180 / 앞발 z=150 / 꼬리 z=70, UE5 Z-up cm) 포함 반환
- `GET /exhibits/{없는 UUID}` → `404 {"detail":"exhibit not found"}`
- Swagger UI(`/docs`) 3개 엔드포인트 노출 확인.
> 티라노 데이터는 실행 중 DB 에 임시 삽입해 테스트(정식 시드는 `feature/seed-data` 담당).

## 5. 작업 분해 (Task Breakdown)

- [ ] `requirements.txt` 작성 및 가상환경 설치
- [ ] `core/config.py` (Settings, `.env` 로드), `core/db.py` (engine/SessionLocal/`get_db`)
- [ ] `models/*` 4종 ORM 매핑 + `Base`
- [ ] `schemas/*` Pydantic Read 스키마
- [ ] `routers/*` 3종 + `main.py` 라우터 등록
- [ ] 테스트 작성 (pytest + httpx TestClient: `/health`, `/dinosaurs` 스모크)

## 6. 테스트 방법

- 로컬: `feature/db-postgres-setup` DB 기동 상태에서 `uvicorn app.main:app --reload` → 브라우저 `/docs` 확인 → Swagger 또는 Postman 으로 3개 엔드포인트 호출. `pytest` 로 `/health`·`/dinosaurs` 스모크 테스트.
- 멀티플레이 / Quest 3 검증 여부: 해당 없음(PC 단독, Postman/Swagger 대체).

## 7. 리스크 / 열린 질문

- ORM ↔ DDL 컬럼 불일치 위험 → DDL을 진실 소스로 두고 ORM은 수동 매핑, 스모크 테스트로 검증.
- `/dinosaurs` 는 Seed 전이라 빈 배열 반환 → 실제 데이터 검증은 `feature/seed-data` 이후.
- `backend_server/` 경로는 레포 구조 확정(단일 레포 폴더 vs 분리)에 따라 조정.
