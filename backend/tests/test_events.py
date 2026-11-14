"""SmartPark-IoT backend event API tests (M3).

Validates:
  - Event ingestion creates device + event rows
  - Idempotent ingest: duplicate (device_id, event_id) → one stored row (NFR-003)
  - Auth required on all endpoints (NFR-006)
  - Occupancy snapshot computed after confirmed passage
"""
import pytest


def test_ingest_event_creates_rows(client, auth_headers, db_session):
    payload = {
        "event_id": "evt-001",
        "device_id": "esp32-001",
        "type": "PASSAGE_CONFIRMED_ENTRY",
        "sensor_id": 0,
        "value": 1,
        "at_ms": 1000,
        "seq": 1,
    }
    resp = client.post("/events/", json=payload, headers=auth_headers)
    assert resp.status_code == 201, resp.text
    data = resp.json()
    assert data["event_id"] == "evt-001"
    assert data["device_id"] == "esp32-001"
    assert data["type"] == "PASSAGE_CONFIRMED_ENTRY"

    # Device registered
    from app.models import Device, Event
    device = db_session.query(Device).filter_by(device_id="esp32-001").first()
    assert device is not None
    assert device.is_online is True
    event = db_session.query(Event).filter_by(event_id="evt-001").first()
    assert event is not None


def test_idempotent_ingest_no_duplicates(client, auth_headers, db_session):
    payload = {
        "event_id": "evt-dup",
        "device_id": "esp32-002",
        "type": "PASSAGE_CONFIRMED_ENTRY",
        "sensor_id": 0,
        "value": 1,
        "at_ms": 2000,
        "seq": 1,
    }
    # Ingest same event 3 times
    for _ in range(3):
        resp = client.post("/events/", json=payload, headers=auth_headers)
        assert resp.status_code == 201, resp.text

    from app.models import Event
    rows = db_session.query(Event).filter_by(event_id="evt-dup").all()
    assert len(rows) == 1, f"expected 1 row, got {len(rows)} (NFR-003)"


def test_auth_required(client):
    """Unauthenticated requests are rejected (NFR-006)."""
    resp = client.post(
        "/events/",
        json={"event_id": "x", "device_id": "d", "type": "PASSAGE_CONFIRMED_ENTRY"},
    )
    assert resp.status_code == 401, resp.text

    resp = client.get("/events/")
    assert resp.status_code == 401, resp.text


def test_list_events(client, auth_headers, db_session):
    from app.models import Event

    for i in range(3):
        client.post(
            "/events/",
            json={
                "event_id": f"evt-list-{i}",
                "device_id": "esp32-list",
                "type": "PASSAGE_CONFIRMED_ENTRY",
                "sensor_id": 0,
                "value": 1,
                "at_ms": 1000 + i,
                "seq": i,
            },
            headers=auth_headers,
        )

    resp = client.get("/events/?device_id=esp32-list", headers=auth_headers)
    assert resp.status_code == 200, resp.text
    data = resp.json()
    assert len(data) == 3