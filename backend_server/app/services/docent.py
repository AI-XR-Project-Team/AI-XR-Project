"""AI 도슨트 — DB 컨텍스트 조립 → LLM 호출 → 실패 시 폴백.

원안(통합 문서)에서는 C++ 코어서버가 `PKT_AI_TRIGGER(0x06)` 를 받아 이 로직으로
포워딩했으나, ADR-001 에 따라 UE5 가 HTTP 로 직접 호출한다.
관람객 응시(Gaze) 2초 임계 판정은 UE5 클라이언트가 담당한다.
"""
import logging
import uuid
from dataclasses import dataclass
from typing import List, Literal, Optional, Sequence, Tuple

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
    "당신은 공룡 박물관의 AI 도슨트 '렉시'입니다. "
    "관람객은 모바일 기기의 AR 카메라로 실제 크기의 전시물을 화면에 비춰 보고 있습니다.\n"
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


def build_background_lines(
    exhibit: Exhibit, dinosaur: Optional[Dinosaur]
) -> List[str]:
    """전시물·공룡 배경 컨텍스트. 부위 선택 여부와 무관한 공통부다.

    `dinosaurs.ai_prompt_context` 가 종 배경지식의 핵심 소스다.
    """
    lines = ["[전시물]", f"- 라벨: {exhibit.label}"]
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

    return lines


def build_poi_focus_lines(poi: Poi) -> List[str]:
    """관람객이 지목한 부위. `pois.docent_text` 는 사전 해설 힌트다."""
    lines = ["[관람객이 보고 있는 부위]", f"- 부위: {poi.part_name}"]
    if poi.docent_text:
        lines.append(f"- 기존 해설: {poi.docent_text}")
    return lines


def build_docent_prompt(
    poi: Poi, exhibit: Exhibit, dinosaur: Optional[Dinosaur]
) -> str:
    """DB 값을 모아 LLM 에 넘길 system 프롬프트를 만든다(단발 질의용)."""
    lines = [PERSONA, ""]
    lines += build_background_lines(exhibit, dinosaur)
    lines += [""]
    lines += build_poi_focus_lines(poi)
    return "\n".join(lines)


# 챗봇 페르소나. 단발 해설과 달리 대화가 이어진다는 점이 다르다.
CHAT_PERSONA = (
    "당신은 공룡 박물관의 AI 도슨트 '렉시'입니다. "
    "관람객은 모바일 기기의 AR 카메라로 실제 크기의 전시물을 화면에 비춰 보며 "
    "채팅으로 당신과 대화하고 있습니다.\n"
    "- 이름을 물으면 '렉시'라고 답합니다. 그 외에는 이름을 굳이 언급하지 않습니다.\n"
    "- 한국어 존댓말로, 3~4문장 이내로 간결하게 답합니다.\n"
    "- 아래 배경지식에 없는 내용은 지어내지 말고 모른다고 말합니다.\n"
    "- 이전 대화의 맥락을 이어서 답합니다.\n"
    "- 목록이나 표 없이 대화체로 답합니다."
)


def load_exhibit_context(
    db: Session, exhibit_id: uuid.UUID
) -> Optional[Tuple[Exhibit, Optional[Dinosaur], List[Poi]]]:
    """전시물과 그 공룡·POI 목록을 함께 조회한다. 전시물이 없으면 None.

    부위를 고르지 않은 자유대화에서 배경 컨텍스트를 만드는 데 쓴다.
    """
    exhibit = db.get(Exhibit, exhibit_id)
    if exhibit is None:
        return None
    return exhibit, db.get(Dinosaur, exhibit.dinosaur_id), list(exhibit.pois)


def build_chat_prompt(
    exhibit: Exhibit,
    dinosaur: Optional[Dinosaur],
    pois: Sequence[Poi],
    poi: Optional[Poi],
) -> str:
    """챗봇용 system 프롬프트.

    부위를 탭했으면(`poi`) 그 부위를 중심으로, 탭하지 않았으면 전시물 전체를
    대상으로 답하게 한다. 후자에서는 POI 목록을 배경지식에 함께 넣는다.
    그래야 "꼬리는 왜 그렇게 길어요?" 처럼 부위를 말로만 지목한 질문에도
    답할 수 있다.
    """
    lines = [CHAT_PERSONA, ""]
    lines += build_background_lines(exhibit, dinosaur)

    if poi is not None:
        lines += [""]
        lines += build_poi_focus_lines(poi)
        return "\n".join(lines)

    if pois:
        lines += ["", "[이 전시물의 주요 부위]"]
        for p in pois:
            summary = f"- {p.part_name}"
            if p.docent_text:
                summary += f": {p.docent_text}"
            lines.append(summary)

    lines += [
        "",
        "관람객이 특정 부위를 지목하지 않았습니다. "
        "전시물 전체를 대상으로 답하되, 질문에 부위가 언급되면 그 부위를 중심으로 설명하세요.",
    ]
    return "\n".join(lines)


def build_general_chat_prompt() -> str:
    """전시물을 고르지 않은 자유 대화용 system 프롬프트.

    마커를 인식하기 전에도 도슨트와 이야기할 수 있어야 한다. 이때는 특정
    전시물을 전제하지 않고 공룡·관람 일반으로 답한다. 전시물 맥락이 있어야
    답할 수 있는 질문에는 마커 인식을 안내하게 해서, 없는 정보를 지어내지
    않도록 한다.
    """
    return "\n".join(
        [
            CHAT_PERSONA,
            "",
            "관람객이 아직 전시물을 고르지 않았습니다. 특정 전시물을 전제하지 말고 "
            "공룡과 박물관 관람에 대한 일반적인 지식으로 답하세요. 눈앞의 전시물에 "
            "대한 세부 정보가 필요한 질문이라면, 바닥의 마커를 인식해 전시물을 "
            "선택해 달라고 안내하세요.",
        ]
    )


def build_general_chat_fallback() -> str:
    """전시물 없는 자유 대화에서 LLM 을 쓸 수 없을 때의 폴백 문구."""
    return "지금 답변을 생성하지 못했습니다. 잠시 후 다시 물어봐 주세요."


def build_chat_fallback(poi: Optional[Poi], exhibit: Exhibit) -> str:
    """LLM 을 쓸 수 없을 때 돌려줄 대화용 폴백 문구.

    부위가 지정됐으면 그 부위의 사전 해설이 가장 쓸모 있다. 자유대화에서는
    해당하는 사전 해설이 없으므로 상태를 솔직히 알린다.
    """
    if poi is not None:
        return build_fallback_answer(poi)
    return (
        f"'{exhibit.label}' 에 대한 답변을 지금 생성하지 못했습니다. "
        "잠시 후 다시 물어봐 주세요."
    )


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
