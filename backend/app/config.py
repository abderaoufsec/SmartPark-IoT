"""SmartPark-IoT backend configuration (M3).

Reads from environment variables. Defaults are safe for local development
but MUST be overridden in production (see backend/.env.example).
"""
import os
from functools import lru_cache


class Settings:
    def __init__(self) -> None:
        # ---- PostgreSQL ----
        self.postgres_user = os.getenv("POSTGRES_USER", "smartpark")
        self.postgres_password = os.getenv("POSTGRES_PASSWORD", "change-me-in-production")
        self.postgres_db = os.getenv("POSTGRES_DB", "smartpark")
        self.postgres_host = os.getenv("POSTGRES_HOST", "localhost")
        self.postgres_port = int(os.getenv("POSTGRES_PORT", "5432"))

        # DATABASE_URL takes precedence if set
        self.database_url = os.getenv(
            "DATABASE_URL",
            f"postgresql+psycopg2://{self.postgres_user}:{self.postgres_password}"
            f"@{self.postgres_host}:{self.postgres_port}/{self.postgres_db}",
        )

        # ---- API security ----
        self.api_secret_key = os.getenv("API_SECRET_KEY", "change-me-in-production")
        self.algorithm = os.getenv("ALGORITHM", "HS256")
        self.access_token_expire_minutes = int(
            os.getenv("ACCESS_TOKEN_EXPIRE_MINUTES", "30")
        )

        # ---- MQTT ----
        self.mqtt_host = os.getenv("MQTT_HOST", "localhost")
        self.mqtt_port = int(os.getenv("MQTT_PORT", "1883"))
        self.mqtt_username = os.getenv("MQTT_USERNAME", "")
        self.mqtt_password = os.getenv("MQTT_PASSWORD", "")
        self.mqtt_topic_prefix = os.getenv("MQTT_TOPIC_PREFIX", "smartpark")

        # ---- Facility / device ----
        self.facility_id = os.getenv("SMARTPARK_FACILITY_ID", "fac-demo")
        self.device_id = os.getenv("SMARTPARK_DEVICE_ID", "esp32-001")


@lru_cache
def get_settings() -> Settings:
    return Settings()