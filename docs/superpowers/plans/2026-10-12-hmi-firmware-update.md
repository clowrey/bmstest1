# BMS Firmware Update over the HMI Serial Link: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the ESP32-P4 HMI flash a new controller image into the RP2354B over the existing HMI serial bus. That removes the need for a USB cable or the Raspberry Pi, and lets a battery's controller and HMI be updated together with one HMI OTA.

**Architecture:**
- **Two slots on the RP2354B.** Its flash gets an RP2350 partition table with A/B image slots. The bootrom picks the slot, and "try before you buy" (TBYB) rolls back automatically.
- **The running BMS firmware writes the update.** It receives the new image over the HMI serial protocol (new message types 0x10–0x13), writes it into the inactive slot, and checks a SHA-256. It then reboots into the new slot with a flash-update reboot.
- **The new image confirms itself.** It "buys" itself once it has initialised and the HMI link works. If it never does, the bootrom goes back to the old slot.
- **The HMI carries the matching image.** Each HMI build includes the controller image for its battery (ck0: BLUETESLA/REV1, ck1: LFP1/REV0), so the screen's event table and the controller always match. The HMI sends the image only when the user asks, from the settings page.

**No custom bootloader:** the RP2350 bootrom already does partition selection, address translation, TBYB and rollback.

**Tech stack:** pico-sdk 2.2.0 (`pico_bootrom`, `pico_sha256`, `hardware_flash`, `pico_flash`), picotool 2.1+, ESPHome external component `hmi_bms` (C++ / Python codegen).

---

## How hard is it?

**Moderate: about 1–1.5 weeks including bench testing.** No new hardware is needed: the existing TX/RX pair (Pico GPIO38/39, 460800 baud, `0xFF`/length/CRC16 framing) is enough. The hard parts are not the transfer itself:

1. **One-time migration to a partition table.** Every controller needs one last flash over USB: ck0 through the Pi with picotool, ck1 by hand. This moves the app from flash offset 0 into slot A.
2. **XIP address translation.** When the app runs from a partition, `XIP_BASE + offset` is translated into that partition. [nvm.c](../../../bms/drivers/chip/nvm.c) reads the LittleFS/NVM area at `XIP_BASE + 0x1E0000`. Once the app runs from a slot, that read lands somewhere else, so settings appear blank.
   - **Fix:** read through the untranslated XIP alias (`XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE`). Do this before the first partitioned build ships, or settings will look reset.
3. **Safety gating.** Flash erase and program run with interrupts off and core 1 parked (`flash_safe_execute`). For the length of each operation that stalls can2040 (PIO CAN), the ADC sampling and the contactor state machine. Updates must therefore be refused unless the contactors are open and the system is INACTIVE.
4. **Supervisor reaction.** The Supervisor MCU's firmware is not in this repo. A controller reboot already happens on a watchdog reset, but check on the desk unit that the Supervisor does nothing drastic during the ~2 s reboot.

## Options considered

| Option | Verdict |
|---|---|
| **A/B partitions + TBYB, written by the running app (this plan)** | Chosen. Power loss at any point leaves the old image bootable, a bad image rolls back by itself, and no bootloader needs maintaining. |
| Staging area + RAM-resident copy over slot 0 | Simpler build, but a power cut during the copy bricks the controller until it is reflashed over USB. No automatic rollback. |
| RP2350 bootrom UART boot | Needs boot-strap pins driven by the P4 and loads into SRAM only. Not wired. |
| P4 as USB host speaking PICOBOOT (what picotool does) | Would work, but the P4's USB is not wired to the RP2354B's USB. |
| The P4 toggles RUN/BOOTSEL | Not wired as far as the code shows (only TX/RX). Not needed for this plan. |

## Flash layout (2 MB RP2354B)

The NVM/LittleFS area stays exactly where it is, so settings and the event log survive the migration.

| Offset | Size | Use |
|---|---|---|
| `0x000000` | 8 KB | Partition table (bootrom) |
| `0x002000` | 768 KB | Slot A (image, partition 0) |
| `0x0C2000` | 768 KB | Slot B (image, partition 1, linked as A's partner) |
| `0x182000` | 376 KB | Spare |
| `0x1E0000` | 128 KB | NVM / LittleFS (`NVM_FLASH_OFFSET`, unchanged). Declare it as a data partition, or leave it unpartitioned. |

The current image is ~150 KB (`build-bluetesla-rev1/bms.bin`), so 768 KB slots leave plenty of room. `PICO_FLASH_SIZE_BYTES` (1.5 MB) stays as the link-time guard. Add a CMake check that the image fits in a slot.

## Transfer protocol (new HMI message types)

The existing frame is `0xFF, len-1, payload…, CRC16` with a payload of at most 255 bytes. All new messages carry the BMS address in byte 1, like the others. Integers are little-endian.

| Msg | Dir | Payload | Reply |
|---|---|---|---|
| `0x10` FW_BEGIN | HMI→BMS | addr, size u32, sha256[32], fw_hash u32, profile u8, board u8 | `0x90`: status u8, max_chunk u8, slot_offset u32 |
| `0x11` FW_DATA | HMI→BMS | addr, offset u32, data[≤192] | `0x91`: status u8, next_offset u32 |
| `0x12` FW_END | HMI→BMS | addr | `0x92`: status u8, sha256[32] as computed over the written slot |
| `0x13` FW_ACTIVATE | HMI→BMS | addr | `0x93`: status u8, then a reboot after ~100 ms |
| `0x14` FW_ABORT | HMI→BMS | addr | `0x94`: status u8 |

**Status codes:**
- 0 = OK
- 1 = REFUSED_STATE (contactors not open or system not INACTIVE)
- 2 = REFUSED_TARGET (profile or board mismatch)
- 3 = TOO_BIG
- 4 = BAD_OFFSET
- 5 = FLASH_ERROR
- 6 = HASH_MISMATCH
- 7 = NOT_PARTITIONED (still on the old layout)
- 8 = BUSY

**Transfer rules:**
- **Stop-and-wait:** the HMI sends the next chunk only after `next_offset` comes back. A repeated or out-of-order chunk is answered with the expected offset, so retries are idempotent.
- **Flash writes:** the BMS fills a 4 KB RAM sector buffer and writes it as one erase plus 16 page programs. Each runs in its own `flash_safe_execute` with a timeout, the same as NVM writes, so no single stall exceeds ~100 ms and the 5 s watchdog stays fed.
- **Timeout:** with no traffic for 30 s the session aborts, and the inactive slot is simply left invalid.
- **Throughput:** about 800 chunks for 150 KB. At ~4.5 ms on the wire plus the loop and reply time, expect **~20–40 s** per update.

## Identity and versioning

The controller has no version string today. Add one: it lets the HMI decide whether an update is needed, and lets the BMS refuse an image built for the wrong battery.

- New HMI registers:
  - `HMI_REG_FW_HASH` (63, uint32): git short hash at build time.
  - `HMI_REG_FW_BUILD_TIME` (64, uint32): unix time at build.
  - `HMI_REG_FW_PROFILE_BOARD` (65, uint16): `BMS_PROFILE << 8 | BMS_BOARD`.
  - `HMI_REG_FW_SLOT` (66, uint8): the running partition, and whether the image is still on trial.
- CMake generates `fw_info.h`, and also writes a sidecar `bms.fwinfo.json` next to `bms.bin`. The HMI build reads that JSON.

---

## Task 1: Version / identity registers (Pico + HMI)

**Files:**
- Modify: `CMakeLists.txt` (generate `fw_info.h` from `git rev-parse --short HEAD`, the build time, `BMS_PROFILE` and `BMS_BOARD`; write `bms.fwinfo.json`)
- Modify: `bms/protocols/hmi_serial/hmi_serial.h` (registers 63–66)
- Modify: `bms/protocols/hmi_serial/hmi_serial.c` (availability + append)
- Modify (HMI repo): `components/hmi_bms/{hmi_bms.h,hmi_bms.cpp,sensor.py,text_sensor.py}`, `bms_core.yaml` (a "BMS Firmware" text sensor such as `a1b2c3d BLUETESLA/REV1 2026-10-12`)

- [ ] Step 1: Generate `fw_info.h` at configure/build time (re-run on every build so the hash stays current).
- [ ] Step 2: Serve registers 63–66. Old HMIs simply never ask for them.
- [ ] Step 3: Decode them in `hmi_bms` and publish a text sensor.
- [ ] Step 4: Host test: the register encoding round-trips (in `tests/`).

This ships on its own, under the current layout, and answers "what is running?" straight away.

## Task 2: Make NVM reads translation-safe

**Files:** Modify `bms/drivers/chip/nvm.c` (`FLASH_MMAP_ADDR` and every `memcpy` from `XIP_BASE + …`).

- [ ] Step 1: Read through the untranslated XIP alias (`XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + NVM_FLASH_OFFSET`). Check the macro name in pico-sdk 2.2.0 `hardware/regs/addressmap.h`.
- [ ] Step 2: Flash it on the desk board using the current, unpartitioned layout. Settings and the log must read back identically.

Ship this before Task 3. It is harmless on the old layout and required on the new one.

## Task 3: Partition table and slot-aware build

**Files:**
- Create: `bms/config/partition_table.json` (the layout above; A and B as a linked pair, both marked as images, plus an NVM data partition or a reserved gap)
- Modify: `CMakeLists.txt`. Embed the table for the migration image (`pico_embed_pt_in_binary`), and produce two outputs:
  - `bms.uf2`: for USB install into slot A.
  - `bms.bin`: the plain image for OTA.

  Mark the image for TBYB when sealing (picotool `seal` option, or the SDK's image-def settings). **Follow the RP2350 OTA example in pico-examples (`pico_w/wifi/ota_update`) for the exact CMake calls in SDK 2.2.0.**
- Create: `tools/migrate_to_partitions.md`. One-off USB procedure for ck0 through the Pi: `picotool partition create`/`load`, then load into slot A with `--ser 559BDEC617328C18`, never the Supervisor's serial. Verify with `picotool partition info`.

- [ ] Step 1: Write the table JSON, build, and inspect it with `picotool partition info`.
- [ ] Step 2: Desk board: migrate. Check that it boots from slot A, settings are intact (Task 2), and HMI comms work.
- [ ] Step 3: Desk board: load a second build into slot B with picotool and reboot into it. Check both slots boot.

## Task 4: Firmware receiver in the BMS

**Files:**
- Create: `bms/protocols/hmi_serial/hmi_fw_update.{c,h}`. Session state, sector buffer, flash writer, SHA-256 (`pico_sha256`, hardware-accelerated), and the reboot.
- Modify: `bms/protocols/hmi_serial/hmi_serial.c`. Dispatch messages 0x10–0x14, and send the replies from `hmi_fw_update`.
- Modify: `CMakeLists.txt`. Link `pico_sha256` and `pico_bootrom`.

- [ ] Step 1: **FW_BEGIN checks.** Refuse unless all of these hold:
  - The system state is INACTIVE and the contactor state is OPEN.
  - `profile`/`board` equal this build's.
  - Size ≤ slot size.
  - Running from a partition. Find the inactive slot with `rom_get_partition_table_info` / `rom_pick_ab_partition`.
- [ ] Step 2: **While a session is open:**
  - Hold the contactor request at OPEN, and refuse system requests (Run, Calibrate…).
  - Raise a new `INFO_FW_UPDATE_IN_PROGRESS` event. Append it at the end of the event table, so older HMIs keep their indices.
- [ ] Step 3: **FW_DATA:**
  - Copy into the sector buffer. When a sector is full, erase it and program 16 pages, each in its own `flash_safe_execute`.
  - Keep a running SHA-256.
  - A duplicate offset is a no-op (answer `next_offset`); a gap is answered with BAD_OFFSET.
- [ ] Step 4: **FW_END:**
  - Flush the partial sector, padding it with 0xFF.
  - Re-hash the slot contents read back through the untranslated XIP alias, and compare with the expected SHA.
  - Reply with the computed hash.
- [ ] Step 5: **FW_ACTIVATE:**
  - Only after a successful FW_END.
  - Save NVM (`nvm_schedule_save_persistent_fast` already exists), wait for it to finish, then reply.
  - Then `rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, 100, slot_flash_addr, 0)`.
- [ ] Step 6: **Session timeout and FW_ABORT:** drop the session and clear the event. The inactive slot stays as written, but it is never activated.
- [ ] Step 7: Host tests (CMocka, flash layer mocked): the chunk sequencing, duplicates, gaps, the size limit, the state refusal and the hash mismatch.

## Task 5: Commit ("buy") logic in the BMS

**Files:** Modify `bms/app/state_machines/system.c` (or `init.c`) and `hmi_fw_update.c`.

- [ ] Step 1: At boot, find out whether this image is on trial: the boot type is a flash-update boot, or the image is TBYB-flagged and not yet bought. Expose this in `HMI_REG_FW_SLOT`.
- [ ] Step 2: Call `rom_explicit_buy()` only once all of these hold:
  - The system SM has left INITIALIZING successfully (it already waits 10 s for the Supervisor).
  - The HMI has made at least one valid request.
  - The BMBs and the ADCs report.

  This must happen before the bootrom's TBYB watchdog runs out (~16.7 s). Check how that interacts with the existing `watchdog_enable(5000)` in `drivers/chip/watchdog.c`. It may need to start later, or be fed during the buy wait.
- [ ] Step 3: Bench test with a deliberately broken image (e.g. one that hangs in `bms_init`). It must reboot back into the previous slot by itself, and the HMI must report the old hash again.

## Task 6: HMI side, bundling and sender

**Files (CellKeeper-BMS-LCD):**
- Modify: `components/hmi_bms/__init__.py`. New optional config key `bms_firmware: <path to bms.bin>`.
  - At codegen, read the file and its `bms.fwinfo.json`.
  - Emit a `static const uint8_t[]` in flash, plus the size, SHA-256, hash, profile and board.
  - The P4 app partition is ~7.75 MB, so a 150 KB image is fine.
- Modify: `components/hmi_bms/hmi_bms.{h,cpp}`. An update state machine inside `loop()`:
  - **IDLE → BEGIN → DATA → END → ACTIVATE → VERIFY.**
  - Normal register polling pauses while a session runs.
  - Each step retries up to 3× on timeout (~300 ms) and aborts on any refusal.
  - **VERIFY** waits up to 60 s for the BMS to come back, then reads `HMI_REG_FW_HASH` and `HMI_REG_FW_SLOT`.
- Modify: `components/hmi_bms/{button.py,text_sensor.py}`.
  - Button **"Update BMS Firmware"**, entity_category config. It works only while Settings Unlock is on and the bundled image's hash differs from the running one.
  - Text sensor **"BMS Firmware Update"** showing `up to date` / `update available a1b2c3d` / `updating 42 %` / `waiting for reboot` / `done a1b2c3d` / `failed: REFUSED_STATE`, and so on.
- Modify: `guition-jc1060p470c-esp32-p4.yaml` (ck0) and `shed-guition-jc1060p470c-esp32-p4.yaml` (ck1): `bms_firmware:` pointing at `../bmstest1/build-bluetesla-rev1/bms.bin` and `../bmstest1/build-lfp1-rev0/bms.bin` respectively (or at a copied-in `firmware/` directory, so that HMI builds are reproducible).
- Modify: `bms_core.yaml`, `bms_ui_scripts.yaml`: show the progress on the screen as well, and put the button on the System page of `HA-dashboard.yaml`.

- [ ] Step 1: Embed the image and its metadata at codegen. Fail the build if the profile or board doesn't match a per-device `bms_profile`/`bms_board` substitution, so ck1's screen can never carry ck0's image.
- [ ] Step 2: State machine plus the button and text sensor.
- [ ] Step 3: Never start by itself. Don't start, or abort, if the contactor state is not OPEN; the BMS checks this too.

## Task 7: Bench test matrix (desk board, `build-desk-rev1`)

- [ ] Normal update: A→B, then B→A. Check the hash, the settings and the event log afterwards.
- [ ] Power cut during DATA, during END and during ACTIVATE. The old image must still boot every time.
- [ ] Bad image (never buys) → automatic rollback.
- [ ] Wrong profile or board → REFUSED_TARGET. Contactors closed → REFUSED_STATE.
- [ ] Watch the Supervisor's behaviour through the reboot (scope the contactor drive lines; no unexpected closure).
- [ ] Two BMS addresses on the bus: only the target responds.
- [ ] Timing: total update time and the longest single flash stall (log both).

## Rollout

1. Tasks 1 and 2 ship as a normal USB/Pi update, with no layout change. ck0 gets them through the Pi.
2. Task 3 migration: ck0 through the Pi (controller serial `559BDEC617328C18` only), ck1 by hand at the same visit that updates its HMI.
3. From then on, update ck0 and ck1 by OTA-ing the HMI and pressing "Update BMS Firmware" with the battery isolated (contactors open). Keep the Pi/USB path as the recovery route.

## Open questions

- Does anything on the board let the P4 reset the RP2354B (RUN pin)? Not required for this plan, but it would allow a forced recovery.
- What does the Supervisor do when the controller stops talking for ~2–15 s?
- Do ck1's Rev0 board and stacked flash really match the 2 MB layout? Check with `picotool info -a` on ck1 before its migration.
