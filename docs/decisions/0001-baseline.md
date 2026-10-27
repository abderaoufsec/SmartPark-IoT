# Decision record 0001 — V1 baseline & repository foundation (M0)

**Status:** Accepted (M0) · **Date:** 2026-10-27 · **Deciders:** SmartPark-IoT
maintainers (abderaoufsec) with original author attribution preserved

## Context

The academic prototype
[MehdiKoceir/Parking-intelligent-](https://github.com/MehdiKoceir/Parking-intelligent-)]
is a single `.ino` file, a PDF report, a duplicated text paste and a README
with claims that do not match the repository. We want a professional,
offline-first IoT platform (ESP32 → MQTT → FastAPI/PostgreSQL → SvelteKit,
later Flutter) built by two developers — without destroying the original work
or over-claiming.

## Decisions

1. **Fork and modernize; never overwrite V1 (ADR-001).** The original sketch
   and report are archived byte-for-byte under `legacy/v1/` with SHA-256
   checksums and provenance (`legacy/v1/README.md`). Future work happens in
   `firmware/esp32/`.

2. **Monorepo** for firmware, backend, frontend, simulation, infra and docs
   (blueprint target structure). Rationale: one atomic change story for a
   two-person team, single CI pipeline, no cross-repo version skew. Planned
   directories are **not** created empty — no placeholder code.

3. **Local-first architecture (ADR-002…006).** ESP32 owns safety and sensing;
   network loss never affects local operation; occupancy has an explicit
   `UNKNOWN` state; events are idempotent. (See
   [`docs/architecture/overview.md`](../architecture/overview.md).)

4. **Work on `main` for M0 only**, per maintainer instruction (no extra
   branches during modernization); blueprint branch rules (develop + feature
   branches, review gates) activate from M1.

5. **Reproducible baseline build.** The V1 logic is built with PlatformIO
   using fully pinned versions (`espressif32@6.13.0` = Arduino-ESP32 2.x era,
   `ESP32Servo@1.2.1`, `LiquidCrystal_I2C@1.1.4`) rather than latest, because
   7.x/8.x platforms ship Arduino-ESP32 3.x with breaking changes. Migration
   to newer cores is deliberate M1 work, not an accident of floating versions.

6. **Only mechanical changes to the source for M0:** `src/main.cpp` is the
   archived sketch plus `#include <Arduino.h>` (required to compile a `.cpp`
   outside the Arduino IDE) and a final newline. Behavior is untouched; the
   archived `.ino` remains pristine.

7. **Duplicate removal with evidence.** `pasted_content_19.txt` was deleted
   only after proving SHA-256 equality with `parking_intelligent.ino`
   (`d5cedf92…0c71ebd`); it stays recoverable from git history.

8. **License: MIT, decided explicitly.** The upstream README displayed an MIT
   badge without any `LICENSE` file. During M0 review (2026-10-27) the
   maintainer chose MIT from presented options; `LICENSE` was added crediting
   Mehdi Koceir, abderaoufsec and contributors. Confirming the choice with the
   original author before the first release remains a tracked follow-up.

## Baseline identifiers

| Item | Value |
|---|---|
| Upstream repo | `https://github.com/MehdiKoceir/Parking-intelligent-` |
| Baseline commit (upstream `main` = this fork pre-M0 `main`) | `d566ec4e7529208aba63a7ea4ec09bb5d546878c` |
| `parking_intelligent.ino` SHA-256 | `d5cedf92d61ef4af1fd34454ef9987b8f6532c7ee9674aaeccc23b29a0c71ebd` |
| Report PDF SHA-256 | `ab1913419dc2ae9496bdc7b98e97e919d52f3dbd87f8ce2075c50f4bc3849e29` |
| Blueprint | `docs/SMARTPARK_V2_COMPLETE_PROJECT_BLUEPRINT.md` (spec, not evidence) |

## Consequences

- Positive: reproducible builds, honest documentation, immutable history,
  reviewable M1 starting point.
- Negative / deferred: the baseline compiles with the *known-buggy* V1 logic
  (documented in [`docs/hardware/v1-audit.md`](../hardware/v1-audit.md)) —
  this is intentional; M0 must not mix refactoring with restructuring.
- Follow-ups deferred on purpose: annotated tag `v1.0.0-original` on the
  baseline commit, collaborator invitation, GitHub project board, license
  decision — all require the maintainers' review of this M0 work first.
