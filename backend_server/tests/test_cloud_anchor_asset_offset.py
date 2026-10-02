"""13-1 — 에셋 배치 보정(asset-offset) 저장·조회·재바인딩 무효화.

DB 없이 스텁 세션으로 라우터만 검증한다.
"""
import os
import uuid
from decimal import Decimal

os.environ.setdefault("SKIP_SCHEMA_PATCH", "1")

import pytest  # noqa: E402
from fastapi.testclient import TestClient  # noqa: E402

from app.core.db import get_db  # noqa: E402
from app.main import app  # noqa: E402
from app.models.cloud_anchor import CloudAnchor  # noqa: E402
from app.models.map_space import MapSpace  # noqa: E402

MAP_ID = uuid.UUID("385f15c8-bf2c-58ec-a2d7-8db2aa34ac0b")


class _Session:
    def __init__(self, row):
        self.row = row

    def get(self, model, pk):
        return MapSpace(id=MAP_ID) if model is MapSpace and pk == MAP_ID else None

    def scalar(self, stmt):
        return self.row

    def scalars(self, stmt):
        row = self.row

        class _R:
            def all(self_inner):
                return [row]

        return _R()

    def commit(self):
        pass

    def refresh(self, row):
        pass


@pytest.fixture
def row():
    return CloudAnchor(
        id=uuid.uuid4(), map_id=MAP_ID, point_no=203, cloud_id="ua-old",
        pos_x_cm=Decimal("0"), pos_y_cm=Decimal("0"), pos_z_cm=Decimal("0"),
        heading_deg=Decimal("90"), label="asset3", enabled=True, note="keep",
    )


@pytest.fixture
def client(row, monkeypatch):
    # bind 의 TTL 백그라운드 연장은 끈다(네트워크 금지).
    monkeypatch.setattr("app.routers.cloud_anchors.schedule_ttl_extension",
                        lambda *a, **k: None)
    app.dependency_overrides[get_db] = lambda: _Session(row)
    yield TestClient(app)
    app.dependency_overrides.pop(get_db, None)


BASE = f"/maps/{MAP_ID}/cloud-anchors"


def test_offset_null_until_saved(client):
    r = client.get(BASE)
    assert r.status_code == 200
    assert r.json()[0]["asset_offset"] is None


def test_put_offset_persists_and_keeps_other_fields(client, row):
    body = {"x_cm": 12.5, "y_cm": -3, "z_cm": 4, "yaw_deg": -15, "scale": 0.85}
    r = client.put(f"{BASE}/points/203/asset-offset", json=body)
    assert r.status_code == 200, r.text
    off = r.json()["asset_offset"]
    assert float(off["x_cm"]) == 12.5 and float(off["scale"]) == 0.85
    assert float(off["yaw_deg"]) == -15
    # 부분 갱신 — 좌표·라벨·cloud_id·note 보존
    assert row.cloud_id == "ua-old" and row.label == "asset3" and row.note == "keep"
    assert client.get(BASE).json()[0]["asset_offset"]["z_cm"] in ("4.00", "4", 4, 4.0)


def test_put_offset_rejects_bad_scale(client):
    r = client.put(f"{BASE}/points/203/asset-offset", json={"scale": 0})
    assert r.status_code == 422


def test_delete_offset(client):
    client.put(f"{BASE}/points/203/asset-offset", json={"x_cm": 1})
    r = client.delete(f"{BASE}/points/203/asset-offset")
    assert r.status_code == 200 and r.json()["asset_offset"] is None


def test_rebind_new_cloud_id_clears_offset(client):
    client.put(f"{BASE}/points/203/asset-offset", json={"x_cm": 50})
    r = client.put(f"{BASE}/points/203/bind", json={"cloud_id": "ua-old"})
    assert r.json()["asset_offset"] is not None   # 같은 앵커 재바인딩 → 유지
    r = client.put(f"{BASE}/points/203/bind", json={"cloud_id": "ua-new"})
    assert r.json()["asset_offset"] is None       # 새 앵커 → 옛 보정 무효


def test_schema_patch_statements_split():
    from app.services.schema_patch import _SQL_FILE, _statements

    stmts = _statements(_SQL_FILE.read_text(encoding="utf-8"))
    assert len(stmts) == 6
    assert all(s.startswith("ALTER TABLE cloud_anchors ADD COLUMN IF NOT EXISTS") for s in stmts)
