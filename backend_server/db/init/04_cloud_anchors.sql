-- =====================================================================
-- 공룡 박물관 AR — 10단계: ARCore Cloud Anchors (보이지 않는 측위 마커)
-- 컨테이너(postgres:16) 최초 기동 시 02_nav_schema.sql 다음으로 자동 실행.
-- 기존 볼륨엔 자동 적용 안 되므로 수동 적용:
--   python scripts/apply_cloud_anchors.py
--   (또는 psql "$DATABASE_URL" -f db/init/04_cloud_anchors.sql)
-- 모든 DDL 이 IF NOT EXISTS 라 재실행 안전.
-- 좌표 규약: UE5 Z-up, cm. heading: +X축 기준 CCW °(markers 와 동일).
-- 배경/판정: docs/nav-stage10-ttl-probe-runbook.md, 프로젝트 메모리 stage10_cloud_anchors.md.
-- =====================================================================

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

-- ---------------------------------------------------------------------
-- cloud_anchors : Google Cloud Anchor ↔ 맵 좌표 매핑 + 수명(Management API 미러).
--
--   * markers 와 같은 "측위 기준점"이지만 물리 QR 대신 3D 특징점 앵커라 미관 훼손 0.
--   * cloud_id = 앱이 resolveCloudAnchor 에 넘기는 Google 앵커 ID(리졸브 키).
--   * pos_*/heading_deg = 리졸브 성공 시 앱이 자기 위치를 정합할 맵 좌표.
--   * create/expire/max_expire/last_localize_time = ARCore Cloud Anchor Management API
--     미러. TTL 자동 연장 잡(scripts/extend_cloud_anchor_ttls.py)이 갱신한다.
--     - API 키 호스팅 앵커도 max_expire_time = create_time + 365일 까지 연장 가능
--       (10단계 게이트에서 실측 확인). 기본 호스팅 TTL 은 1일이라 서버가 늘려줘야 한다.
--   * enabled=false : 죽었거나 재호스팅 대기 중인 앵커(리졸브 목록에서 제외, 행은 보존).
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS cloud_anchors (
    id                 UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    map_id             UUID         NOT NULL
                       REFERENCES map_spaces(id) ON DELETE CASCADE,
    point_no           INTEGER      NOT NULL,                 -- 지도 계획상 포인트 번호(1..N). 좌표의 주키.
    cloud_id           VARCHAR(200) UNIQUE,                   -- Google Cloud Anchor ID (리졸브 키). 바인딩 전 NULL
    pos_x_cm           NUMERIC(8,2) NOT NULL,                 -- UE5 X (cm)
    pos_y_cm           NUMERIC(8,2) NOT NULL,                 -- UE5 Y (cm)
    pos_z_cm           NUMERIC(8,2) NOT NULL,                 -- UE5 Z(up) (cm)
    heading_deg        NUMERIC(6,2) NOT NULL DEFAULT 0,       -- 앵커 정면(+X 기준 CCW °)
    edge               VARCHAR(20),                           -- 이 포인트가 놓인 네비 edge(예: "A-B"). 11단계 배치 규칙
    label              VARCHAR(200),                          -- 사람이 읽는 위치 이름(예: "1관 입구 기둥")
    enabled            BOOLEAN      NOT NULL DEFAULT TRUE,     -- false=리졸브 목록서 제외

    -- Management API 미러 (자동 연장 잡이 관리) --------------------------------
    create_time        TIMESTAMPTZ,                           -- 앵커 호스팅 시각
    expire_time        TIMESTAMPTZ,                           -- 현재 만료 시각(잡이 max 로 밀어 올림)
    max_expire_time    TIMESTAMPTZ,                           -- 연장 상한(create_time + 365일 고정)
    last_localize_time TIMESTAMPTZ,                           -- 마지막 리졸브(관측) 시각
    ttl_synced_at      TIMESTAMPTZ,                           -- 서버가 마지막으로 API 와 동기화한 시각
    ttl_extended_at    TIMESTAMPTZ,                           -- 365일 연장이 성공한 시각. NULL=미연장(잡이 픽업 대상)
    verified_at        TIMESTAMPTZ,                           -- 앱 재시작 후 리졸브 검증 통과 시각(11단계 §4①)
    verify_latency_ms  INTEGER,                               -- 그때의 리졸브 지연(실측 기록용)
    note               VARCHAR(200),

    CONSTRAINT uq_cloud_anchors_point UNIQUE (map_id, point_no)
);

CREATE INDEX IF NOT EXISTS idx_cloud_anchors_map ON cloud_anchors(map_id);
-- 관리자 앱의 "미등록 포인트" 조회용.
CREATE INDEX IF NOT EXISTS idx_cloud_anchors_unbound ON cloud_anchors(map_id)
    WHERE cloud_id IS NULL;
-- 자동 연장 잡이 "곧 만료" 앵커만 스캔하도록.
CREATE INDEX IF NOT EXISTS idx_cloud_anchors_expire ON cloud_anchors(expire_time)
    WHERE enabled;


-- =====================================================================
-- 11단계 마이그레이션 — 이미 04 를 적용한 DB 를 위한 멱등 ALTER.
-- (신규 DB 는 위 CREATE TABLE 로 이미 반영돼 있어 전부 no-op 이다.)
--
-- 11단계 D5': 좌표를 지도에서 사전 확정하고(포인트 행 먼저 생성),
--             현장 등록은 그 포인트에 cloud_id 를 바인딩하는 작업이다.
--             → cloud_id 는 바인딩 전까지 NULL 이어야 한다.
-- =====================================================================
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS point_no          INTEGER;
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS edge              VARCHAR(20);
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS ttl_extended_at   TIMESTAMPTZ;
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS verified_at       TIMESTAMPTZ;
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS verify_latency_ms INTEGER;
ALTER TABLE cloud_anchors ALTER COLUMN cloud_id DROP NOT NULL;

-- point_no 채우기(기존 행이 있다면 map 별 순번 부여) 후 NOT NULL·UNIQUE 승격.
UPDATE cloud_anchors c SET point_no = s.rn
  FROM (SELECT id, row_number() OVER (PARTITION BY map_id ORDER BY cloud_id) AS rn
          FROM cloud_anchors) s
 WHERE c.id = s.id AND c.point_no IS NULL;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_cloud_anchors_point') THEN
        ALTER TABLE cloud_anchors ADD CONSTRAINT uq_cloud_anchors_point UNIQUE (map_id, point_no);
    END IF;
    BEGIN
        ALTER TABLE cloud_anchors ALTER COLUMN point_no SET NOT NULL;
    EXCEPTION WHEN others THEN
        RAISE NOTICE 'point_no NOT NULL 승격 보류(기존 행 확인 필요)';
    END;
END $$;

CREATE INDEX IF NOT EXISTS idx_cloud_anchors_unbound ON cloud_anchors(map_id)
    WHERE cloud_id IS NULL;
