"""LLM 연동 점검 — DB 없이 실제 LLM 호출까지 확인한다.

PostgreSQL/Docker 없이도 "키가 유효한가 / 프롬프트가 제대로 조립되는가 /
2초 KPI 안에 응답이 오는가" 를 한 번에 볼 수 있다.
시드 데이터(티라노 두개골)와 같은 값을 가짜 객체로 만들어 실제 경로를 태운다.

실행 (backend_server/ 를 작업 디렉터리로):
    python scripts/check_llm.py
    python scripts/check_llm.py "이 공룡은 무엇을 먹었나요?"
"""
import os
import sys
import time
from decimal import Decimal
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.config import settings                    # noqa: E402
from app.services.docent import ask_docent, build_docent_prompt  # noqa: E402
from app.services.llm.factory import get_llm_client     # noqa: E402

# scripts/seed.py 의 티라노 데이터와 동일 (DB 대신 메모리 객체)
DINOSAUR = SimpleNamespace(
    name_ko="티라노사우루스",
    name_sci="Tyrannosaurus rex",
    period="백악기 후기",
    length_m=Decimal("12.3"),
    ai_prompt_context=(
        "티라노사우루스(Tyrannosaurus rex)는 약 6,800만~6,600만 년 전 백악기 후기 "
        "북아메리카에 살았던 대형 수각류 공룡이다. 몸길이 약 12m, 강력한 턱과 톱니 달린 "
        "원뿔형 이빨로 먹이를 물어뜯는 최상위 포식자였으며, 앞발은 두 개의 발가락으로 "
        "퇴화했다. 도슨트는 관람객 눈높이에서 크기·먹이·서식 환경을 쉽게 설명한다."
    ),
)
EXHIBIT = SimpleNamespace(
    id="(dry-run)",
    label="1관 티라노 전신골격",
    anchor_hint="중앙 홀 바닥 마커 A1 기준 정렬",
)
POI = SimpleNamespace(
    id="(dry-run)",
    part_name="두개골",
    docent_text="길이 약 1.5m의 거대한 두개골. 톱니 달린 원뿔형 이빨이 늘어서 있다.",
)


def main() -> int:
    question = sys.argv[1] if len(sys.argv) > 1 else None

    print("=" * 66)
    print(f"  provider : {settings.LLM_PROVIDER}")
    print(f"  model    : {settings.LLM_MODEL}")
    print(f"  thinking : {settings.LLM_THINKING_LEVEL}   timeout: {settings.LLM_TIMEOUT_SEC}s")
    print(f"  api key  : {'설정됨' if settings.LLM_API_KEY else '*** 비어 있음 ***'}")
    print("=" * 66)

    if settings.LLM_PROVIDER == "gemini" and not settings.LLM_API_KEY:
        print("\n[실패] .env 의 LLM_API_KEY 가 비어 있다.")
        print("       https://aistudio.google.com/apikey 에서 키를 발급해(무료)")
        print("       backend_server/.env 의 LLM_API_KEY= 뒤에 붙여넣어라.")
        return 1

    print("\n--- 조립된 system 프롬프트 ---")
    print(build_docent_prompt(POI, EXHIBIT, DINOSAUR))

    print(f"\n--- 질문: {question or '(없음 → 기본 해설)'} ---")
    started = time.perf_counter()
    result = ask_docent(POI, EXHIBIT, DINOSAUR, question, get_llm_client())
    elapsed_ms = int((time.perf_counter() - started) * 1000)

    print(f"\n--- 응답 (source={result.source}, {elapsed_ms}ms) ---")
    print(result.answer)

    print()
    if result.source == "fallback":
        print("[실패] LLM 호출이 안 돼서 DB 사전 해설로 폴백했다.")
        print("       위에 찍힌 경고 로그(키 오류 / 쿼터 초과 / 타임아웃)를 확인하라.")
        return 1

    print(f"[성공] LLM 응답 수신 ({elapsed_ms}ms)")
    if elapsed_ms > 2000:
        print("[참고] 2초 KPI 초과. 첫 호출은 클라이언트 생성이 포함돼 느리다.")
        print("       계속 넘으면 LLM_THINKING_LEVEL=minimal 확인, 또는")
        print("       LLM_MODEL=gemini-3.1-flash-lite (실측 ~1초) 검토.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
