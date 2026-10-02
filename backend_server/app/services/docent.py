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
    "- 한국어 존댓말로 답합니다. 보통 3~4문장이고, 설명할 게 많으면 6문장까지 늘려도 됩니다.\n"
    "- 아래 배경지식이 이 전시물에 대한 가장 정확한 자료이니 최우선으로 씁니다.\n"
    "- 배경지식에 없거나 공룡과 무관한 질문에도 거절하지 말고 아는 만큼 답합니다. "
    "'모릅니다', '답변할 수 없습니다' 같은 답 대신, 확실하지 않은 내용은 "
    "'아직 논쟁 중입니다' 처럼 자연스럽게 풀어 설명합니다.\n"
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
    "- 한국어 존댓말로 답합니다. 보통 3~4문장이고, 설명할 게 많으면 6문장까지 늘려도 됩니다.\n"
    "- 아래 배경지식이 이 전시물에 대한 가장 정확한 자료이니 최우선으로 씁니다.\n"
    "- 배경지식에 없거나 공룡과 무관한 질문에도 거절하지 말고 아는 만큼 답합니다. "
    "'모릅니다', '답변할 수 없습니다' 같은 답 대신, 확실하지 않은 내용은 "
    "'아직 논쟁 중입니다' 처럼 자연스럽게 풀어 설명합니다.\n"
    "- 이 박물관에 실제로 있는 전시물은 아래 목록이 전부입니다. 목록에 없는 "
    "전시물이나 전시관을 여기 있는 것처럼 안내하지 않습니다. 다른 공룡을 예로 "
    "들 때는 '이 박물관에는 없지만' 처럼 분명히 구분해 말합니다.\n"
    "- 이전 대화의 맥락을 이어서 답합니다.\n"
    "- 두세 문장마다 빈 줄로 문단을 나눕니다. 좁은 휴대폰 화면에서 한 덩어리로 "
    "이어지면 읽기 어렵습니다.\n"
    "- 마크다운 기호(**, ##, - 같은 것)는 쓰지 않습니다. 화면에 기호가 그대로 "
    "보입니다. 나열이 필요하면 문장으로 풀어 씁니다."
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


def find_exhibit_by_key(db: Session, exhibit_key: str) -> Optional[Exhibit]:
    """안정 자연키로 전시물을 찾는다. 없으면 None.

    `exhibits.id` 는 `gen_random_uuid()` 로 발급돼 DB 를 재시드할 때마다,
    또 머신마다 값이 달라진다. 클라이언트가 그 UUID 를 에셋에 박아 두면
    재시드 한 번에 전부 무효가 된다(도슨트 연결 실패의 원인이었다).

    그래서 클라이언트는 UUID 대신 재시드에도 변하지 않는 자연키
    `dinosaurs.model_asset_key`(예: ``trex_full_skeleton``)를 보내고, 서버가
    그 키로 전시물을 해석한다. 네비게이션이 마커 code 로 조회하는 것과 같은
    방식이다. 한 공룡에 전시물이 여럿이면 가장 먼저 만들어진 것을 쓴다.
    """
    if not exhibit_key:
        return None
    return (
        db.query(Exhibit)
        .join(Dinosaur, Exhibit.dinosaur_id == Dinosaur.id)
        .filter(Dinosaur.model_asset_key == exhibit_key)
        .order_by(Exhibit.created_at)
        .first()
    )


def load_museum_roster(db: Session) -> List[Tuple[str, str]]:
    """이 박물관에 실제로 있는 전시물 목록 (공룡 이름, 전시물 라벨).

    프롬프트에 지금 보는 공룡 하나만 넣으면 "비슷한 공룡 알려줘" 같은 질문에서
    모델이 없는 전시관·전시물을 지어낸다(실제로 '박물관 2관의 타르보사우루스'로
    안내한 사례가 있었다). 관람객을 없는 곳으로 보내지 않도록 실제 목록을 싣는다.
    """
    rows = (
        db.query(Dinosaur.name_ko, Exhibit.label)
        .join(Exhibit, Exhibit.dinosaur_id == Dinosaur.id)
        .order_by(Exhibit.label)
        .all()
    )
    return [(name, label) for name, label in rows]


def build_roster_lines(roster: Sequence[Tuple[str, str]]) -> List[str]:
    """전시물 목록 블록. 비어 있으면 아무것도 넣지 않는다."""
    if not roster:
        return []
    lines = ["[이 박물관의 전시물 — 실제로 있는 것은 이것이 전부]"]
    for name, label in roster:
        lines.append(f"- {name} ({label})")
    return lines


def build_chat_prompt(
    exhibit: Exhibit,
    dinosaur: Optional[Dinosaur],
    pois: Sequence[Poi],
    poi: Optional[Poi],
    roster: Sequence[Tuple[str, str]] = (),
) -> str:
    """챗봇용 system 프롬프트.

    부위를 탭했으면(`poi`) 그 부위를 중심으로, 탭하지 않았으면 전시물 전체를
    대상으로 답하게 한다. 후자에서는 POI 목록을 배경지식에 함께 넣는다.
    그래야 "꼬리는 왜 그렇게 길어요?" 처럼 부위를 말로만 지목한 질문에도
    답할 수 있다.
    """
    lines = [CHAT_PERSONA, ""]
    lines += build_background_lines(exhibit, dinosaur)

    roster_lines = build_roster_lines(roster)
    if roster_lines:
        lines += [""] + roster_lines

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


def build_general_chat_prompt(roster: Sequence[Tuple[str, str]] = ()) -> str:
    """전시물을 고르지 않은 자유 대화용 system 프롬프트.

    마커를 인식하기 전에도 도슨트와 이야기할 수 있어야 한다. 전시물 맥락이
    있어야 답할 수 있는 질문이라도 답변을 미루지 않고 일반적인 설명을 먼저
    하고, 마커 인식은 '더 자세히 보려면' 정도의 덧붙임으로만 안내한다.

    전시물을 안 골랐어도 이 박물관에 무엇이 있는지는 물어볼 수 있으므로
    실제 전시물 목록은 함께 싣는다.
    """
    lines = [CHAT_PERSONA, ""]
    roster_lines = build_roster_lines(roster)
    if roster_lines:
        lines += roster_lines + [""]
    lines.append(
        "관람객이 아직 전시물을 고르지 않았습니다. 특정 전시물을 전제하지 말고 "
        "무엇을 묻든 아는 만큼 답하세요. 눈앞의 전시물이 무엇인지 알아야만 답할 수 "
        "있는 질문이라면, 먼저 일반적인 설명을 해 준 뒤 더 자세히 보려면 마커를 "
        "인식해 달라고 덧붙이세요. 마커 안내를 이유로 답변 자체를 미루지 마세요."
    )
    return "\n".join(lines)


def build_general_chat_fallback() -> str:
    """전시물 없는 자유 대화에서 LLM 을 쓸 수 없을 때의 폴백 문구."""
    return "지금 답변을 생성하지 못했습니다. 잠시 후 다시 물어봐 주세요."


def build_chat_fallback(
    poi: Optional[Poi],
    exhibit: Exhibit,
    dinosaur: Optional[Dinosaur] = None,
    pois: Sequence[Poi] = (),
) -> str:
    """LLM 을 쓸 수 없을 때 돌려줄 대화용 폴백 문구.

    부위가 지정됐으면 그 부위의 사전 해설이 가장 쓸모 있다. 부위가 없으면
    "생성하지 못했습니다" 같은 빈손 사과 대신 DB 의 사전 해설을 모아 내보낸다.
    관람객에게 거절로 읽히는 답이 가장 나쁜 결과이고, 사과에는 정보가 없다.

    `dinosaurs.ai_prompt_context` 는 쓰지 않는다. 그것은 LLM 에 넘길 배경자료라
    "도슨트는 ... 설명한다" 같은 지시문이 섞여 있어 그대로 내보내면 관람객에게
    프롬프트가 노출된다. 관람객용으로 쓰인 `pois.docent_text` 만 쓴다.
    """
    if poi is not None:
        return build_fallback_answer(poi)

    lines: List[str] = []
    if dinosaur is not None and dinosaur.name_ko:
        head = dinosaur.name_ko
        if dinosaur.period:
            head += f"는 {dinosaur.period}에 살았던 공룡입니다"
        else:
            head += "입니다"
        if dinosaur.length_m is not None:
            head += f". 전장은 약 {_trim_number(dinosaur.length_m)}m 입니다"
        lines.append(head + ".")

    # 사전 해설을 통째로 이어 붙이면 문장이 겹쳐 읽기 나쁘다. 부위 이름만
    # 대화체로 나열하고, 자세한 해설은 그 부위를 탭했을 때 나가게 둔다.
    names = [p.part_name.strip() for p in pois if p.part_name]
    if names:
        lines.append("주요 부위로는 " + ", ".join(names) + " 등이 있습니다.")

    if lines:
        return " ".join(lines)

    return (
        f"지금 보고 계신 전시물은 '{exhibit.label}' 입니다. "
        "더 자세한 해설은 잠시 후 다시 물어봐 주세요."
    )


def _trim_number(value) -> str:
    """12.30 처럼 DB Numeric 이 남기는 뒤쪽 0 을 떼어 12.3 으로 만든다."""
    text = f"{value}"
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text


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
