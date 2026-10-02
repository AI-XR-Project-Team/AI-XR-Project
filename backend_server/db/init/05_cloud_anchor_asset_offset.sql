-- =====================================================================
-- 13-1 — cloud_anchors 에 "앵커 위 에셋 배치 보정" 컬럼 추가 (멱등).
-- 현장 앱(13-1 테스트 앱)의 조정 패드가 PUT .../points/{n}/asset-offset 으로 저장한다.
-- 축: 앵커 로컬 X=앞 Y=우 Z=위(cm) · yaw(°) · 배율. 앵커 pose 기준이라 재바인딩 시 지운다.
-- 서버 기동 시 app.services.schema_patch.ensure_asset_offset_columns 가 자동 적용한다.
-- 수동: psql "$DATABASE_URL" -f db/init/05_cloud_anchor_asset_offset.sql
-- =====================================================================
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS asset_off_x_cm          NUMERIC(8,2);
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS asset_off_y_cm          NUMERIC(8,2);
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS asset_off_z_cm          NUMERIC(8,2);
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS asset_off_yaw_deg       NUMERIC(6,2);
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS asset_scale             NUMERIC(6,3);
ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS asset_offset_updated_at TIMESTAMPTZ;
