% Read CSV file
data = readtable('231833.CSV');

% Extract time and ax acceleration
t = data.t_s;
ax = data.raw_ax_mps2;

% Existing filtered ax from CSV
ax_csv_filtered = data.raw_ax_filt_mps2;

% Sampling frequency from time column
dt = median(diff(t));
Fs = 1/dt;

% Low-pass filter settings
Fc = 5;          % cutoff frequency in Hz, adjust if needed
order = 4;

% Butterworth low-pass filter
[b, a] = butter(order, Fc/(Fs/2), 'low');

% Zero-delay filtering: forward and backward
ax_filtered = filtfilt(b, a, ax);

% Add new filtered ax to the table
data.ax_filtered_new = ax_filtered;

% Difference between CSV filtered and your new filtered signal
data.ax_filter_difference = ax_filtered - ax_csv_filtered;

% Save new CSV file
writetable(data, '231833_ax_filtered_compared.csv');

% Plot comparison
figure
plot(t, ax, 'Color', [0.75 0.75 0.75])
hold on
plot(t, ax_csv_filtered, 'LineWidth', 2)
plot(t, ax_filtered, '--', 'LineWidth', 2)

xlabel('Time [s]')
ylabel('a_x [m/s^2]')
legend('Original ax', 'CSV filtered ax', 'New MATLAB filtered ax')
title('Comparison of Original, CSV Filtered, and MATLAB Filtered ax')
grid on