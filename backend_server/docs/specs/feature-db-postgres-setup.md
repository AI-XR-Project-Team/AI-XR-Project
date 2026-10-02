# Feature Spec: PostgreSQL 구축 (Docker + 공룡 박물관 스키마)

- **Branch**: `feature/db-postgres-setup`
- **Status**: Implemented  <!-- Draft | Approved | Implemented | Merged -->
- **Author**: 현우
- **관련 이슈/태스크**: `docs/tasks/task-2-week1-python-server.md` Step 1

## 1. 목표 / 배경

Quest 3 기기 없이 PC만으로 API를 검증할 수 있도록, 공룡 박물관 AR의 데이터 저장소를 먼저 확정한다. Docker로 PostgreSQL을 로컬 기동하고 5개 테이블(DDL)을 생성해, 이후 FastAPI 스켈레톤(`feature/fastapi-skeleton`)과 Seed(`feature/seed-data`)가 붙을 기반을 만든다. 이 DB가 없으면 ORM 모델·엔드포인트·시드 어떤 것도 검증할 수 없다.

## 2. 범위

- 포함(In scope):
  - `docker-compose.yml` 로 PostgreSQL 16 로컬 기동
  - `users / dinosaurs / exhibits / pois / view_logs` 5개 테이블 DDL
  - DDL 초기화 스크립트(`db/init/01_schema.sql`)와 접속 확인
- 제외(Out of scope):
  - ORM 모델 매핑, 엔드포인트 → `feature/fastapi-skeleton`
  - 실제 데이터 삽입 → `feature/seed-data`
  - 인증/로그인 (관람객은 익명 device_uuid)
  - 마이그레이션 도구(Alembic) 도입 — 1주차 이후 논의

## 3. 설계

### 파일 / 모듈 구조
```
backend_server/
  docker-compose.yml         # postgres:16 서비스 정의
  .env.example               # POSTGRES_USER/PASSWORD/DB, DATABASE_URL 예시
  db/
    init/
      01_schema.sql          # 아래 DDL (컨테이너 최초 기동 시 자동 실행)
```
> `db/init/*.sql` 은 postgres 이미지가 첫 기동 시 자동 실행하는 위치(`/docker-entrypoint-initdb.d`)에 마운트한다.

### 인터페이스
- 접속 정보(로컬 기본값):
  - host `localhost`, port `5432`, db `dino_ar`, user `dino`, password `dino_dev_pw`
  - `DATABASE_URL=postgresql+psycopg2://dino:dino_dev_pw@localhost:5432/dino_ar`
- 데이터 스키마 / 모델 (DDL 요약):
  - `users(id UUID PK, device_uuid UNIQUE, created_at)` — Quest 3 익명 관람객
  - `dinosaurs(id UUID PK, name_ko, name_sci, period, length_m, model_asset_key, ai_prompt_context, created_at)`
  - `exhibits(id UUID PK, dinosaur_id FK→dinosaurs, label, anchor_hint, created_at)`
  - `pois(id UUID PK, exhibit_id FK→exhibits, part_name, pos_x_cm, pos_y_cm, pos_z_cm, docent_text)`
  - `view_logs(id BIGSERIAL PK, user_id FK→users, poi_id FK→pois, viewed_at)`
  - 좌표는 **UE5 Z-up, cm** 규약 준수.
  - `gen_random_uuid()` 사용 → `pgcrypto` 확장 필요 시 `CREATE EXTENSION IF NOT EXISTS pgcrypto;` 를 스키마 상단에 둔다. (PG13+ 기본 제공하나 명시)
- 외부 의존성: Docker, Docker Compose, postgres:16 이미지. (앱 라이브러리 없음)

### 데이터 흐름
1주차에는 DB 단독. 클라이언트(언리얼) ↔ 서버 흐름은 다음 브랜치에서 붙는다. 이 브랜치는 `docker compose up -d` → 컨테이너가 `01_schema.sql` 자동 실행 → `psql`/GUI로 테이블 확인까지.

## 4. 완료 조건 (Acceptance Criteria)

- [x] `docker compose up -d` 로 `db` 컨테이너가 healthy 상태로 기동된다
- [x] 컨테이너 최초 기동 시 `01_schema.sql` 이 자동 실행되어 5개 테이블이 생성된다
- [x] `psql ... -c "\dt"` 로 5개 테이블이 조회된다
- [x] FK 제약(exhibits→dinosaurs, pois→exhibits, view_logs→users/pois)이 걸려 있다
- [x] `.env.example` 에 접속 정보/`DATABASE_URL` 이 문서화되어 있다

### 검증 메모 (2026-07-18, 실행 검증 완료)

로컬에 Docker Desktop 대신 **Colima**(headless Docker 런타임)를 설치해 실제로 기동·검증했다. (`brew install colima docker docker-compose` → `colima start`)

- 기동: `docker compose up -d` → `dino_ar_db` 컨테이너 **Up (healthy)** 확인.
- 자동 초기화: 최초 기동 시 `01_schema.sql` 자동 실행 → `\dt` 로 5개 테이블(`users/dinosaurs/exhibits/pois/view_logs`) 조회 확인.
- FK: `pg_constraint` 조회로 FK 4개(exhibits→dinosaurs, pois→exhibits, view_logs→users, view_logs→pois) 확인.
- 스모크 테스트: dinosaurs→exhibits→pois INSERT 후 3-way JOIN 정상, `gen_random_uuid()` 기본값 동작, 존재하지 않는 FK INSERT 시 `foreign key constraint` 위반 에러로 차단 확인. 검증 후 `TRUNCATE ... CASCADE` 로 데이터 정리(실 시드는 `feature/seed-data` 담당).

## 5. 작업 분해 (Task Breakdown)

- [ ] `backend_server/docker-compose.yml` 작성 (postgres:16, 볼륨, 포트, `db/init` 마운트, healthcheck)
- [ ] `backend_server/db/init/01_schema.sql` 작성 (확장 + 5개 테이블 DDL)
- [ ] `backend_server/.env.example` 작성
- [ ] 로컬 기동 후 `\dt` / 간단 `INSERT`·`SELECT` 로 스키마 검증
- [ ] `.gitignore` 에 실제 `.env` 제외 확인

## 6. 테스트 방법

- 로컬: `docker compose up -d` → `docker compose ps` healthy 확인 → `psql ... -c "\dt"` 로 테이블 5개 확인 → 각 테이블 `\d <table>` 로 컬럼·FK 확인.
- 멀티플레이 / Quest 3 검증 여부: 해당 없음(1주차 PC 단독).

## 7. 리스크 / 열린 질문

- **레포 구조 미확정**: `backend_server/` 를 단일 레포(`AI-XR-Project`) 하위 폴더로 둘지, 별도 레포로 분리할지 팀 미팅 확정 필요. 확정 시 경로 조정.
- `gen_random_uuid()` 제공 여부는 PG 버전/확장 의존 → `pgcrypto` 확장 명시로 대응.
- 볼륨(`dino_pgdata`)이 남아 있으면 `db/init` 스크립트는 **재실행되지 않음**. 스키마 변경 시 `docker compose down -v` 로 볼륨 초기화 필요 — 팀에 공지.
