"""SmartPark-IoT backend MQTT consumer (M3).

Connects to the Mosquitto broker, subscribes to device event topics, and
ingests each event via the /events endpoint logic. Idempotent ingest by
(device_id, event_id) prevents duplicates on at-least-once MQTT delivery
(ADR-005, NFR-003).
"""
import json
import threading
import time

import paho.mqtt.client as mqtt

from app.config import get_settings
from app.database import SessionLocal, engine
from app.models import Base
from app.api.events import ingest_event
from app.schemas import EventCreate

settings = get_settings()

# Ensure tables exist on import (simple bootstrap; Alembic migrations are
# the canonical path for production deployments).
Base.metadata.create_all(bind=engine)

TOPIC_EVENTS = f"{settings.mqtt_topic_prefix}/+/+/events"


def _on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        client.subscribe(TOPIC_EVENTS)
        print(f"[mqtt] subscribed to {TOPIC_EVENTS}")
    else:
        print(f"[mqtt] connect failed rc={rc}")


def _on_message(client, userdata, msg: mqtt.MQTTMessage):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
        event = EventCreate(
            event_id=payload.get("q", ""),  # seq used as durable event id
            device_id=payload.get("dev", "unknown"),
            type=payload.get("t", "UNKNOWN_EVENT"),
            sensor_id=payload.get("s"),
            value=payload.get("v"),
            at_ms=payload.get("a"),
            seq=payload.get("q"),
        )
        db = SessionLocal()
        try:
            ingest_event(event, db)
        finally:
            db.close()
    except Exception as exc:  # noqa: BLE001 — never kill the consumer loop
        print(f"[mqtt] ingest error: {exc}")


def start_consumer(daemon: bool = True) -> threading.Thread:
    """Start the MQTT consumer in a background thread."""
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(settings.mqtt_username, settings.mqtt_password)
    client.on_connect = _on_connect
    client.on_message = _on_message

    client.connect(settings.mqtt_host, settings.mqtt_port, keepalive=60)
    client.loop_start()

    # Wait briefly for connection
    for _ in range(50):
        if client.is_connected():
            break
        time.sleep(0.1)

    thread = threading.Thread(target=client.loop_forever, daemon=daemon, name="mqtt-consumer")
    thread.start()
    print("[mqtt] consumer started")
    return thread