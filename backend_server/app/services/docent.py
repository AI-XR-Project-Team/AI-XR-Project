"""AI 도슨트 — DB 컨텍스트 조립 → LLM 호출 → 실패 시 폴백.

원안(통합 문서)에서는 C++ 코어서버가 `PKT_AI_TRIGGER(0x06)` 를 받아 이 로직으로
포워딩했으나, ADR-001 에 따라 UE5 가 HTTP 로 직접 호출한다.
관람객 응시(Gaze) 2초 임계 판정은 UE5 클라이언트가 담당한다.
"""
import logging
import uuid
from dataclasses import dataclass
from typing import Literal, Optional, Tuple

from sqlalchemy.orm import Session

from app.models.dinosaur import Dinosaur
from app.models.exhibit import Exhibit
from app.models.poi import Poi
from app.services.llm.base import LlmClient, LlmError

logger = logging.getLogger(__name__)

# 질문 없이 호출됐을 때(단순 응시) 사용할 기본 요청 문장.
DEFAULT_QUESTION = "이 부위를 관람객에게 설명해줘."

# 도슨트 페르소나. 배경지식은 아래에 DB 값이 이어 붙는다.
PERSONA = (
    "당신은 공룡 박물관의 AI 도슨트입니다. "
    "관람객은 Meta Quest 3 를 착용하고 실제 크기의 전시물을 눈앞에서 보고 있습니다.\n"
    "- 한국어 존댓말로, 3~4문장 이내로 간결하게 답합니다.\n"
    "- 아래 배경지식에 없는 내용은 지어내지 말고 모른다고 말합니다.\n"
    "- 관람객이 지금 보고 있는 부위를 중심으로 설명합니다."
)


@dataclass
class DocentResult:
    """도슨트 응답 + 그 출처."""

    answer: str
    source: Literal["llm", "fallback"]


def load_poi_context(
    db: Session, poi_id: uuid.UUID
) -> Optional[Tuple[Poi, Exhibit, Optional[Dinosaur]]]:
    """POI 와 그 상위 전시물·공룡을 함께 조회한다. POI 가 없으면 None."""
    poi = db.get(Poi, poi_id)
    if poi is None:
        return None

    exhibit = db.get(Exhibit, poi.exhibit_id)
    if exhibit is None:
        # FK NOT NULL 이라 실제로는 도달하지 않지만, 데이터 정합이 깨진 경우 방어.
        logger.error("poi=%s 의 exhibit=%s 를 찾을 수 없다", poi_id, poi.exhibit_id)
        return None

    return poi, exhibit, db.get(Dinosaur, exhibit.dinosaur_id)


def build_docent_prompt(
    poi: Poi, exhibit: Exhibit, dinosaur: Optional[Dinosaur]
) -> str:
    """DB 값을 모아 LLM 에 넘길 system 프롬프트를 만든다.

    `dinosaurs.ai_prompt_context` 가 종 배경지식의 핵심 소스이고,
    `pois.docent_text` 는 부위별 사전 해설로서 힌트 역할을 한다.
    """
    lines = [PERSONA, "", "[전시물]", f"- 라벨: {exhibit.label}"]
    if exhibit.anchor_hint:
        lines.append(f"- 배치: {exhibit.anchor_hint}")

    if dinosaur is not None:
        lines += ["", "[공룡]", f"- 이름: {dinosaur.name_ko} ({dinosaur.name_sci})"]
        if dinosaur.period:
            lines.append(f"- 시대: {dinosaur.period}")
        if dinosaur.length_m is not None:
            lines.append(f"- 전장: 약 {dinosaur.length_m}m")
        if dinosaur.ai_prompt_context:
            lines += ["", "[배경지식]", dinosaur.ai_prompt_context]

    lines += ["", "[관람객이 보고 있는 부위]", f"- 부위: {poi.part_name}"]
    if poi.docent_text:
        lines.append(f"- 기존 해설: {poi.docent_text}")

    return "\n".join(lines)


def build_fallback_answer(poi: Poi) -> str:
    """LLM 을 쓸 수 없을 때 돌려줄 사전 작성 해설.

    통합 문서 리스크 R3(LLM 응답 지연)과 KPI(도슨트 응답 <=2s) 대응.
    LLM 이 죽어도 관람 경험이 끊기지 않도록 500 대신 이 값을 반환한다.
    """
    if poi.docent_text:
        return poi.docent_text
    return f"'{poi.part_name}' 부위입니다. 자세한 해설은 준비 중입니다."


def ask_docent(
    poi: Poi,
    exhibit: Exhibit,
    dinosaur: Optional[Dinosaur],
    question: Optional[str],
    llm: LlmClient,
) -> DocentResult:
    """컨텍스트를 조립해 LLM 에 묻고, 실패하면 폴백 해설을 돌려준다."""
    system_prompt = build_docent_prompt(poi, exhibit, dinosaur)
    user_prompt = (question or "").strip() or DEFAULT_QUESTION

    try:
        answer = llm.generate(system_prompt, user_prompt)
    except LlmError as exc:
        logger.warning("LLM 호출 실패 poi=%s: %s — 폴백", poi.id, exc)
        return DocentResult(build_fallback_answer(poi), "fallback")
    except Exception:
        # 벤더 어댑터가 LlmError 로 감싸지 못한 예외까지 흡수한다.
        # 도슨트는 관람 중 호출되므로 500 보다 폴백이 낫다.
        logger.exception("LLM 호출 중 예상치 못한 예외 poi=%s — 폴백", poi.id)
        return DocentResult(build_fallback_answer(poi), "fallback")

    answer = (answer or "").strip()
    if not answer:
        logger.warning("LLM 이 빈 응답 반환 poi=%s — 폴백", poi.id)
        return DocentResult(build_fallback_answer(poi), "fallback")

    return DocentResult(answer, "llm")
