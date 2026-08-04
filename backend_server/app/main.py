"""공룡 박물관 AR — FastAPI 엔트리포인트.

기동:  uvicorn app.main:app --reload
문서:  http://localhost:8000/docs  (Swagger UI)
"""
from fastapi import FastAPI

from app.routers import chat, dinosaurs, docent, exhibits, health, maps, navigation

app = FastAPI(
    title="공룡 박물관 AR API",
    version="0.1.0",
    description="Quest 3 익명 관람객용 조회 API (1주차 스켈레톤)",
)

app.include_router(health.router, tags=["health"])
app.include_router(dinosaurs.router, tags=["dinosaurs"])
app.include_router(exhibits.router, tags=["exhibits"])
app.include_router(docent.router, tags=["docent"])
app.include_router(maps.router, tags=["maps"])
app.include_router(navigation.router, tags=["navigation"])
# chat 라우터는 엔드포인트별로 tags 를 직접 지정한다.
app.include_router(chat.router)
