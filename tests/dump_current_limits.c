#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "../bms/app/battery/current_limits.h"
#include "../bms/config/limits.h"

// Mock nmc_ocv_to_soc if we don't want to link ekf.c (which has many dependencies)
// Or better, let's just implement a simple version here or link ekf.c if possible.
// Actually, nmc_ocv_to_soc is quite simple, I can just copy it here for the tool.

static float nmc_ocv_curve[101] = {
    2.9f,       3.0443f,    3.1399f,    3.2162f,    3.2762f,
    3.3229f,    3.3527f,    3.3664999f, 3.3755f,    3.3834f,
    3.3908f,    3.3982999f, 3.4063f,    3.4164f,    3.4296f,
    3.4435f,    3.4572999f, 3.4716f,    3.4847f,    3.4958999f,
    3.5062f,    3.5165f,    3.5265999f, 3.5360999f, 3.5448f,
    3.553f,     3.5606999f, 3.5682f,    3.5754f,    3.582f,
    3.588f,     3.5934999f, 3.5987f,    3.6036999f, 3.6085999f,
    3.6133f,    3.6180999f, 3.6228f,    3.6275f,    3.6322999f,
    3.6372f,    3.6422f,    3.6475f,    3.6529f,    3.6585f,
    3.6645f,    3.6707f,    3.6775f,    3.6847999f, 3.6926f,
    3.7012f,    3.7107999f, 3.7218f,    3.7346f,    3.7492f,
    3.7646999f, 3.7794f,    3.7925f,    3.8043f,    3.8152f,
    3.8255f,    3.8355f,    3.845f,     3.8543f,    3.8634f,
    3.8722f,    3.8806f,    3.8889f,    3.897f,     3.9049f,
    3.9128f,    3.9207f,    3.9288f,    3.9370999f, 3.9458f,
    3.9549f,    3.9644f,    3.9744f,    3.9849f,    3.9956999f,
    4.0068f,    4.0179f,    4.0288f,    4.0394f,    4.0495f,
    4.0587f,    4.0668998f, 4.0739999f, 4.0798998f, 4.0844998f,
    4.0883999f, 4.092f,     4.0956998f, 4.0998998f, 4.1048f,
    4.1108999f, 4.1188f,    4.1290998f, 4.143f,     4.1617999f,
    4.1880999f
};

float nmc_ocv_to_soc(float ocv) {
    if (ocv <= nmc_ocv_curve[0]) return 0.0f;
    if (ocv >= nmc_ocv_curve[100]) return 1.0f;

    for (int i = 0; i < 100; i++) {
        if (ocv < nmc_ocv_curve[i + 1]) {
            float frac = (ocv - nmc_ocv_curve[i]) / (nmc_ocv_curve[i + 1] - nmc_ocv_curve[i]);
            return (i + frac) / 100.0f;
        }
    }
    return 1.0f;
}

int main() {
    printf("temperature_dC,voltage_mV,charge_limit_dA,discharge_limit_dA\n");

    for (int temp_dC = -200; temp_dC <= 600; temp_dC += 2) {
        for (int volt_mV = 2200; volt_mV <= 4500; volt_mV += 2) {
            uint16_t t_charge = calculate_temperature_charge_current_limit(temp_dC, temp_dC);
            uint16_t t_discharge = calculate_temperature_discharge_current_limit(temp_dC, temp_dC);

            uint16_t v_charge = calculate_cell_voltage_charge_current_limit(volt_mV, volt_mV);
            uint16_t v_discharge = calculate_cell_voltage_discharge_current_limit(volt_mV, volt_mV);

            uint16_t final_charge = CHARGE_MAX_CURRENT_dA;
            if (t_charge < final_charge) final_charge = t_charge;
            if (v_charge < final_charge) final_charge = v_charge;

            uint16_t final_discharge = DISCHARGE_MAX_CURRENT_dA;
            if (t_discharge < final_discharge) final_discharge = t_discharge;
            if (v_discharge < final_discharge) final_discharge = v_discharge;

            printf("%d,%d,%u,%u\n", temp_dC, volt_mV, final_charge, final_discharge);
        }
    }

    return 0;
}
