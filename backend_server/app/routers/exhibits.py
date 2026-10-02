import uuid

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.core.db import get_db
from app.models.exhibit import Exhibit
from app.schemas.exhibit import ExhibitRead

router = APIRouter()


@router.get("/exhibits/{exhibit_id}", response_model=ExhibitRead)
def get_exhibit(exhibit_id: uuid.UUID, db: Session = Depends(get_db)):
    """전시물 1개 + 소속 POI 목록. 없으면 404."""
    exhibit = db.get(Exhibit, exhibit_id)
    if exhibit is None:
        raise HTTPException(status_code=404, detail="exhibit not found")
    return exhibit
