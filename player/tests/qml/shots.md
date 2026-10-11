# Review renders

| Screen | State | Shot | Rows checked | Date |
|---|---|---|---|---|
| settings | browse | `--size 640x960 --orientation landscape` beside `--size 960x640` | OUTPUT section, header, focus bar | 2026-10-08 |
| home | browse | `--size 640x960 --orientation landscape` beside `--size 960x640` | menu, Quickplay peek | 2026-10-08 |
| nowplaying | browse | `--size 640x960 --orientation landscape` beside `--size 960x640` | art, text column, rings | 2026-10-08 |
| hub | albums | `--size 640x960 --orientation landscape` beside `--size 960x640` | pivot strip, grid captions | 2026-10-08 |
| settings, home, nowplaying | browse | `--size 640x960 --orientation landscape-flipped` | as above, turned 180 | 2026-10-08 |
| settings | `orientation-edit` | `--size 640x960` | ORIENTATION value in accent, PORTRAIT FLIPPED | 2026-10-08 |
| settings | `orientation` | `--size 960x640` | ORIENTATION reads EXTERNAL DISPLAY | 2026-10-08 |
| settings | `orientation` | `--size 640x960`, and with `--orientation landscape` | ORIENTATION reads PORTRAIT, LANDSCAPE | 2026-10-08 |
| settings | `orientation-edit-end` | `--size 640x960` | ORIENTATION draft LANDSCAPE FLIPPED in accent, the longest value | 2026-10-08 |
| settings | `orientation-external` | `--size 640x960` | ORIENTATION reads EXTERNAL DISPLAY on a portrait surface | 2026-10-08 |
| settings | `auto-rotate` | `--size 640x960`, and with `--orientation landscape` | AUTO ROTATE ON under ORIENTATION, ORIENTATION read only PORTRAIT, LANDSCAPE | 2026-10-09 |
| settings | `auto-rotate-unsupported` | `--size 640x960` | AUTO ROTATE reads UNSUPPORTED | 2026-10-09 |
| settings | `auto-rotate-below` | `--size 640x960`, and with `--orientation landscape` | SCALE and SAVER under AUTO ROTATE ON, focus on SAVER | 2026-10-09 |
| settings | `auto-rotate-off` | `--size 640x960` | AUTO ROTATE OFF, ORIENTATION editable above it | 2026-10-09 |
| settings | `auto-rotate-edit` | `--size 640x960` | AUTO ROTATE draft ON in accent | 2026-10-09 |
| settings | browse | `--size 640x960` | AUTO ROTATE reads OFF | 2026-10-09 |
| hub | `pressed` | `--pivot songs` and `--pivot albums`, `--size 640x960` and `960x640` | focus on the first song row or album cell, F3 scale inside the slot, a pressed tile's caption not clipped | pending |
| hub | `touch-scrolled` | `--pivot artists`, `--size 640x960` and `960x640` | focus row fully inside `[CONTENT_TOP, H - ROW_HEIGHT]` | pending |
| album | `pressed` | `--size 640x960` and `960x640` | focused track row at F3 scale | pending |
| queue | `touch-scrolled` | `--size 640x960` and `960x640` | focus row fully inside `[CONTENT_TOP, H - ROW_HEIGHT]` | pending |
| home | `pressed` | `--size 640x960` and `960x640` | focused menu item at F3 scale | pending |
| quickplay | `pressed` | `--size 640x960` and `960x640` | focused tile at F3 scale inside its slot | pending |
| nowplaying | `pressed` | `--size 640x960` and `960x640` | focused ring at F3 scale | pending |
| settings | `touch-edit` | `--size 640x960` and `960x640` | TOUCH draft OFF in accent after AUTO ROTATE | pending |
| settings | `touch` | `--size 640x960` and `960x640` | TOUCH reads ON after AUTO ROTATE, focus on TOUCH | pending |
| eq | `pressed` | `--size 640x960` and `960x640` | renders as browse (EQ rows have no F3) | pending |
| power | `pressed` | `--size 640x960` and `960x640` | renders as browse (dialog items have no F3) | pending |
| settings | browse | `--size 640x960` | BIT PERFECT OFF under BLUETOOTH CODEC, the OUTPUT rows above unchanged | pending |
| settings | `bit-perfect` | `--size 640x960` and `960x640` | BIT PERFECT ON on USB, focus on BIT PERFECT | pending |
| settings | `bit-perfect-edit` | `--size 640x960` and `960x640` | BIT PERFECT draft EXCLUSIVE in accent | pending |
| settings | `bit-perfect-phone` | `--size 640x960` and `960x640` | BIT PERFECT reads EXCLUSIVE (PHONE) | pending |
| settings | `bit-perfect-unavailable` | `--size 640x960` and `960x640` | BIT PERFECT reads EXCLUSIVE (UNAVAILABLE), the longest value, unclipped, clear of the title | pending |
| settings | `bit-perfect-usb-only` | `--size 640x960` and `960x640` | BIT PERFECT reads USB ONLY on the headphones | pending |
| settings | `sample-rate-edit` | `--size 640x960` and `960x640` | SAMPLE RATE draft 352800 in accent on the FiiO rates | pending |
| settings | `sample-rate-unsupported` | `--size 640x960` and `960x640` | SAMPLE RATE reads 96000 (NOT SUPPORTED) on the Apple dongle rate | pending |
| settings | `sample-rate-locked` | `--size 640x960` and `960x640` | focus on EQ reading BYPASSED, SAMPLE RATE above it read only AUTO 96000 | pending |
| eq | browse | `--size 640x960` and `960x640` | BAND rows PK, LS, HS text unchanged, band 2 sub rows | pending |
| eq | `eq-types` | `--size 640x960` and `960x640` | BAND rows OFF, PK, LS, HS, BP, focus on BAND 5, gain free rows with no gain | pending |
| eq | `eq-types-end` | `--size 640x960` and `960x640` | BAND rows LP, HP, AP, `HS 20 KHZ -24.0 DB Q 10.00` unclipped and clear of the title, focus on BAND 10 | pending |
| eq | `eq-open-hs` | `--size 640x960` and `960x640` | BAND 4 sub rows TYPE to Q on screen, TYPE HIGH SHELF focused, GAIN +12.0 DB | pending |
| eq | `eq-type-edit` | `--size 640x960` and `960x640` | BAND 5 sub rows TYPE to Q on screen, TYPE BAND PASS in accent (edit mode), GAIN NOT USED | pending |
| eq | `eq-gain-unused` | `--size 640x960` and `960x640` | BAND 6 sub rows TYPE to Q on screen, TYPE LOW PASS, focus on GAIN NOT USED in the read only value style | pending |
| eq | `eq-open-hp` | `--size 640x960` and `960x640` | BAND 7 sub rows TYPE to Q on screen, TYPE HIGH PASS focused, GAIN NOT USED | pending |
| eq | `eq-open-ap` | `--size 640x960` and `960x640` | BAND 8 sub rows TYPE to Q on screen, TYPE ALL PASS focused, GAIN NOT USED | pending |

`ctest -R screenshot-orientation` turns the `--orientation landscape` settings render back by L and gates it
against the `--size 960x640` render: at most 2% of pixels differ by more than 32 in a channel (0.52% measured
2026-10-08, a wrong turn gives 19%).
