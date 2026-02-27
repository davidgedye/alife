#!/usr/bin/env python3
"""
Plot unique token count vs epoch for multiple soup stats files on the same axes.

Usage:
    python3 plot_tokens_compare.py <file1:label1> <file2:label2> ... [--out output.png]
"""

import sys
import numpy as np
import matplotlib.pyplot as plt

def load(path):
    epochs, unique = [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('epoch'):
                continue
            parts = line.split()
            epochs.append(int(parts[0]))
            unique.append(int(parts[3]))
    return np.array(epochs), np.array(unique)

def main():
    args = sys.argv[1:]
    out_path = 'soup_tokens_compare.png'
    if '--out' in args:
        i = args.index('--out')
        out_path = args[i + 1]
        args = args[:i] + args[i + 2:]

    if not args:
        print(__doc__)
        sys.exit(1)

    entries = []
    for a in args:
        if ':' in a:
            path, label = a.split(':', 1)
        else:
            path, label = a, a
        entries.append((path, label))

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    for path, label in entries:
        epochs, unique = load(path)
        for ax in axes:
            ax.plot(epochs, unique, linewidth=1.4, label=label)

    for ax, yscale, title in [
        (axes[0], 'linear', 'Unique token IDs vs epoch (linear)'),
        (axes[1], 'log',    'Unique token IDs vs epoch (log scale)'),
    ]:
        ax.set_yscale(yscale)
        ax.set_xlabel('Epoch')
        ax.set_ylabel('Unique token IDs')
        ax.set_title(title)
        ax.axhline(1, color='red', linestyle='--', linewidth=0.8, label='1 lineage')
        ax.legend()
        ax.grid(True, alpha=0.3)

    fig.suptitle('BFF soup — lineage diversity (seed=12345)')
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"Saved to {out_path}")

if __name__ == '__main__':
    main()
