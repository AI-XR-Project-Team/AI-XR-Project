-- =====================================================================
-- 공룡 박물관 AR — 초기 스키마 (1주차)
-- 컨테이너(postgres:16) 최초 기동 시 /docker-entrypoint-initdb.d 에서 자동 실행됨.
-- 좌표 규약: UE5 Z-up, 단위 cm.
-- =====================================================================

-- gen_random_uuid() 보장 (PG13+ 기본 제공하나 명시적으로 확장 생성)
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- ---------------------------------------------------------------------
-- users : 관람객 (로그인 없음, Quest 3 익명 device UUID)
-- ---------------------------------------------------------------------
CREATE TABLE users (
    id          UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    device_uuid VARCHAR(128) UNIQUE NOT NULL,      -- 기기 식별자
    created_at  TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------
-- dinosaurs : 공룡 종 (예: 티라노사우루스 / 백악기)
-- ---------------------------------------------------------------------
CREATE TABLE dinosaurs (
    id                UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    name_ko           VARCHAR(100) NOT NULL,       -- 티라노사우루스
    name_sci          VARCHAR(100) NOT NULL,       -- Tyrannosaurus rex
    period            VARCHAR(50),                 -- 백악기 등
    length_m          NUMERIC(5,2),                -- 전장(m)
    model_asset_key   VARCHAR(200),                -- UE5 렌더 에셋 키
    ai_prompt_context TEXT,                        -- Gemini 도슨트 프롬프트 컨텍스트
    created_at        TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------
-- exhibits : 전시물 (특정 공룡 종의 화석/전시 인스턴스)
-- ---------------------------------------------------------------------
CREATE TABLE exhibits (
    id           UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    dinosaur_id  UUID         NOT NULL
                 REFERENCES dinosaurs(id) ON DELETE CASCADE,
    label        VARCHAR(200) NOT NULL,            -- 전시 라벨
    anchor_hint  VARCHAR(200),                     -- 공간 앵커 힌트
    created_at   TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------
-- pois : 관심 지점 (전시물의 부위별 좌표 + 도슨트 텍스트)
-- 좌표: UE5 Z-up, cm
-- ---------------------------------------------------------------------
CREATE TABLE pois (
    id          UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    exhibit_id  UUID         NOT NULL
                REFERENCES exhibits(id) ON DELETE CASCADE,
    part_name   VARCHAR(100) NOT NULL,             -- 두개골, 대퇴골 등
    pos_x_cm    NUMERIC(8,2) NOT NULL,             -- UE5 X (cm)
    pos_y_cm    NUMERIC(8,2) NOT NULL,             -- UE5 Y (cm)
    pos_z_cm    NUMERIC(8,2) NOT NULL,             -- UE5 Z(up) (cm)
    docent_text TEXT
);

-- ---------------------------------------------------------------------
-- view_logs : 관람 로그 (어떤 관람객이 어떤 POI 를 언제 봤는지)
-- ---------------------------------------------------------------------
CREATE TABLE view_logs (
    id        BIGSERIAL    PRIMARY KEY,
    user_id   UUID         NOT NULL
              REFERENCES users(id) ON DELETE CASCADE,
    poi_id    UUID         NOT NULL
              REFERENCES pois(id)  ON DELETE CASCADE,
    viewed_at TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- 조회 성능용 인덱스 (FK 컬럼)
CREATE INDEX idx_exhibits_dinosaur_id ON exhibits(dinosaur_id);
CREATE INDEX idx_pois_exhibit_id      ON pois(exhibit_id);
CREATE INDEX idx_view_logs_user_id    ON view_logs(user_id);
CREATE INDEX idx_view_logs_poi_id     ON view_logs(poi_id);
