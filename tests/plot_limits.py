import csv
import matplotlib.pyplot as plt
import sys
import os

def plot_limits(csv_file):
    # Read the CSV data using standard csv module
    data = []
    try:
        with open(csv_file, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                data.append({
                    'temp': int(row['temperature_dC']) / 10.0,
                    'volt': int(row['voltage_mV']) / 1000.0,
                    'charge': int(row['charge_limit_dA']) / 10.0,
                    'discharge': int(row['discharge_limit_dA']) / 10.0
                })
    except Exception as e:
        print(f"Error reading {csv_file}: {e}")
        return

    if not data:
        print("CSV file is empty or invalid.")
        return

    # Extract unique sorted axes
    temps = sorted(list(set(d['temp'] for d in data)))
    volts = sorted(list(set(d['volt'] for d in data)))

    # Create mapping for grid placement
    temp_map = {t: i for i, t in enumerate(temps)}
    volt_map = {v: j for j, v in enumerate(volts)}

    # Initialize matrices for plotting
    charge_matrix = [[0.0 for _ in range(len(volts))] for _ in range(len(temps))]
    discharge_matrix = [[0.0 for _ in range(len(volts))] for _ in range(len(temps))]

    for d in data:
        charge_matrix[temp_map[d['temp']]][volt_map[d['volt']]] = d['charge']
        discharge_matrix[temp_map[d['temp']]][volt_map[d['volt']]] = d['discharge']

    # Create the plots using Matplotlib
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7))

    # Charge Limit Heatmap
    im1 = ax1.imshow(charge_matrix, aspect='auto', origin='lower',
                     extent=[volts[0], volts[-1], temps[0], temps[-1]],
                     cmap='magma')
    fig.colorbar(im1, ax=ax1, label='Amps')
    ax1.set_title('Charge Current Limit (A)')
    ax1.set_xlabel('Cell Voltage (V)')
    ax1.set_ylabel('Temperature (°C)')

    # Discharge Limit Heatmap
    im2 = ax2.imshow(discharge_matrix, aspect='auto', origin='lower',
                     extent=[volts[0], volts[-1], temps[0], temps[-1]],
                     cmap='magma')
    fig.colorbar(im2, ax=ax2, label='Amps')
    ax2.set_title('Discharge Current Limit (A)')
    ax2.set_xlabel('Cell Voltage (V)')
    ax2.set_ylabel('Temperature (°C)')

    plt.tight_layout()
    
    # Save the plot
    output_file = 'current_limits_plot.png'
    plt.savefig(output_file)
    print(f"Plot saved to {output_file}")
    
    # Show the plot if possible
    try:
        plt.show()
    except Exception:
        pass

if __name__ == "__main__":
    filename = 'current_limits.csv'
    if len(sys.argv) > 1:
        filename = sys.argv[1]
    
    if not os.path.exists(filename):
        print(f"File {filename} not found. Please run the dump utility first.")
        print("Usage: ./dump_current_limits > current_limits.csv")
    else:
        plot_limits(filename)
