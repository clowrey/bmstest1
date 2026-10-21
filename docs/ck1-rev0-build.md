# ck1 build: Rev0 PCB only

**This branch is the firmware running on ck1 (the 108s LFP shed pack), which has
a Rev0 PCB. Build it only for Rev0 boards.** Rev1 boards (ck0, the desk bench)
need `-DBMS_BOARD=2` and their own USB options; don't flash a Rev0 build to them.

## What's flashed

ck1's controller and supervisor were flashed on 2026-10-20 at 15:54 from commit
`0530604`, the parent of the commit that added this note. It is these three
branches merged:

| Branch | Commit | Change |
|---|---|---|
| `module-id-reading` | `8b0c547` | Module IDs and pack type, read after the first full cell read |
| `short-cell-reads` | `9bf7a92` | Short cell reads; the supervisor's snooper flag on GPIO5 on Rev0 |
| `usb-log-flush-race` | `61c3e3a` | Log flushed from the main loop (fixes a USB watchdog reset) |

Rebuild it from those branches once they're merged, rather than developing on
this one.

## Build

Both chips use `BMS_PROFILE=3` (LFP1, 108 cells) and `BMS_BOARD=1` (Rev0), plain
USB (no external pull-up, no VBUS sense):

```bash
cmake -S . -B build-ck1 -DBMS_PROFILE=3 -DBMS_BOARD=1
cmake -S supervisor -B supervisor/build-ck1 -DBMS_PROFILE=3 -DBMS_BOARD=1
```

`BMS_BOARD=1` matters for the supervisor too: it puts the isoSPI snooper's PIO
flag on GPIO5. With the Rev1 pin (GPIO30) the supervisor decodes nothing on
ck1's board and never enables the contactors.

## Flashing ck1

Via `ssh lfp-shed-pi`. Stop the pack first (`restart\r` on the controller
console), flash the supervisor and then the controller with
`sudo picotool load -v -x <uf2> -f --ser <serial>`, check the boot (pack type
108s1p SR LFP, supervisor ON with 0 CRC errors), then send `toggle\r` (Run).
Controller 607CD4F76E6454EC, supervisor E720584CF2631C82.

Rollback images on the Pi: `/tmp/bms-3a31ee6-lfp1-rev0.uf2` (controller, from
before 2026-10-20) and `~/supervisor-backup-2026-10-20.uf2` (the old superrr
supervisor).
