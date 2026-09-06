import serial
import re
import matplotlib.pyplot as plt
from collections import deque

PORT = "COM3"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)

inst_bpm = deque(maxlen=100)
avg_bpm = deque(maxlen=100)

plt.ion()

fig, ax = plt.subplots()

while True:
    line = ser.readline().decode("utf-8", errors="ignore").strip()

    if not line:
        continue

    print(line)

    # Look for: Inst BPM: 78
    match = re.search(r"Inst BPM:\s*([\d.]+)", line)
    if match:
        inst_bpm.append(float(match.group(1)))

    # Look for: Avg BPM: 75
    match = re.search(r"Avg BPM:\s*([\d.]+)", line)
    if match:
        avg_bpm.append(float(match.group(1)))

    # Redraw graph
    ax.clear()

    ax.plot(inst_bpm, label="Instant BPM")
    ax.plot(avg_bpm, label="Average BPM")

    ax.set_title("Heart Rate")
    ax.set_xlabel("Samples")
    ax.set_ylabel("BPM")

    ax.legend()
    ax.grid()

    plt.pause(0.01)