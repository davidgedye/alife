#!/usr/bin/env python3
"""
Plot soup stats TSV (output of ./soup_orig --stats N).

Usage:
    python3 plot_stats.py <stats.tsv> [output.png]

Columns (old): epoch  mean_ops  median_ops  unique_ids  modal_id  repr_tape (modal_count)
Columns (new): epoch  mean_ops  median_ops  mean_steps  max_steps  unique_ids  modal_id  repr_tape (modal_count)
"""

import sys
import re
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

def parse(path):
    epochs, mean_ops, median_ops, mean_steps, max_steps, unique_ids, modal_counts = \
        [], [], [], [], [], [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or not line[0].isdigit():
                continue
            parts = line.split('\t')
            parts = [p.strip() for p in parts]
            has_steps = len(parts) >= 8  # new format
            epochs.append(int(parts[0]))
            mean_ops.append(float(parts[1]))
            median_ops.append(float(parts[2]))
            if has_steps:
                mean_steps.append(float(parts[3]))
                max_steps.append(int(parts[4]))
                unique_ids.append(int(parts[5]))
                last = parts[7]
            else:
                mean_steps.append(float('nan'))
                max_steps.append(0)
                unique_ids.append(int(parts[3]))
                last = parts[5]
            m = re.search(r'\((\d+)\)\s*$', last)
            modal_counts.append(int(m.group(1)) if m else 0)
    return (np.array(epochs), np.array(mean_ops), np.array(median_ops),
            np.array(mean_steps), np.array(max_steps),
            np.array(unique_ids), np.array(modal_counts))

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    out  = sys.argv[2] if len(sys.argv) > 2 else re.sub(r'\.tsv$', '', path) + '_plot.png'

    ep, mean, med, msteps, xsteps, uniq, modal = parse(path)
    has_steps = not np.all(np.isnan(msteps))

    nrows = 4 if has_steps else 3
    fig, axes = plt.subplots(nrows, 1, figsize=(10, 3.5 * nrows), sharex=True)
    fig.suptitle('BFF-orig primordial soup — stats over epochs', fontsize=13)

    # --- mean / median ops ---
    ax = axes[0]
    ax.plot(ep, mean, label='mean ops', color='steelblue')
    ax.plot(ep, med,  label='median ops', color='orange', linestyle='--')
    ax.set_ylabel('ops per tape')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # --- mean / max steps ---
    if has_steps:
        ax = axes[1]
        ax.plot(ep, msteps, label='mean steps', color='steelblue')
        ax2 = ax.twinx()
        ax2.plot(ep, xsteps, label='max steps', color='tomato', alpha=0.6)
        ax2.set_ylabel('max steps', color='tomato')
        ax.set_ylabel('mean steps')
        ax.legend(loc='upper left')
        ax2.legend(loc='upper right')
        ax.grid(True, alpha=0.3)

    # --- unique IDs ---
    ax = axes[2 if has_steps else 1]
    ax.plot(ep, uniq, color='forestgreen')
    ax.set_ylabel('unique token IDs')
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(
        lambda x, _: f'{x/1e6:.1f}M' if x >= 1e5 else f'{int(x):,}'))
    ax.grid(True, alpha=0.3)

    # --- modal lineage count ---
    ax = axes[3 if has_steps else 2]
    ax.plot(ep, modal, color='crimson')
    ax.set_ylabel('modal lineage count')
    ax.set_xlabel('epoch')
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f'{int(x):,}'))
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(out, dpi=150)
    print(f'Saved to {out}')

if __name__ == '__main__':
    main()
