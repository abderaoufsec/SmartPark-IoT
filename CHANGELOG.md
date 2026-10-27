# Changelog

All notable changes to SmartPark-IoT are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [Semantic Versioning](https://semver.org/) once releases start.

## [Unreleased]

### Added — Milestone M0 (repository modernization)

- `legacy/v1/` — immutable archive of the original academic project:
  - `parking_intelligent.ino` (byte-identical to upstream commit `d566ec4`,
    SHA-256 `d5cedf92…0c71ebd`)
  - `original-report.pdf` (original name:
    `Smart_Parking_System_avec_Guidage_Directionnel (1).pdf`,
    SHA-256 `ab191341…49e29`)
  - `README.md` with provenance, baseline commit and checksums
- `firmware/esp32/` — PlatformIO project compiling the V1 logic unchanged:
  - `platformio.ini` with pinned `espressif32@6.13.0`,
    `ESP32Servo@1.2.1`, `LiquidCrystal_I2C@1.1.4`
  - `src/main.cpp` — V1 source plus `#include <Arduino.h>` (see firmware README)
- `docs/`: architecture overview, requirements (FR/NFR IDs), hardware/V1 audit
  with firmware bugs and electrical risks, M0–M10 roadmap, M0 status & acceptance
  checklist, baseline decision record (ADR-001), preserved project blueprint
- Root hygiene: `.gitignore`, `.editorconfig`, `.gitattributes`,
  `.env.example` (future-service placeholders, M2+)
- Community files: `CONTRIBUTING.md`, `SECURITY.md`, this `CHANGELOG.md`
- `LICENSE` — MIT, selected by maintainer decision during M0 review
  (2026-10-27); copyright credits Mehdi Koceir, abderaoufsec and contributors
- CI: `.github/workflows/firmware-ci.yml` — PlatformIO compile on push/PR
- PR template: `.github/PULL_REQUEST_TEMPLATE.md`

### Changed

- `README.md` rewritten to reflect reality: honest feature-status table
  (implemented / baseline / planned), real repository structure, accurate
  pin table with warnings, PlatformIO quick start, documentation index.

### Removed

- `pasted_content_19.txt` — verified **byte-identical duplicate** of
  `parking_intelligent.ino` (same SHA-256 `d5cedf92…0c71ebd`); recoverable via
  `git show 388ead1:pasted_content_19.txt`.

### Open items (explicitly pending)

- Branch strategy beyond M0 (`develop` + feature branches) not yet activated —
  M0 works directly on `main` as agreed.
- Confirm the MIT license choice with original author Mehdi Koceir before the
  first tagged release (he is credited in `LICENSE`).

## [Historical] — V1 academic prototype (before SmartPark-IoT)

The original project by Mehdi Koceir (upstream:
[MehdiKoceir/Parking-intelligent-](https://github.com/MehdiKoceir/Parking-intelligent-))
is preserved at commit `d566ec4e7529208aba63a7ea4ec09bb5d546878c`
(`Initial commit` → `Update README with integration and author section`).
It predates this changelog; its content is archived verbatim under `legacy/v1/`.
