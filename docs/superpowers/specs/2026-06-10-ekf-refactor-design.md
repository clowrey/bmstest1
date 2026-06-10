# EKF Refactor Design

**Date:** 2026-06-10  
**Scope:** `bms/app/estimators/ekf.c` and `ekf.h`

---

## Problem

`ekf_tick` is the only function in `ekf.c` that accesses the global `bms_model_t model` directly, while every other function in the file accepts `bms_model_t *model` explicitly. This creates a hidden dependency, makes the code harder to test (tests must know about the global), and is inconsistent with the rest of the file.

Two secondary issues:
1. `ekf_auto_init` uses `charge_used_Ah != 0.0f` to detect a stored SoC estimate, which is incorrect when the battery is fully charged (0 Ah used = 100% SoC).
2. `ekf_update_limits` sets `working_capacity_mC` with a `TODO - move` comment that has never been resolved.

---

## Design

### 1. Explicit model passing in `ekf_tick`

Change the signature from:
```c
uint32_t ekf_tick(float charge_Ah, int32_t current_mA, int32_t voltage_mV);
```
to:
```c
uint32_t ekf_tick(bms_model_t *model, float charge_Ah, int32_t current_mA, int32_t voltage_mV);
```

The `extern bms_model_t model` access inside `ekf_tick` is replaced with `model->`. All internal calls (`ekf_auto_init`, `ekf_update_limits`, etc.) already accepted a pointer and are unchanged.

**Callers updated:**
- `bms/app/bms.c`: `ekf_tick(...)` → `ekf_tick(&model, ...)`
- `tests/test_soc.c`: `ekf_tick(0, 0, ...)` → `ekf_tick(&model, 0, 0, ...)`
- `bms/app/estimators/ekf.h`: update declaration

---

### 2. Init: split `ekf_auto_init` into named phases

Rename `ekf_auto_init` → `ekf_init_from_model`. Extract the "pick voltage vs. stored charge" logic into a small static helper `estimate_initial_soc`:

```c
static float estimate_initial_soc(const bms_model_t *model, float voltage_volts, float capacity_ah) {
    float voltage_soc = ocv_to_soc(voltage_volts);
    if (model->charge_used_Ah >= 0.0f && model->charge_used_Ah <= capacity_ah) {
        float stored_soc = 1.0f - (model->charge_used_Ah / capacity_ah);
        // Accept stored value only if within 10% of voltage estimate.
        // No != 0 guard: zero Ah used is valid at 100% SoC, and the tolerance
        // already rejects an uninitialized zero (stored=100%, voltage≈50% → 0.5 diff).
        if (fabsf(stored_soc - voltage_soc) < 0.1f)
            return stored_soc;
    }
    return voltage_soc;
}

static void ekf_init_from_model(bms_model_t *model, float voltage_volts) {
    if (model->ekf.initialized || voltage_volts <= 0.0f) return;
    float capacity_ah = (float)model->nameplate_capacity_mC / 3600000.0f;
    float initial_soc = estimate_initial_soc(model, voltage_volts, capacity_ah);
    info_printf("EKF init: SOC %.2f%% (voltage %.3fV, stored %.4fAh)\n",
                initial_soc * 100.0f, voltage_volts, model->charge_used_Ah);
    ekf_init(&model->ekf, initial_soc, capacity_ah);
}
```

**Bug fix:** the `charge_used_Ah != 0.0f` guard is removed. The tolerance check (`< 0.1`) is sufficient — an uninitialized struct (charge=0, voltage≈50%) produces a diff of ~0.5, which exceeds the threshold and falls back to voltage-based init. Tolerance tightened from 0.2 to 0.1 for robustness.

---

### 3. `ekf_update_limits`: remove TODO, document intent

`working_capacity_mC` is computed inside `ekf_update_limits` because it depends on the same working voltage min/max inputs as the SOC scaling factors. The TODO comment is replaced with an explanation:

```c
// working_capacity_mC is the usable subset of nameplate capacity given the current
// working voltage range. Computed here because it shares the same min/max limit
// inputs as the SOC scaling factors above.
model->working_capacity_mC = (soc_max - model->ekf.prev_soc_min) * (float)model->nameplate_capacity_mC;
```

No structural change to this function.

---

## Files Changed

| File | Change |
|------|--------|
| `bms/app/estimators/ekf.c` | `ekf_tick` signature, rename `ekf_auto_init`, add `estimate_initial_soc`, fix init logic, update `ekf_update_limits` comment |
| `bms/app/estimators/ekf.h` | Update `ekf_tick` declaration |
| `bms/app/bms.c` | Update `ekf_tick` call site |
| `tests/test_soc.c` | Update `ekf_tick` call sites (2 tests) |

---

## Non-goals

- Moving `prev_min/max/soc_min/soc_mul` out of `ekf_t` (user decided to keep them there)
- Moving scaling logic out of `ekf.c` to the caller
- Any changes to `ekf_step`, `ekf_get_soc`, `ekf_set_soc`, or the EKF mathematics
