import csv, os, sys
import matplotlib.pyplot as plt

TESTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tests")
src = sys.argv[1]
if not os.path.exists(src):
    src = os.path.join(TESTS_DIR, os.path.basename(src))
rows = list(csv.DictReader(open(src)))
t = [float(r["time_us"]) for r in rows]
v = [float(r["voltage_pin_v"]) for r in rows]
i = [float(r["current_pin_v"]) for r in rows]

fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True)
ax1.plot(t, v, marker=".")
ax1.set_ylabel("V_DCHG pin (V)")
ax2.plot(t, i, marker=".", color="tab:orange")
ax2.set_ylabel("I_DCHG pin (V)")
ax2.set_xlabel("time (µs)")
fig.suptitle(os.path.basename(src))
os.makedirs(TESTS_DIR, exist_ok=True)
png = os.path.join(TESTS_DIR, os.path.splitext(os.path.basename(src))[0] + ".png")
fig.savefig(png, dpi=150)
print("saved " + png)
plt.show()