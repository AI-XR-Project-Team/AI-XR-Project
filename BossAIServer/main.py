from fastapi import FastAPI
import asyncio
import random
from ollama import AsyncClient

app = FastAPI(title="Mech-Raid Boss AI Server")

@app.get("/api/v1/boss/taunt")
async def get_boss_taunt(phase: int, target_player: int):
    # 실제 로컬 AI 흉내내는 Sleep
    prompt = (
        f"당신은 기계 군단의 지배자 '오메가'입니다. "
        f"현재 전투 페이즈: {phase}단계. "
        f"공격 대상 플레이어 ID: {target_player}. "
        f"당신의 말투는 차갑고 오만하며, 인간의 나약함을 비웃는 기계적인 어투입니다. "
        f"절대 괄호나 [ ] 같은 기호를 쓰지 말고, 오직 도발 대사만 출력하세요. "
        f"대사는 반드시 한국어이며 30자 이내로 짧고 강렬하게 작성하십시오."
    )
    try:
        response = await AsyncClient().chat(model='qwen2.5:7b', messages=[
            {'role': 'user', 'content': prompt}
        ])
        ai_text = response['message']['content']
    except Exception as e:
        ai_text = "시스템 오류... 정화를 강제 집행한다."
        print(f"Ollama 연동 에러: {e}")

    return{
        "status"            : "success",
        "phase"             : phase,
        "targer_player_id"  : target_player,
        "behavior"          :{
            "text" : f"[{target_player}번 개체 타겟팅] {ai_text}",
            "audio_cur_name" : f"A_Boss_Voice_P{phase}_{random.randint(1, 3)}"
        }
    }