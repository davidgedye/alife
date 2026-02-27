#!/usr/bin/env python3
"""
Plot run-length distribution over epochs from a soup binary runlog.

Usage:
    python3 plot.py <runlog.bin> [output.png]

The runlog is a flat binary stream of uint32_t values written by soup with
--runlog. Each epoch contributes NPAIRS consecutive values (run lengths in
steps). Epoch number is inferred from position in the file.
"""

import sys
import numpy as np
import matplotlib.pyplot as plt

NPAIRS = 65536  # must match NPAIRS in soup.c

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    runlog_path = sys.argv[1]
    out_path    = sys.argv[2] if len(sys.argv) > 2 else runlog_path + '.png'

    data = np.fromfile(runlog_path, dtype=np.uint32)
    n_epochs = len(data) // NPAIRS
    if n_epochs == 0:
        print("No complete epochs found in file.")
        sys.exit(1)

    steps  = data[:n_epochs * NPAIRS]
    epochs = np.repeat(np.arange(1, n_epochs + 1), NPAIRS)

    fig, axes = plt.subplots(1, 2, figsize=(16, 6))

    for ax, max_epoch, label in [
        (axes[0], n_epochs,  f'All {n_epochs} epochs'),
        (axes[1], min(30, n_epochs), 'First 30 epochs (zoom)'),
    ]:
        mask = epochs <= max_epoch
        hb = ax.hexbin(epochs[mask], steps[mask],
                       gridsize=(min(max_epoch, 300), 200),
                       bins='log', cmap='inferno', mincnt=1)
        fig.colorbar(hb, ax=ax, label='log₁₀(count)')
        ax.set_xlabel('Epoch')
        ax.set_ylabel('Run length (steps)')
        ax.set_title(label)

    fig.suptitle(f'BFF soup run-length distribution ({NPAIRS} pairs/epoch)')
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"Saved to {out_path}")
    plt.show()

if __name__ == '__main__':
    main()
