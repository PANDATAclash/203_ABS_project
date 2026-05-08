import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import butter, filtfilt

# Read CSV file
data = pd.read_csv("231833.CSV")

# Extract time and ax acceleration
t = data["t_s"].to_numpy()
ax = data["raw_ax_mps2"].to_numpy()

# Existing filtered ax from CSV
ax_csv_filtered = data["raw_ax_filt_mps2"].to_numpy()

# Sampling frequency from time column
dt = np.median(np.diff(t))
Fs = 1 / dt

# Low-pass filter settings
Fc = 5        # cutoff frequency in Hz
order = 4

# Butterworth low-pass filter
b, a = butter(order, Fc / (Fs / 2), btype="low")

# Zero-phase filtering: same idea as MATLAB filtfilt()
ax_filtered = filtfilt(b, a, ax)

# Add new filtered ax and difference to dataframe
data["ax_filtered_new"] = ax_filtered
data["ax_filter_difference"] = ax_filtered - ax_csv_filtered

# Save new CSV file
data.to_csv("231833_ax_filtered_compared.csv", index=False)

# Plot comparison
plt.figure()
plt.plot(t, ax, color="gray", alpha=0.5, label="Original ax")
plt.plot(t, ax_csv_filtered, linewidth=2, label="CSV filtered ax")
plt.plot(t, ax_filtered, "--", linewidth=2, label="Python filtfilt ax")
plt.xlabel("Time [s]")
plt.ylabel("a_x [m/s²]")
plt.title("Comparison of Original, CSV Filtered, and Python Filtered ax")
plt.legend()
plt.grid(True)
plt.show()

# Plot difference
plt.figure()
plt.plot(t, data["ax_filter_difference"], linewidth=1.5)
plt.xlabel("Time [s]")
plt.ylabel("Difference [m/s²]")
plt.title("Difference: Python filtered ax - CSV filtered ax")
plt.grid(True)
plt.show()

# Error values
diff_ax = ax_filtered - ax_csv_filtered

mean_difference = np.mean(diff_ax)
max_difference = np.max(np.abs(diff_ax))
rms_difference = np.sqrt(np.mean(diff_ax**2))

print(f"Sampling frequency: {Fs:.2f} Hz")
print(f"Mean difference: {mean_difference:.6f} m/s²")
print(f"Max absolute difference: {max_difference:.6f} m/s²")
print(f"RMS difference: {rms_difference:.6f} m/s²")