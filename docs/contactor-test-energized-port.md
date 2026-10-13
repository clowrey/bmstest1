# Contactor self-test fails while the Deye keeps its battery port energized

**Status:** fixed in `bug-fix` 9c3676b (2026-10-12). ck0 has been flashed with it; ck1 hasn't
yet, because flashing ck1 means another inverter restart.

- **What changed:** with both contactors commanded open, an output of at least 80 % of the battery
  voltage now raises the warning `CONTACTOR_PORT_ENERGIZED` instead of a FATAL weld.
- **Retry:** the self-test waits 20 s and runs again, up to 30 times. After that it judges the
  contactors as before.
- **A dead output** still detects a weld immediately.

A battery that already latched FAULT still needs a controller reboot. Run alone doesn't leave the
FAULT state.

## Symptom

When a battery is (re)started while the Deye inverter is running, the BMS contactor self-test
fails with **FATAL `CONTACTOR_POS_STUCK_CLOSED`** (plus WARNING `CONTACTOR_CLOSING_FAILED`). The
system goes to **FAULT**: the contactors stay open, and the battery needs a manual **Run** to try
again.

The contactors are not welded. The test passes as soon as the inverter's battery port is no longer
energized, for example right after an inverter restart, or while the inverter is faulted.

## Observed at Umauma (2026-10-12)

| Time (HST) | Battery | Sequence |
|---|---|---|
| 13:04:08–13:04:34 | ck1 (Rev0, BAT1) | Stopped → Run → `TST_NEG_OP` → **`TST_FAIL`** → FAULT. Events: `CONTACTOR_POS_STUCK_CLOSED` (FATAL), `CONTACTOR_CLOSING_FAILED`. The inverter showed F56 (see the Deye note below). |
| 14:42:06–14:42:21 | ck0 (Rev1, BAT2) | Controller reflashed → INIT → `TST_PRE_CL` → `TST_NEG_OP` → `TST_NEG_CL` → `TST_POS_OP` → **`TST_FAIL`** → FAULT. Same two events. The inverter showed F58 for about 20 s and the AC output dropped, then it ran on ck1. |
| 12:01, 12:54 | ck1, ck0 | Start-ups that **passed**. At those times the inverter was faulted or had just restarted, so the battery port was dead. |

**Readings during the 14:42 ck0 test:**
- With ck0's contactors open, its **output (the Deye BAT2 port) stayed at 379–385 V**. The pack
  was about 386–388 V.
- About 3 V across the negative contactor.
- Pack Output Voltage only fell to 0 V at 14:42:43, after the inverter had restarted.

## Why the test fails

The self-test (`bms/app/state_machines/contactors.c`) runs before precharge:

```
TESTING_PRE_CLOSED  → precharge relay aux contact must read closed
TESTING_NEG_OPEN    → all open:      confirm_contactor_pos_and_neg_seems_open()
TESTING_NEG_CLOSED  → neg closed:    confirm_contactor_neg_seems_closed()
TESTING_POS_OPEN    → all open:      confirm_contactor_pos_and_neg_seems_open()
TESTING_POS_CLOSED  → pos closed:    confirm_contactor_pos_seems_closed()
→ PRECHARGING
```

"Seems open" means **at least** `CONTACTORS_OPEN_VOLTAGE_THRESHOLD_MV` /
`CONTACTORS_POS_OPEN_VOLTAGE_THRESHOLD_MV` across the contactor (`config/limits.h`):

| Board | Threshold |
|---|---|
| Rev1 | **3 V** |
| Rev0 (positive contactor) | **10 V** |

The check assumes the output side is dead or floating when the contactors are open. On a Deye HV
inverter it isn't:
- **The Deye keeps the port energized.** While a battery input is enabled, it holds that port at
  about pack voltage from its DC bus.
- **That makes an open contactor look closed.** With Out+ ≈ Batt+ and Out− ≈ Batt−, the voltage
  across each open contactor is only a few volts, so it reads as stuck closed.
- **Only when the port goes dead does the test pass.** That happens when the inverter disables
  that input, for example after its low-battery shutdown and restart (see
  `Deye-Firmware/50K/notes/BatteryLowShutdown.md`), or when it is faulted.

The result depends on timing, which is why the same battery sometimes passes and sometimes fails.

## Workaround (current firmware)

- **After a FAULT from this,** press **Run** again once the inverter has restarted and the
  battery's *Pack Output Voltage* reads ~0 V. The test then passes.
- **Avoid opening a battery's contactors while the inverter is running.** Opening them also
  triggers the Deye's low-battery restart (see `deye-offgrid-charging.md`).

## The fix (as implemented)

Tell an externally energized port apart from a welded contactor:

- With **both** contactors commanded open, a single welded contactor cannot put close to full pack
  voltage across Out+/Out−. Only an external source can, or both contactors welded.
- **If the output voltage is close to the pack voltage while both are commanded open,** treat it as
  "port energized externally":
  - don't raise `*_STUCK_CLOSED` / FATAL;
  - wait (for example in `TESTING_FAILED` or a new `WAITING_PORT_DEAD` state) and retry the test,
    rather than going to system FAULT.
- **Keep real weld detection** for the case where the output is dead or floating, as now.
- **Optionally,** skip precharge when the port is already within the precharge tolerance of the
  pack. The inverter has effectively precharged it.
- **Covered by host tests (`tests/test_contactors.c`):** energized port → retry and no FATAL; a
  real weld (dead port) → still FATAL.

Related: the planned **standby** request (limits 0, contactors closed) avoids both this and the
Deye's restart when taking a battery offline.

## Open questions

- How long the Deye keeps an enabled port energized after the battery disconnects. ck0's port
  stayed up for at least ~35 s until the inverter restart.
- Whether the Deye energizes the port before a battery is first connected, or only once it has
  been enabled.
