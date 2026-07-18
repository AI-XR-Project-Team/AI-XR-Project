"""공룡 박물관 AR — 테스트 시드 데이터 (멱등).

Quest 3 실측 데이터가 없는 1주차에, API 가 빈 배열이 아니라 실제 값을
반환하도록 최소 데이터(티라노 1종 + 전시물 1개 + POI 3개)를 넣는다.

실행 (server/ 를 작업 디렉터리로):
    python scripts/seed.py
    # 또는
    python -m scripts.seed

재실행해도 자연키로 존재를 확인하므로 중복 삽입되지 않는다(멱등).
"""
import os
import sys
from decimal import Decimal

# `python scripts/seed.py` 로 직접 실행해도 app 패키지를 찾도록 server 루트를 경로에 추가
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import SessionLocal          # noqa: E402
from app.models.dinosaur import Dinosaur      # noqa: E402
from app.models.exhibit import Exhibit        # noqa: E402
from app.models.poi import Poi                # noqa: E402

# --- 시드 데이터 정의 -------------------------------------------------

TYRANNO = dict(
    name_ko="티라노사우루스",
    name_sci="Tyrannosaurus rex",
    period="백악기 후기",
    length_m=Decimal("12.3"),
    model_asset_key="trex_full_skeleton",
    ai_prompt_context=(
        "티라노사우루스(Tyrannosaurus rex)는 약 6,800만~6,600만 년 전 백악기 후기 "
        "북아메리카에 살았던 대형 수각류 공룡이다. 몸길이 약 12m, 강력한 턱과 톱니 달린 "
        "원뿔형 이빨로 먹이를 물어뜯는 최상위 포식자였으며, 앞발은 두 개의 발가락으로 "
        "퇴화했다. 도슨트는 관람객 눈높이에서 크기·먹이·서식 환경을 쉽게 설명한다."
    ),
)

EXHIBIT = dict(
    label="1관 티라노 전신골격",
    anchor_hint="중앙 홀 바닥 마커 A1 기준 정렬",
)

# (part_name, pos_x_cm, pos_y_cm, pos_z_cm, docent_text) — UE5 Z-up, cm
POIS = [
    ("두개골", Decimal("0"), Decimal("0"), Decimal("180"),
     "길이 약 1.5m의 거대한 두개골. 톱니 달린 원뿔형 이빨이 늘어서 있다."),
    ("갈비뼈", Decimal("20"), Decimal("35"), Decimal("120"),
     "넓은 흉곽을 이루는 갈비뼈. 거대한 폐와 내장을 감싸 보호했다."),
    ("대퇴골", Decimal("15"), Decimal("-10"), Decimal("70"),
     "두껍고 강한 대퇴골. 빠른 보행을 지탱한 뒷다리의 핵심 뼈다."),
]


# --- 시드 로직 (멱등) -------------------------------------------------

def seed() -> None:
    db = SessionLocal()
    try:
        # 1) 공룡 — name_sci 를 자연키로 존재 확인
        dino = db.query(Dinosaur).filter_by(name_sci=TYRANNO["name_sci"]).first()
        if dino is None:
            dino = Dinosaur(**TYRANNO)
            db.add(dino)
            db.flush()  # dino.id 확보
            print(f"[+] dinosaur 삽입: {dino.name_ko}")
        else:
            print(f"[=] dinosaur 이미 존재: {dino.name_ko} (건너뜀)")

        # 2) 전시물 — label 을 자연키로 존재 확인
        exhibit = db.query(Exhibit).filter_by(label=EXHIBIT["label"]).first()
        if exhibit is None:
            exhibit = Exhibit(dinosaur_id=dino.id, **EXHIBIT)
            db.add(exhibit)
            db.flush()  # exhibit.id 확보
            print(f"[+] exhibit 삽입: {exhibit.label}")
        else:
            print(f"[=] exhibit 이미 존재: {exhibit.label} (건너뜀)")

        # 3) POI — (exhibit_id, part_name) 을 자연키로 존재 확인
        for part_name, x, y, z, docent_text in POIS:
            exists = (
                db.query(Poi)
                .filter_by(exhibit_id=exhibit.id, part_name=part_name)
                .first()
            )
            if exists is None:
                db.add(Poi(
                    exhibit_id=exhibit.id,
                    part_name=part_name,
                    pos_x_cm=x, pos_y_cm=y, pos_z_cm=z,
                    docent_text=docent_text,
                ))
                print(f"[+] poi 삽입: {part_name}")
            else:
                print(f"[=] poi 이미 존재: {part_name} (건너뜀)")

        db.commit()
        print("시드 완료.")
    finally:
        db.close()


if __name__ == "__main__":
    seed()
