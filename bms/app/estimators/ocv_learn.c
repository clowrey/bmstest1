#include "ocv_learn.h"

#include <math.h>
#include <string.h>

#include "sys/logging/logging.h"
#include "ekf.h"
#include "config/limits.h"

// From prototyping on ~94 days of a 96S NMC pack (scripts next to this repo, in
// rpipico/kalman): rests plus low-current readings brought the EKF from 3.9% to
// 1.2% RMS, converging in about two weeks.
#define PRIOR_SD 0.060            // V, prior on each correction
#define PRIOR_ELL_BINS 1.5        // correlation length of the prior
#define Q_PER_DAY 1e-6            // (1mV)^2/day, so the curve can follow ageing
#define R_OHM 0.0013              // steady-state cell resistance, for low-current readings
#define MAX_GAP_S (21 * 86400)    // full charges further apart than this are too drifty
#define DAILY_DECAY 0.99446f      // exp(-1/180): counts fade with a 180 day time constant
#define TRUST_CYCLES 1.5f

// Learned capacity. How far the factory curve may be from the pack's at rest (V),
// e.g. hysteresis and ageing; a profile can override it. At 10mV, 10 shallow days
// of an aged 96S NMC pack (to 27% below full) gave 206 +- 13Ah against 177 +- 5Ah
// from 40 deeper days (to 67%), so its error bars were too narrow.
#ifndef OCV_LEARN_CURVE_SD_V
#define OCV_LEARN_CURVE_SD_V 0.020
#endif
#define CAPACITY_MIN_DEPTH 0.05   // shallower bins say too little about capacity
#define CAPACITY_GOOD_SD 0.05     // relative uncertainty for OCV_LEARN_CAPACITY_GOOD
#define CAPACITY_MAX_SD 0.25      // beyond this it's no figure at all, e.g. only an LFP plateau

#define S_FULL_MOVE 0.01f         // factory-curve SOC the charge voltage may move before full is re-anchored

// Bin index and weight of the upper bin for a given Ah below full. The bins run
// from full down to the factory curve's 0%, so they scale with the pack.
static int bin_of(const ocv_learn_t *l, double a, double *w) {
    double x = a / (l->persistent.s_full * l->cap) * (OCV_LEARN_BINS - 1);
    if (x <= 0.0) { *w = 0.0; return 0; }
    if (x >= OCV_LEARN_BINS - 1) { *w = 1.0; return OCV_LEARN_BINS - 2; }
    int j = (int)x;
    *w = x - j;
    return j;
}

static double corr_at(const ocv_learn_t *l, double a) {
    double w;
    int j = bin_of(l, a, &w);
    return l->persistent.corr[j] * (1.0 - w) + l->persistent.corr[j + 1] * w;
}

// Learned OCV at a Ah below full
static double curve(const ocv_learn_t *l, double a) {
    return table_ocv(l->factory, l->persistent.s_full - a / l->cap) + corr_at(l, a);
}

static void update(ocv_learn_t *l, double a, double v, double sv, double sa) {
    ocv_learn_persistent_t *p = &l->persistent;
    double w;
    int j = bin_of(l, a, &w);
    double slope = (curve(l, a + 1.0) - curve(l, a - 1.0)) / 2.0;
    double ph[OCV_LEARN_BINS];
    for (int i = 0; i < OCV_LEARN_BINS; i++) ph[i] = p->P[i][j] * (1.0 - w) + p->P[i][j + 1] * w;
    double s = ph[j] * (1.0 - w) + ph[j + 1] * w + sv * sv + slope * slope * sa * sa;
    double y = v - curve(l, a);
    for (int i = 0; i < OCV_LEARN_BINS; i++) p->corr[i] += ph[i] / s * y;
    for (int i = 0; i < OCV_LEARN_BINS; i++)
        for (int k = 0; k < OCV_LEARN_BINS; k++) p->P[i][k] -= ph[i] * ph[k] / s;
}


// SOC uncertainty of a voltage looked up on the factory curve: half the SOC span of
// v +- OCV_LEARN_CURVE_SD_V. Between LFP plateaus a local slope looks steep while a
// few mV either way lands anywhere on a plateau, so it takes the span, not the slope.
static double curve_sd_soc(const float *f, double v) {
    return (table_soc(f, (float)(v + OCV_LEARN_CURVE_SD_V)) - table_soc(f, (float)(v - OCV_LEARN_CURVE_SD_V))) / 2.0;
}

static void clear_capacity(ocv_learn_t *l, uint8_t state) {
    l->capacity_Ah = l->capacity_sd_Ah = NAN;
    l->capacity_depth = l->capacity_depth_V = l->capacity_depth_cycles = NAN;
    l->capacity_state = state;
}

// Capacity from each trusted bin, C = Ah below full / factory-curve SOC below full,
// combined by inverse variance. A voltage error becomes an SOC error on the curve, at
// the bin and at full, and is divided by the depth, so the deep bins dominate and the
// flat parts of an LFP curve hardly count.
static void estimate_capacity(ocv_learn_t *l) {
    const ocv_learn_persistent_t *p = &l->persistent;
    clear_capacity(l, OCV_LEARN_CAPACITY_NO_DATA);
    double sd_full = curve_sd_soc(l->factory, table_ocv(l->factory, p->s_full));
    // Per bin: capacity, its error from the curve, and dC/dV for the learned voltage
    double w[OCV_LEARN_BINS] = {0}, c[OCV_LEARN_BINS], sd_curve[OCV_LEARN_BINS], g[OCV_LEARN_BINS];
    bool used[OCV_LEARN_BINS] = {false};
    double sw = 0.0;
    for (int j = 1; j < OCV_LEARN_BINS; j++) {
        if (p->cycles[j] < TRUST_CYCLES) continue;
        double a = (double)j / (OCV_LEARN_BINS - 1) * p->s_full * l->cap;
        double v = curve(l, a);
        double s = table_soc(l->factory, (float)v);
        double depth = p->s_full - s, sd_s = curve_sd_soc(l->factory, v);
        if (depth < CAPACITY_MIN_DEPTH || sd_s <= 0.0) continue;
        c[j] = a / depth;
        sd_curve[j] = c[j] / depth * sqrt(sd_full * sd_full + sd_s * sd_s);
        g[j] = c[j] / depth * sd_s / OCV_LEARN_CURVE_SD_V;   // dC/dV, over the same span
        w[j] = 1.0 / (sd_curve[j] * sd_curve[j] + g[j] * g[j] * p->P[j][j]);
        used[j] = true;
        sw += w[j];
        if (isnan(l->capacity_depth) || depth > l->capacity_depth) {
            l->capacity_depth = (float)depth;
            l->capacity_depth_V = (float)v;
            l->capacity_depth_cycles = p->cycles[j];
        }
    }
    if (sw <= 0.0) return;
    double cap = 0.0, sd_c = 0.0, var_v = 0.0;
    for (int j = 1; j < OCV_LEARN_BINS; j++) {
        if (!used[j]) continue;
        w[j] /= sw;
        cap += w[j] * c[j];
        sd_c += w[j] * sd_curve[j];   // shared by every bin, so it adds up rather than averaging out
        for (int k = 1; k < OCV_LEARN_BINS; k++) {
            // Through the learner's covariance, which is tight for the average of two bins a rest fell between
            if (used[k]) var_v += w[j] * g[j] * w[k] * g[k] * p->P[j][k];
        }
    }
    double sd = sqrt(sd_c * sd_c + (var_v > 0.0 ? var_v : 0.0));
    if (sd > CAPACITY_MAX_SD * cap) {
        clear_capacity(l, OCV_LEARN_CAPACITY_NO_DATA);
        return;
    }
    l->capacity_Ah = (float)cap;
    l->capacity_sd_Ah = (float)sd;
    l->capacity_state = sd <= CAPACITY_GOOD_SD * cap ? OCV_LEARN_CAPACITY_GOOD : OCV_LEARN_CAPACITY_PRELIMINARY;
}

// Rebuild the OCV table from the factory one and the learned correction
static void rebuild(ocv_learn_t *l) {
    const ocv_learn_persistent_t *p = &l->persistent;
    for (int i = 0; i <= 100; i++) {
        l->table[i] = l->factory[i] + corr_at(l, (p->s_full - i / 100.0) * l->cap);
        if (i > 0 && l->table[i] < l->table[i - 1] + 1e-4f) l->table[i] = l->table[i - 1] + 1e-4f;
    }

    // Capacity to V_REF, only where both bins either side have recent data
    double a = (p->s_full - table_soc(l->table, OCV_LEARN_V_REF)) * l->cap, w;
    int j = bin_of(l, a, &w);
    bool trusted = p->cycles[j] >= TRUST_CYCLES && p->cycles[j + 1] >= TRUST_CYCLES;
    l->capacity_ref_Ah = trusted ? (float)a : NAN;
    // Logged even untrusted, so a one-off calibration discharge shows its result
    info_printf("OCV learn: %.1fAh from full to %.2fV%s (%.1f and %.1f cycles)\n", a, OCV_LEARN_V_REF,
                trusted ? "" : ", not yet trusted", p->cycles[j], p->cycles[j + 1]);

    estimate_capacity(l);
    if (l->capacity_state >= OCV_LEARN_CAPACITY_PRELIMINARY) {
        info_printf("OCV learn: capacity %.1f +- %.1fAh%s, from bins down to %.2fV (%.0f%% below full)\n",
                    l->capacity_Ah, l->capacity_sd_Ah,
                    l->capacity_state == OCV_LEARN_CAPACITY_GOOD ? "" : " (preliminary)",
                    l->capacity_depth_V, l->capacity_depth * 100.0f);
    }
}

static void init_prior(ocv_learn_persistent_t *p, float s_full) {
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < OCV_LEARN_BINS; i++)
        for (int k = 0; k < OCV_LEARN_BINS; k++)
            p->P[i][k] = PRIOR_SD * PRIOR_SD * exp(-fabs((double)(i - k)) / PRIOR_ELL_BINS);
    p->s_full = s_full;
    p->valid = true;
}

// At a new settled full charge: place each held reading between it and the previous one
static void learn_from_readings(ocv_learn_t *l) {
    ocv_learn_persistent_t *p = &l->persistent;
    double cycle_w[OCV_LEARN_BINS] = {0};
    double total_Ah = l->charge_Ah - l->anchor_Ah;
    uint32_t span_s = l->t_s - l->anchor_s;
    int used = 0;
    for (int n = 0; n < l->n_readings; n++) {
        const ocv_reading_t *r = &l->readings[n];
        double hp = (r->t_s - l->anchor_s) / 3600.0, hn = (l->t_s - r->t_s) / 3600.0;
        double w = hp / (hp + hn);
        double bp = -r->dq_Ah, bn = total_Ah - r->dq_Ah;   // Ah below the full charge before / after
        double a = (1.0 - w) * bp + w * bn;
        if (a < 0.5) continue;
        double sa = 0.3 + fabs(bp - bn) * (w < 0.5 ? w : 1.0 - w) + 0.005 * (hp < hn ? hp : hn);
        update(l, a, r->v, r->sv, sa);
        double wb;
        int j = bin_of(l, a, &wb);
        cycle_w[j] += 1.0 - wb;
        cycle_w[j + 1] += wb;
        used++;
    }
    for (int i = 0; i < OCV_LEARN_BINS; i++) {
        if (cycle_w[i] >= 1.0) p->cycles[i] += 1.0f;
        p->P[i][i] += Q_PER_DAY * span_s / 86400.0;
    }
    info_printf("OCV learn: %d readings over %.1f days\n", used, span_s / 86400.0);
}

bool ocv_learn_set_s_full(ocv_learn_t *l, float s_full) {
    if (!(s_full > 0.0f && s_full <= 1.0f)) return false;
    if (!l->persistent.valid) {
        init_prior(&l->persistent, s_full);
    } else {
        l->persistent.s_full = s_full;
    }
    ocv_learn_mark_edited(l);
    return true;
}

bool ocv_learn_tick(ocv_learn_t *l, const float factory[101], double capacity_Ah, float charge_V,
                    double charge_Ah, float current_A, float voltage_V) {
    ocv_learn_persistent_t *p = &l->persistent;
    bool changed = false;
    l->cap = capacity_Ah;
    if (!l->factory) {
        l->factory = factory;
        l->capacity_ref_Ah = NAN;
        clear_capacity(l, OCV_LEARN_CAPACITY_NOT_LEARNING);
        if (p->valid) {
            rebuild(l);
            changed = true;
        }
    }
    if (l->edited) {
        l->edited = false;
        if (p->valid) {
            rebuild(l);
            changed = true;
        }
    }

    // The charge voltage moved (a new working max or inverter offset, or a new factory
    // curve): full now sits elsewhere on the curve. For now, re-anchor full there, drop
    // the readings held against the old full, and have every bin earn its trust again.
    // The corrections stay, though they're now out by the Ah between the old and new full.
    // A fuller fix would shift them by that Ah instead, measured across a charge from one
    // to the other or estimated from the factory curve, so what was learned carries over.
    if (p->valid) {
        float s_charge = table_soc(l->factory, charge_V);
        if (fabsf(s_charge - p->s_full) > S_FULL_MOVE) {
            info_printf("OCV learn: full moved from %.1f%% to %.1f%% (charge voltage %.3fV), relearning\n",
                        p->s_full * 100.0f, s_charge * 100.0f, charge_V);
            p->s_full = s_charge;
            for (int i = 0; i < OCV_LEARN_BINS; i++) p->cycles[i] = 0.0f;
            l->n_readings = 0;
            l->have_anchor = false;
            l->dirty = true;
            rebuild(l);
            changed = true;
        }
    }
    l->t_s++;
    l->charge_Ah += charge_Ah;

    if (l->t_s % 86400 == 0) {
        for (int i = 0; i < OCV_LEARN_BINS; i++) p->cycles[i] *= DAILY_DECAY;
        l->dirty = true;
    }

    float i_abs = fabsf(current_A);
    l->quiet_s = i_abs < 0.3f ? l->quiet_s + 1 : 0;
    l->low_s = i_abs < 3.0f ? l->low_s + 1 : 0;
    l->full_s = (voltage_V >= charge_V - 0.010f && i_abs < 0.5f) ? l->full_s + 1 : 0;

    if (l->full_s >= OCV_LEARN_FULL_SETTLE_S) {
        if (l->full_s == OCV_LEARN_FULL_SETTLE_S) {
            // Settled full charge reached. Fix where full sits on the factory curve once,
            // at the charge voltage, so the learned Ah-based correction keeps one frame
            // (until the charge voltage moves, above).
            if (!p->valid) {
                init_prior(p, table_soc(l->factory, charge_V));
                l->dirty = true;
            }
            if (l->have_anchor && l->t_s - l->anchor_s <= MAX_GAP_S && l->n_readings > 0) {
                learn_from_readings(l);
                rebuild(l);
                changed = true;
                l->dirty = true;
            }
            l->n_readings = 0;
        }
        // The anchor is the end of the full charge, so keep moving it while we're here
        l->have_anchor = true;
        l->anchor_Ah = l->charge_Ah;
        l->anchor_s = l->t_s;
    } else if (l->have_anchor && l->t_s - l->last_reading_s >= 7200 && l->n_readings < OCV_LEARN_MAX_READINGS &&
               (l->quiet_s >= 7200 || l->low_s >= 1800)) {
        ocv_reading_t *r = &l->readings[l->n_readings++];
        r->t_s = l->t_s;
        r->dq_Ah = (float)(l->charge_Ah - l->anchor_Ah);
        if (l->quiet_s >= 7200) {
            r->v = voltage_V;
            r->sv = 0.002f + 0.015f * 7200.0f / l->quiet_s;  // still relaxing after a short rest
        } else {
            r->v = voltage_V - current_A * R_OHM;
            r->sv = 0.006f + 0.0005f * i_abs;
        }
        l->last_reading_s = l->t_s;
    }
    return changed;
}
