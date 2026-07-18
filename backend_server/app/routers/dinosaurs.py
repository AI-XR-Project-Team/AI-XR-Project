from typing import List

from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from app.core.db import get_db
from app.models.dinosaur import Dinosaur
from app.schemas.dinosaur import DinosaurRead

router = APIRouter()


@router.get("/dinosaurs", response_model=List[DinosaurRead])
def list_dinosaurs(db: Session = Depends(get_db)):
    """등록된 공룡 종 전체 목록."""
    return db.query(Dinosaur).order_by(Dinosaur.created_at).all()
