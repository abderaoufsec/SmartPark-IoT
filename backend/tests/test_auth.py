"""SmartPark-IoT backend auth tests (M3)."""
def test_login_issues_token(client):
    resp = client.post("/token", data={"username": "alice", "password": "pw"})
    assert resp.status_code == 200, resp.text
    data = resp.json()
    assert "access_token" in data
    assert data["token_type"] == "bearer"


def test_protected_endpoint_requires_bearer(client):
    resp = client.get("/events/")
    assert resp.status_code == 401, resp.text


def test_invalid_token_rejected(client):
    resp = client.get(
        "/events/",
        headers={"Authorization": "Bearer definitely-not-a-valid-token"},
    )
    assert resp.status_code == 401, resp.text