# legacy/v1 — original academic project (immutable archive)

This directory preserves the **original Smart Parking project** exactly as it
existed upstream before the SmartPark-IoT modernization. Treat these files as
read-only history: never edit, reformat or "fix" them. All future development
happens in `firmware/esp32/`.

## Provenance

| | |
|---|---|
| Original repository | <https://github.com/MehdiKoceir/Parking-intelligent-> |
| Original author | [Mehdi Koceir](https://github.com/MehdiKoceir), University of Blida |
| Baseline commit (upstream `main`, identical to this fork's pre-M0 `main`) | `d566ec4e7529208aba63a7ea4ec09bb5d546878c` |
| Archived during | Milestone M0 (repository modernization) |

## Contents and checksums (SHA-256)

| File in this directory | Original path/name in upstream repo | SHA-256 |
|---|---|---|
| `parking_intelligent.ino` | `parking_intelligent.ino` (commit `0756746`) | `d5cedf92d61ef4af1fd34454ef9987b8f6532c7ee9674aaeccc23b29a0c71ebd` |
| `original-report.pdf` | `Smart_Parking_System_avec_Guidage_Directionnel (1).pdf` (commit `388ead1`) | `ab1913419dc2ae9496bdc7b98e97e919d52f3dbd87f8ce2075c50f4bc3849e29` |

- The `.ino` file is byte-identical to the original commit (renamed only by
  path, content untouched; `.gitattributes` forces git to store it without any
  line-ending conversion).
- The PDF keeps its original bytes; only the file *name* was normalized from
  `Smart_Parking_System_avec_Guidage_Directionnel (1).pdf` to
  `original-report.pdf` (browser-download artifact `(1)` removed).
  Restore the original name with
  `git log --follow -- "legacy/v1/original-report.pdf"` if ever needed.

## Removed duplicate (verified before deletion)

The upstream repository also contained **`pasted_content_19.txt`**, added in
commit `388ead1`. Before deleting it in M0, its content was verified as an
**exact byte-for-byte duplicate** of `parking_intelligent.ino`:

```text
SHA-256(parking_intelligent.ino) = d5cedf92d61ef4af1fd34454ef9987b8f6532c7ee9674aaeccc23b29a0c71ebd
SHA-256(pasted_content_19.txt)   = d5cedf92d61ef4af1fd34454ef9987b8f6532c7ee9674aaeccc23b29a0c71ebd
```

Identical hashes → redundant copy removed. It remains recoverable from git
history (`git show 388ead1:pasted_content_19.txt`).

## Build note

The archived sketch targets the Arduino IDE (ESP32 board package +
`LiquidCrystal_I2C` + `ESP32Servo`). The equivalent, behavior-preserving
PlatformIO build lives in `firmware/esp32/` — see that directory's README for
the two mechanical differences (`#include <Arduino.h>` and a final newline).
