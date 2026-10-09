"""Regenerates the figures shown in the README and prints the numbers of Table 2.

    pip install numpy pandas matplotlib
    python docs/figures/make_figures.py

Figure 2 reads docs/figures/bench.csv, produced by bench.cc (see the header of that file).
"""
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd
from matplotlib import font_manager
from matplotlib.patches import FancyBboxPatch

HERE = Path(__file__).resolve().parent
for f in Path("/usr/share/fonts/lm").glob("lm*10-*.otf"):  # Latin Modern, if installed
    font_manager.fontManager.addfont(str(f))
plt.style.use(HERE / "paper.mplstyle")
C = plt.rcParams["axes.prop_cycle"].by_key()["color"]
INK, GRAY = "#1a1a1a", "#8c8c8c"
BLUE, LGRAY, ORANGE = "#dce6f2", "#ececec", "#f7e6c8"
PHYSICAL, LOGICAL = 6, 12  # cores / hardware threads of the benchmark machine (Ryzen 5 5600)


def save(fig, name):
    fig.savefig(HERE / name, metadata={"Date": None})
    plt.close(fig)


def box(ax, x, y, w, h, text="", fc="white", ec=INK, fs=7.5, ls="-", lw=0.6, **kw):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=0.06",
                                fc=fc, ec=ec, lw=lw, ls=ls))
    if text:
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs, **kw)


def arrow(ax, p, q, color=INK, ls="-", lw=0.7, text=None, off=(0, 0.08), fs=6.8, ha="center"):
    ax.annotate("", xy=q, xytext=p, arrowprops=dict(arrowstyle="-|>", lw=lw, color=color, ls=ls, shrinkA=0,
                                                    shrinkB=0, mutation_scale=7))
    if text:
        ax.text((p[0] + q[0]) / 2 + off[0], (p[1] + q[1]) / 2 + off[1], text, fontsize=fs, ha=ha, va="bottom",
                color=color)


# ---- Figure 1: structure of the pool in src/thread-pool.cc
fig, ax = plt.subplots(figsize=(7.2, 3.3))
ax.set_xlim(0, 14.4)
ax.set_ylim(0, 6.6)
ax.set_aspect("equal")
ax.axis("off")
box(ax, 0.2, 4.4, 2.6, 1.3, "client threads\nschedule(thunk)", fc=LGRAY)
box(ax, 0.2, 0.5, 2.6, 1.3, "client thread\nwait()", fc=LGRAY)
box(ax, 4.0, 4.4, 3.0, 1.3, "taskQueue\n(FIFO, queueLock)", fc=ORANGE)
box(ax, 4.0, 2.45, 3.0, 1.3, "pendingTasks\n(taskCountLock)", fc=ORANGE)
box(ax, 8.2, 4.4, 2.4, 1.3, "dispatcher\nthread", fc=BLUE)
arrow(ax, (2.8, 5.05), (4.0, 5.05), text="push")
arrow(ax, (2.8, 4.55), (4.0, 3.45), text="+1", off=(0.25, 0.0))
arrow(ax, (7.0, 5.05), (8.2, 5.05), text="pop")
ax.plot([1.5, 1.5, 9.4], [5.7, 6.15, 6.15], color=C[0], lw=0.7)
arrow(ax, (9.4, 6.15), (9.4, 5.7), color=C[0])
ax.text(5.5, 6.22, "taskAvailable.signal()", fontsize=6.8, color=C[0], ha="center")
for k in range(3):
    y = 3.9 - k * 1.45
    box(ax, 11.6, y, 2.6, 1.05, "worker %s\nruns thunk" % ("0", "1", "N-1")[k], fc=BLUE)
    arrow(ax, (10.6, 4.9), (11.6, y + 0.52), color=C[0])
ax.text(11.0, 1.15, "…", fontsize=10, ha="center")
ax.text(8.2, 4.25, "copies the thunk to the first\nfree worker, ready.signal()", fontsize=6.6, color=C[0], ha="left",
        va="top")
arrow(ax, (11.6, 1.25), (7.0, 2.85), color=INK, text="−1 when done", off=(0.2, -0.45))
arrow(ax, (2.8, 1.15), (4.0, 2.6), color=C[1], ls=(0, (3, 2)), text="poll + yield\nuntil 0", off=(-0.15, -0.1),
      ha="right")
ax.text(8.2, 3.5, "spins with yield while\nno worker is free", fontsize=6.6, color=C[1], ha="left", va="top")
fig.tight_layout(pad=0.2)
save(fig, "fig1-architecture.svg")

# ---- Figure 2 + Table 2: speed-up and CPU use from bench.csv
d = pd.read_csv(HERE / "bench.csv")
g = d.groupby(["workload", "workers"]).agg(wall=("wall_s", "median"), cpu=("cpu_s", "median")).reset_index()
g["speedup"] = g.groupby("workload").wall.transform("first") / g.wall
g["busy"] = g.cpu / g.wall
fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.8))
labels = {"cpu": "compute-bound (240 × 4M sqrt)", "sleep": "sleep-bound (96 × 5 ms)"}
for (w, lab), c, m in zip(labels.items(), C, ("o", "s")):
    h = g[g.workload == w]
    axes[0].plot(h.workers, h.speedup, marker=m, color=c, label=lab, ms=3.5)
    axes[1].plot(h.workers, h.busy, marker=m, color=c, label=lab, ms=3.5)
axes[0].plot([1, 24], [1, 24], color=GRAY, lw=0.7, ls=":", label="ideal")
axes[1].axhline(LOGICAL, color=GRAY, lw=0.7, ls=":")
axes[0].set_ylim(0, 24)
axes[1].set_ylim(0, 13)
for ax in axes:
    for x, t in ((PHYSICAL, "6 cores"), (LOGICAL, "12 threads")):
        ax.axvline(x, color=GRAY, lw=0.5, ls="--")
        top = ax is axes[0]
        ax.text(x + 0.3, ax.get_ylim()[1] * (0.97 if top else 0.03), t, fontsize=6.8, color="#4d4d4d",
                va="top" if top else "bottom")
    ax.set_xlabel("worker threads")
    ax.set_xlim(0, 25)
axes[0].set_ylabel("speed-up over 1 worker")
axes[0].set_title("(a) Speed-up (median of 5 runs)")
axes[1].set_ylabel("CPU time / wall time")
axes[1].set_title("(b) Average busy hardware threads")
axes[0].legend(loc="upper left", bbox_to_anchor=(0.0, 0.88))
fig.tight_layout()
save(fig, "fig2-speedup.svg")

print("| Workers | cpu: wall [s] | cpu: speed-up | cpu: busy | sleep: wall [s] | sleep: speed-up | sleep: busy |")
for n in sorted(g.workers.unique()):
    a, b = (g[(g.workload == w) & (g.workers == n)].iloc[0] for w in ("cpu", "sleep"))
    print(f"| {n} | {a.wall:.3f} | {a.speedup:.2f} | {a.busy:.1f} | {b.wall:.3f} | {b.speedup:.2f} | {b.busy:.1f} |")
