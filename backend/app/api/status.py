"""SmartPark-IoT backend status API (M3).

GET /status/{device_id} — current occupancy and device health.
"""
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.database import get_db
from app.models import Device, OccupancySnapshot
from app.schemas import DeviceOut, OccupancyOut

router = APIRouter(tags=["status"])


@router.get("/status/{device_id}", response_model=DeviceOut)
def device_status(device_id: str, db: Session = Depends(get_db)):
    """Return device info and online status."""
    device = db.query(Device).filter_by(device_id=device_id).first()
    if device is None:
        raise HTTPException(status_code=404, detail="device not found")
    return device


@router.get("/occupancy/{device_id}", response_model=OccupancyOut)
def device_occupancy(device_id: str, db: Session = Depends(get_db)):
    """Return latest occupancy snapshot for a device."""
    device = db.query(Device).filter_by(device_id=device_id).first()
    if device is None:
        raise HTTPException(status_code=404, detail="device not found")

    snapshot = (
        db.query(OccupancySnapshot)
        .filter_by(device_id=device_id)
        .order_by(OccupancySnapshot.recorded_at.desc())
        .first()
    )
    if snapshot is None:
        return OccupancyOut(
            device_id=device_id, count=0, free_spaces=6, recorded_at=device.first_seen
        )
    return snapshot


@router.get("/health")
def health():
    """Liveness probe."""
    return {"status": "ok"}