"""일회성 정리: 느티나무 맵의 중복 nav 노드/간선 제거 후 재시드.

배경: 과거 시더가 (map_id, label) 자연키였던 탓에 라벨 변경(4단계 'G지점'→
7단계 '브라키오사우르스' 등) 시 같은 좌표에 중복 노드가 INSERT 되어 그래프가
두 겹으로 갈라졌다(길안내 우회 버그). 시더는 (map_id, 좌표) 키로 수정됐으므로
기존에 쌓인 중복만 한 번 비우고 CSV 기준으로 다시 채운다.

느티나무 맵에는 exhibits.nav_node_id 참조가 없어(자연사관 1층만 참조) 노드
삭제가 FK 를 깨지 않는다. floor1 은 건드리지 않는다.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import SessionLocal          # noqa: E402
from app.models.map_space import MapSpace     # noqa: E402
from app.models.nav_edge import NavEdge       # noqa: E402
from app.models.nav_node import NavNode       # noqa: E402

TARGET_MAPS = ("느티나무 4층(확장)", "느티나무 4층 테스트 유닛")


def purge() -> None:
    db = SessionLocal()
    try:
        map_ids = [m.id for m in db.query(MapSpace)
                   .filter(MapSpace.name.in_(TARGET_MAPS)).all()]
        if not map_ids:
            print("대상 맵 없음 — 아무 것도 안 함")
            return
        e = (db.query(NavEdge).filter(NavEdge.map_id.in_(map_ids))
             .delete(synchronize_session=False))
        n = (db.query(NavNode).filter(NavNode.map_id.in_(map_ids))
             .delete(synchronize_session=False))
        db.commit()
        print(f"삭제: nav_edges {e}행, nav_nodes {n}행 (맵 {len(map_ids)}개)")
    finally:
        db.close()


if __name__ == "__main__":
    purge()
