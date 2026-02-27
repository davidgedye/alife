#!/usr/bin/env python3
"""
Plot unique token count vs epoch from soup stats output.

Usage:
    python3 plot_tokens.py <stats.tsv> [output.png]

The stats file is the stdout of soup (tab-separated: epoch, mean_ops,
median_ops, unique_ids).
"""

import sys
import numpy as np
import matplotlib.pyplot as plt

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    tsv_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else tsv_path.replace('.tsv', '') + '_tokens.png'

    epochs, unique = [], []
    with open(tsv_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('epoch'):
                continue
            parts = line.split()
            epochs.append(int(parts[0]))
            unique.append(int(parts[3]))

    epochs = np.array(epochs)
    unique = np.array(unique)

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    for ax, yscale, title in [
        (axes[0], 'linear', 'Unique token IDs vs epoch (linear)'),
        (axes[1], 'log',    'Unique token IDs vs epoch (log scale)'),
    ]:
        ax.plot(epochs, unique, color='steelblue', linewidth=1.2)
        ax.set_xlabel('Epoch')
        ax.set_ylabel('Unique token IDs')
        ax.set_yscale(yscale)
        ax.set_title(title)
        ax.axhline(1, color='red', linestyle='--', linewidth=0.8, label='1 lineage')
        ax.legend()
        ax.grid(True, alpha=0.3)

    fig.suptitle('BFF soup — lineage diversity (seed=12345, mutation=1e-6)')
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"Saved to {out_path}")

if __name__ == '__main__':
    main()
