from fastapi import FastAPI
import asyncio
import random

app = FastAPI(title="Mech-Raid Boss AI Server")

# AI 생성 전 임시 대사
TAUNUS = [
    "aaaaaaaa",
    "bbbbbbbbbbbbb",
    "cccccccc"
]

@app.get("/api/v1/boss/taunt")
async def get_boss_taunt(phase: int, target_player: int):
    # 실제 로컬 AI 흉내내는 Sleep
    await asyncio.sleep(1.5)

    selected_text = random.choice(TAUNUS)

    return{
        "status"            : "success",
        "phase"             : phase,
        "targer_player_id"  : target_player,
        "behavior"          :{
            "text" : f"[{target_player}번 개체 타겟팅] {selected_text}",
            "audio_cur_name" : f"A_Boss_Voice_P{phase}_{random.randint(1, 3)}"
        }
    }