import numpy as np
import matplotlib.pyplot as plt

# Constants
CHARGE_MAX_CURRENT_dA = 500  # 50A
BMS_INTERNAL_RESISTANCE_uR = 100
SIM_INTERNAL_RESISTANCE_uR = 14 # Different resistance for testing realism
DEFAULT_CELL_VOLTAGE_WORKING_MAX_mV = 4180 # LFP
SIM_DURATION_S = 200
SIM_STEP_S = 0.02
INVERTER_LAG_S = 5.0

#true pack resistance probably 1.3mohm, so per cell = 14uR

# AEMA Constants
AEMA_ALPHA_MIN = 0.1
AEMA_ALPHA_MAX = 0.2
AEMA_NOISE_THRESHOLD = 1.0  # dA
AEMA_CHANGE_THRESHOLD = 10.0 # dA

class AEMA:
    def __init__(self):
        self.value = np.nan
        self.deviation = 0.0

    def update(self, input_val):
        if np.isnan(self.value):
            self.value = input_val
            self.deviation = 0.0
            return self.value

        diff = abs(input_val - self.value)
        
        if diff <= AEMA_NOISE_THRESHOLD:
            alpha = AEMA_ALPHA_MIN
        elif diff >= AEMA_CHANGE_THRESHOLD:
            alpha = AEMA_ALPHA_MAX
        else:
            ratio = (diff - AEMA_NOISE_THRESHOLD) / (AEMA_CHANGE_THRESHOLD - AEMA_NOISE_THRESHOLD)
            alpha = AEMA_ALPHA_MIN + (ratio * (AEMA_ALPHA_MAX - AEMA_ALPHA_MIN))

        self.value = (alpha * input_val) + ((1.0 - alpha) * self.value)
        
        deviation_alpha = 0.1
        self.deviation = (deviation_alpha * diff) + ((1.0 - deviation_alpha) * self.deviation)
        return self.value

def calculate_working_charge_current_limit_step(current_mA, cell_voltage_max_mV, aema, target_max_mV=DEFAULT_CELL_VOLTAGE_WORKING_MAX_mV):
    current_dA = current_mA / 100.0
    target_ocv_uV = target_max_mV * 1000

    # 1. Estimate current cell OCV
    guessed_ocv_uV = cell_voltage_max_mV * 1000 - (current_mA * BMS_INTERNAL_RESISTANCE_uR) / 1000

    # 2. Calculate target limit to maintain OCV at working max
    if guessed_ocv_uV < target_ocv_uV:
        delta_uV = target_ocv_uV - guessed_ocv_uV
        target_limit_dA = delta_uV * 10.0 / BMS_INTERNAL_RESISTANCE_uR
        # print("max is {} mV, guessed OCV is {:.2f} mV, delta is {:.2f} mV, target limit is {:.2f} dA".format(
        #    cell_voltage_max_mV, guessed_ocv_uV / 1000.0, delta_uV / 1000.0, target_limit_dA))
    else:
        target_limit_dA = 0.0


    # Clip to absolute max charge
    if target_limit_dA > CHARGE_MAX_CURRENT_dA:
        target_limit_dA = CHARGE_MAX_CURRENT_dA

    # 3. Apply Tethered Ceiling
    current_clamped_dA = max(current_dA, 0.0)
    tethered_ceiling_dA = current_clamped_dA + 20.0 # 2A ceiling
    if target_limit_dA > tethered_ceiling_dA:
        target_limit_dA = tethered_ceiling_dA

    if target_limit_dA < 0.0:
        target_limit_dA = 0.0

    # 4. Update smoothed value via AEMA
    filtered_limit_dA = aema.update(target_limit_dA)
    return filtered_limit_dA, target_limit_dA

def run_simulation():
    steps = int(SIM_DURATION_S / SIM_STEP_S)
    time = np.linspace(0, SIM_DURATION_S, steps)
    
    # State
    ocv_mV = 4170  # Start at 3.3V
    current_mA = 0.0
    aema = AEMA()
    
    # Cell Model
    # Capacitance in Ah to mV slope (very simple)
    # Let's say 100Ah battery, slope is ~200mV (3.2V to 3.4V)
    # I = dQ/dt => dV = dt * I / C_fixed
    C_mV_per_As = 0.0001 # Extremely simplified
    
    # History for inverter lag
    limit_history = []
    
    results = {
        'time': time,
        'limit_dA': [],
        'target_dA': [],
        'current_mA': [],
        'ocv_mV': [],
        'guessed_ocv_mV': [],
        'v_meas_mV': []
    }
    
    inverter_lag_steps = int(INVERTER_LAG_S / SIM_STEP_S)
    
    for i in range(steps):
        # 1. Inverter logic (lagged)
        if len(limit_history) > inverter_lag_steps:
            target_limit_dA = limit_history[-inverter_lag_steps]
        else:
            target_limit_dA = 0.0
            
        current_mA = target_limit_dA * 100.0
        
        # 2. Cell voltage response
        # v_meas_mV = ocv_mV + (current_mA * WORKING_LIMIT_INTERNAL_RESISTANCE_uR) / 1000.0 / 1000.0
        # V = (I * R) / 1,000,000 mV
        v_meas_mV = ocv_mV + (current_mA * SIM_INTERNAL_RESISTANCE_uR) / 1000000.0
        
        # Increase OCV
        ocv_mV += (current_mA / 1000.0) * SIM_STEP_S * 0.002 # Slightly slower OCV rise
        
        # 3. BMS limit calculation
        limit_dA, target_dA = calculate_working_charge_current_limit_step(current_mA, v_meas_mV, aema)
        
        # Calculate guessed OCV for logging
        guessed_ocv_mV = (v_meas_mV * 1000 - (current_mA * BMS_INTERNAL_RESISTANCE_uR) / 1000) / 1000.0
        
        # 4. Record history
        limit_history.append(limit_dA)
        
        # Logging
        results['limit_dA'].append(limit_dA)
        results['target_dA'].append(target_dA)
        results['current_mA'].append(current_mA)
        results['ocv_mV'].append(ocv_mV)
        results['guessed_ocv_mV'].append(guessed_ocv_mV)
        results['v_meas_mV'].append(v_meas_mV)

    # Plotting
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    
    ax1.plot(time, results['limit_dA'], label='Filtered Limit (dA)')
    ax1.plot(time, results['target_dA'], label='Raw Target (dA)', alpha=0.3)
    ax1.plot(time, np.array(results['current_mA'])/100.0, label='Actual Current (dA)', linestyle='--')
    ax1.set_ylabel('Current (dA, 0.1A)')
    ax1.legend()
    ax1.set_title('BMS Charge Current Limits and Inverter Response (5s Lag)')
    ax1.grid(True)
    
    ax2.plot(time, results['ocv_mV'], label='True OCV (mV)')
    ax2.plot(time, results['guessed_ocv_mV'], label='BMS Guessed OCV (mV)', alpha=0.5)
    ax2.plot(time, results['v_meas_mV'], label='V_measured (mV)', linestyle=':', alpha=0.3)
    ax2.axhline(DEFAULT_CELL_VOLTAGE_WORKING_MAX_mV, color='r', linestyle='--', label='Working Max Target')
    ax2.set_ylabel('Voltage (mV)')
    ax2.set_xlabel('Time (s)')
    ax2.legend()
    ax2.grid(True)
    
    plt.tight_layout()
    plt.savefig('working_limit_model.png')
    print("Simulation complete. Plot saved to working_limit_model.png")

if __name__ == "__main__":
    run_simulation()
