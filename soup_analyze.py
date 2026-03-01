#!/usr/bin/env python3
"""Interactive analysis of BFF soup trace data.

Loads binary snapshots written by soup --trace-dir and lets you explore
the soup evolution epoch by epoch: inspect tapes, find dominant lineages,
trace ancestry through pair interactions, and step through BFF execution.

Usage:
  python3 soup_analyze.py <trace-dir>          # interactive REPL
  python3 soup_analyze.py <trace-dir> --auto   # automatic full analysis

Commands (in REPL):
  help                  Show this help
  epochs                List available epochs in trace
  stats [E]             Per-epoch stats for epoch E (or all)
  tape N [E]            Show tape N at epoch E (default: last)
  top E [K]             Top K pairs by step count in epoch E (default 10)
  pair N E              Show pair containing tape N in epoch E + before/after
  lineage [E]           Dominant token ID analysis at epoch E
  trace                 Trace dominant lineage back to origin epoch
  bff N E               Step-by-step BFF run of tape N's epoch-E interaction
  search PAT [E]        Find tapes matching instruction pattern at epoch E
  quit / exit           Exit
"""

import sys
import os
import struct
import argparse
import readline
from collections import Counter

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    print("Warning: numpy not available; loading will be slow", file=sys.stderr)
    HAS_NUMPY = False

# ── BFF constants ──────────────────────────────────────────────────────────────
BFF_TAPE_LEN   = 128
BFF_HALF_LEN   = 64
BFF_IP_START   = 2
BFF_MAX_STEPS  = 16384
BFF_STACK_DEPTH = 64
BFF_OPS        = set(b'<>+-,[]')

# ── Global config (overridden by metadata.txt) ─────────────────────────────────
CFG = dict(soup_size=131072, half_len=64, npairs=65536)

# ── Caches ──────────────────────────────────────────────────────────────────────
_soup_cache  = {}   # epoch -> np.ndarray (SOUP_SIZE, HALF_LEN) uint64
_perm_cache  = {}   # epoch -> np.ndarray SOUP_SIZE uint32
_steps_cache = {}   # epoch -> np.ndarray NPAIRS uint32

TRACE_DIR = None


# ── Loading ────────────────────────────────────────────────────────────────────

def load_metadata(trace_dir):
    path = os.path.join(trace_dir, "metadata.txt")
    if not os.path.exists(path):
        return
    with open(path) as f:
        for line in f:
            line = line.strip()
            if '=' in line:
                k, v = line.split('=', 1)
                try:
                    CFG[k] = int(v)
                except ValueError:
                    try:
                        CFG[k] = float(v)
                    except ValueError:
                        CFG[k] = v


def _bin_path(epoch, kind):
    return os.path.join(TRACE_DIR, f"epoch{epoch}_{kind}.bin")


def available_epochs():
    epochs = []
    for fn in os.listdir(TRACE_DIR):
        if fn.startswith("epoch") and fn.endswith("_soup.bin"):
            try:
                epochs.append(int(fn[5:fn.index("_soup")]))
            except ValueError:
                pass
    return sorted(epochs)


def load_soup(epoch):
    if epoch not in _soup_cache:
        path = _bin_path(epoch, "soup")
        if not os.path.exists(path):
            print(f"  No soup snapshot for epoch {epoch}")
            return None
        if HAS_NUMPY:
            arr = np.fromfile(path, dtype=np.uint64)
            _soup_cache[epoch] = arr.reshape(CFG['soup_size'], CFG['half_len'])
        else:
            sz = CFG['soup_size'] * CFG['half_len']
            with open(path, 'rb') as f:
                data = struct.unpack(f"{sz}Q", f.read(sz * 8))
            import array
            _soup_cache[epoch] = data  # flat tuple fallback
    return _soup_cache[epoch]


def load_perm(epoch):
    if epoch not in _perm_cache:
        path = _bin_path(epoch, "perm")
        if not os.path.exists(path):
            return None
        if HAS_NUMPY:
            _perm_cache[epoch] = np.fromfile(path, dtype=np.uint32)
        else:
            sz = CFG['soup_size']
            with open(path, 'rb') as f:
                _perm_cache[epoch] = struct.unpack(f"{sz}I", f.read(sz * 4))
    return _perm_cache[epoch]


def load_steps(epoch):
    if epoch not in _steps_cache:
        path = _bin_path(epoch, "steps")
        if not os.path.exists(path):
            return None
        if HAS_NUMPY:
            _steps_cache[epoch] = np.fromfile(path, dtype=np.uint32)
        else:
            sz = CFG['npairs']
            with open(path, 'rb') as f:
                _steps_cache[epoch] = struct.unpack(f"{sz}I", f.read(sz * 4))
    return _steps_cache[epoch]


# ── Token field extraction ─────────────────────────────────────────────────────

def tok_char(t):
    return int(t) & 0xFF

def tok_id(t):
    return (int(t) >> 32) & 0xFFFFFFFF

def tok_epoch(t):
    return (int(t) >> 16) & 0xFFFF

def is_op(ch):
    return ch in BFF_OPS

def tape_str(half_tape, mark_ops=True):
    """Format a HALF_LEN token array as a string (op char or '.' for data)."""
    chars = []
    for t in half_tape:
        ch = tok_char(t)
        if mark_ops and ch in BFF_OPS:
            chars.append(chr(ch))
        else:
            chars.append('.')
    return ''.join(chars)

def tape_str_full(half_tape):
    """Show all bytes as printable hex pairs, ops highlighted."""
    out = []
    for t in half_tape:
        ch = tok_char(t)
        if ch in BFF_OPS:
            out.append(f"\033[1;32m{chr(ch)}\033[0m ")
        else:
            out.append(f"{ch:02x} ")
    return ''.join(out)

def count_ops(half_tape):
    return sum(1 for t in half_tape if tok_char(t) in BFF_OPS)


# ── Stats ──────────────────────────────────────────────────────────────────────

def compute_stats(epoch):
    soup = load_soup(epoch)
    if soup is None:
        return None
    ss = CFG['soup_size']
    hl = CFG['half_len']
    if HAS_NUMPY:
        chars = (soup & 0xFF).astype(np.uint8)
        op_mask = np.zeros(256, dtype=bool)
        for b in BFF_OPS:
            op_mask[b] = True
        ops_per_tape = op_mask[chars].sum(axis=1)
        mean_ops   = float(ops_per_tape.mean())
        median_ops = float(np.median(ops_per_tape))

        ids = (soup >> 32).astype(np.uint32).flatten()
        unique = int(np.unique(ids).size)
        counter = Counter(ids.tolist())
        modal_id, modal_count = counter.most_common(1)[0]

        # epoch field distribution of the modal ID tokens
        modal_mask = (soup >> 32).astype(np.uint32) == modal_id
        modal_epochs = ((soup[modal_mask] >> 16) & 0xFFFF).tolist()
        epoch_dist = Counter(modal_epochs)
    else:
        ops_per_tape = [count_ops(soup[i*hl:(i+1)*hl]) for i in range(ss)]
        mean_ops   = sum(ops_per_tape) / ss
        sorted_ops = sorted(ops_per_tape)
        median_ops = (sorted_ops[ss//2-1] + sorted_ops[ss//2]) / 2
        all_ids    = [tok_id(t) for row in soup for t in row]
        unique     = len(set(all_ids))
        counter    = Counter(all_ids)
        modal_id, modal_count = counter.most_common(1)[0]
        epoch_dist = {}

    return dict(epoch=epoch, mean_ops=mean_ops, median_ops=median_ops,
                unique=unique, modal_id=modal_id, modal_count=modal_count,
                epoch_dist=epoch_dist)


# ── Display helpers ────────────────────────────────────────────────────────────

def show_tape(tape_idx, epoch):
    soup = load_soup(epoch)
    if soup is None:
        return
    hl = CFG['half_len']
    half = soup[tape_idx]
    ops = count_ops(half)
    print(f"\n  Tape {tape_idx} @ epoch {epoch}  ({ops} ops / {hl} cells)")
    print(f"  Instr: |{tape_str(half)}|")
    print(f"  Bytes: |{tape_str_full(half)}|")
    # Token ID distribution
    if HAS_NUMPY:
        ids = (half >> 32).astype(np.uint32)
        id_counts = Counter(ids.tolist())
    else:
        id_counts = Counter(tok_id(t) for t in half)
    top_ids = id_counts.most_common(4)
    print(f"  Top token IDs: " +
          ", ".join(f"id={i} ×{c}" for i, c in top_ids))
    # Per-cell detail
    print(f"\n  idx  op  char  token_id    tok_epoch")
    for j, t in enumerate(half):
        ch  = tok_char(t)
        tid = tok_id(t)
        tep = tok_epoch(t)
        op  = chr(ch) if ch in BFF_OPS else f"0x{ch:02x}"
        print(f"  {j:3d}  {op:<4}  {ch:3d}  {tid:>10}  {tep:>9}")


def show_pair(tape_idx, epoch):
    """Show the pair containing tape_idx in epoch, and before/after tapes."""
    perm  = load_perm(epoch)
    steps = load_steps(epoch)
    if perm is None or steps is None:
        print(f"  No pairing data for epoch {epoch}")
        return
    npairs = CFG['npairs']

    # Find pair index
    pair_i = None
    if HAS_NUMPY:
        pa = perm[:npairs]
        pb = perm[npairs:]
        idxs_a = np.where(pa == tape_idx)[0]
        idxs_b = np.where(pb == tape_idx)[0]
        if len(idxs_a):
            pair_i = int(idxs_a[0]); role = 'A'
        elif len(idxs_b):
            pair_i = int(idxs_b[0]); role = 'B'
    else:
        for i in range(npairs):
            if perm[i] == tape_idx:
                pair_i = i; role = 'A'; break
            if perm[i + npairs] == tape_idx:
                pair_i = i; role = 'B'; break

    if pair_i is None:
        print(f"  Tape {tape_idx} not found in epoch {epoch} pairing?")
        return

    ai = int(perm[pair_i])
    bi = int(perm[pair_i + npairs])
    st = int(steps[pair_i])
    print(f"\n  Epoch {epoch}: pair {pair_i}  A={ai}  B={bi}  steps={st}  (tape {tape_idx} was {role})")

    # Before state (epoch-1 soup)
    prev = epoch - 1
    soup_before = load_soup(prev)
    soup_after  = load_soup(epoch)
    hl = CFG['half_len']

    for label, tidx in [('A', ai), ('B', bi)]:
        print(f"\n  ── Tape {tidx} ({label}) ──")
        if soup_before is not None:
            print(f"     before: |{tape_str(soup_before[tidx])}|")
        if soup_after is not None:
            print(f"     after:  |{tape_str(soup_after[tidx])}|")


def show_top_pairs(epoch, k=10):
    steps = load_steps(epoch)
    perm  = load_perm(epoch)
    if steps is None or perm is None:
        print(f"  No data for epoch {epoch}")
        return
    npairs = CFG['npairs']
    soup_before = load_soup(epoch - 1)
    soup_after  = load_soup(epoch)

    if HAS_NUMPY:
        top_idx = np.argsort(steps)[::-1][:k].tolist()
    else:
        top_idx = sorted(range(npairs), key=lambda i: steps[i], reverse=True)[:k]

    print(f"\n  Top {k} pairs by step count in epoch {epoch}:")
    print(f"  {'pair':>6}  {'A':>7}  {'B':>7}  {'steps':>7}  A-before / A-after  B-before / B-after")
    for pi in top_idx:
        ai  = int(perm[pi])
        bi  = int(perm[pi + npairs])
        st  = int(steps[pi])
        def s(soup, idx):
            return tape_str(soup[idx]) if soup is not None else '?'*64
        print(f"  {pi:>6}  {ai:>7}  {bi:>7}  {st:>7}"
              f"  |{s(soup_before,ai)[:20]}…|→|{s(soup_after,ai)[:20]}…|"
              f"  |{s(soup_before,bi)[:20]}…|→|{s(soup_after,bi)[:20]}…|")


def show_lineage(epoch):
    st = compute_stats(epoch)
    if st is None:
        return
    print(f"\n  Epoch {epoch} lineage:")
    print(f"    Mean ops:    {st['mean_ops']:.3f}")
    print(f"    Median ops:  {st['median_ops']:.1f}")
    print(f"    Unique IDs:  {st['unique']:,}")
    print(f"    Modal ID:    {st['modal_id']}  (appears in {st['modal_count']:,} cells)")
    if st['epoch_dist']:
        print(f"    Modal token birth epochs: " +
              ", ".join(f"ep{k}×{v}" for k, v in sorted(st['epoch_dist'].items())))

    # Show the best representative tape
    soup = load_soup(epoch)
    if soup is not None and HAS_NUMPY:
        mid = st['modal_id']
        ids = (soup >> 32).astype(np.uint32)
        counts = (ids == mid).sum(axis=1)
        best_tape = int(counts.argmax())
        print(f"    Best tape for modal ID: tape {best_tape} ({int(counts[best_tape])} cells with modal ID)")
        print(f"      Instr: |{tape_str(soup[best_tape])}|")


def trace_lineage():
    """Trace dominant token from final epoch back to its first appearance."""
    epochs = available_epochs()
    if not epochs:
        print("  No epochs available"); return
    final = epochs[-1]

    st = compute_stats(final)
    if st is None:
        return
    mid = st['modal_id']
    print(f"\n  Tracing modal ID {mid} (×{st['modal_count']:,} cells at epoch {final}) backwards...\n")

    soup_final = load_soup(final)
    if soup_final is None:
        return

    # Show tape with most of modal ID at each epoch
    for ep in epochs:
        soup = load_soup(ep)
        if soup is None:
            continue
        if HAS_NUMPY:
            ids    = (soup >> 32).astype(np.uint32)
            counts = (ids == mid).sum(axis=1)
            total  = int(counts.sum())
            if total == 0:
                print(f"  epoch {ep:3d}: ID {mid} not present")
                continue
            best = int(counts.argmax())
            n    = int(counts[best])
            instr = tape_str(soup[best])
        else:
            total = 0; best = 0; n = 0; instr = '?'*64
        perm  = load_perm(ep)
        steps = load_steps(ep)
        step_info = ""
        if perm is not None and steps is not None:
            npairs = CFG['npairs']
            perm_a = perm[:npairs]; perm_b = perm[npairs:]
            if HAS_NUMPY:
                idxs_a = np.where(perm_a == best)[0]
                idxs_b = np.where(perm_b == best)[0]
                if len(idxs_a):
                    pi = int(idxs_a[0])
                    partner = int(perm_b[pi])
                    step_info = f"  paired with {partner}, {int(steps[pi])} steps"
                elif len(idxs_b):
                    pi = int(idxs_b[0])
                    partner = int(perm_a[pi])
                    step_info = f"  paired with {partner}, {int(steps[pi])} steps"
        print(f"  epoch {ep:3d}: {total:>8,} cells  best tape={best:6d} ({n:2d} cells)  |{instr}|{step_info}")


# ── BFF step-by-step tracer ────────────────────────────────────────────────────

def bff_trace(tape_a, tape_b, max_steps=200, verbose=True):
    """
    Run BFF on combined tape A||B and print each instruction.
    tape_a, tape_b: lists/arrays of HALF_LEN uint64 tokens.
    Returns (final_tape_a, final_tape_b, steps_taken).
    """
    hl = CFG['half_len']
    tape = list(tape_a) + list(tape_b)   # 128 tokens

    ip    = BFF_IP_START
    head0 = tok_char(tape[0]) & (BFF_TAPE_LEN - 1)
    head1 = tok_char(tape[1]) & (BFF_TAPE_LEN - 1)
    stack = []
    steps = 0

    if verbose:
        print(f"\n  BFF trace: head0={head0}, head1={head1}, ip={ip}")
        print(f"  {'step':>5}  {'ip':>3}  {'op':>4}  {'head0':>5}  {'head1':>5}  effect")

    def note(msg):
        if verbose:
            print(f"  {steps:>5}  {ip:>3}  {op_ch:>4}  {head0:>5}  {head1:>5}  {msg}")

    while steps < min(max_steps, BFF_MAX_STEPS):
        steps += 1
        ch = tok_char(tape[ip])
        op_ch = chr(ch) if ch in BFF_OPS else f"0x{ch:02x}"

        if ch == ord('<'):
            head0 = (head0 - 1) & (BFF_TAPE_LEN - 1)
            note(f"head0 → {head0}")
        elif ch == ord('>'):
            head0 = (head0 + 1) & (BFF_TAPE_LEN - 1)
            note(f"head0 → {head0}")
        elif ch == ord('+'):
            old = tok_char(tape[head0])
            new = (old + 1) & 0xFF
            tape[head0] = (tape[head0] & ~0xFF) | new
            note(f"tape[{head0}] char {old}→{new}")
        elif ch == ord('-'):
            old = tok_char(tape[head0])
            new = (old - 1) & 0xFF
            tape[head0] = (tape[head0] & ~0xFF) | new
            note(f"tape[{head0}] char {old}→{new}")
        elif ch == ord(','):
            src_id = tok_id(tape[head0])
            tape[head1] = tape[head0]
            note(f"tape[{head1}] ← tape[{head0}] (id={src_id}, ch={tok_char(tape[head0])}); head1 → {(head1+1)&127}")
            head1 = (head1 + 1) & (BFF_TAPE_LEN - 1)
        elif ch == ord('['):
            if len(stack) >= BFF_STACK_DEPTH:
                note("stack overflow → HALT")
                break
            stack.append(ip)
            note(f"push ip={ip}  (depth={len(stack)})")
        elif ch == ord(']'):
            if not stack:
                note("empty stack → HALT")
                break
            val = tok_char(tape[head0])
            if val != 0:
                ip = stack[-1]
                note(f"loop (tape[{head0}]={val} ≠ 0) → ip={ip}")
            else:
                stack.pop()
                note(f"exit loop (tape[{head0}]=0)  (depth={len(stack)})")
        else:
            note(f"nop (0x{ch:02x})")

        ip = (ip + 1) & (BFF_TAPE_LEN - 1)

    if steps >= max_steps and verbose:
        print(f"  ... stopped at step limit {max_steps}")

    if verbose:
        print(f"\n  Final tape A: |{tape_str(tape[:hl])}|")
        print(f"  Final tape B: |{tape_str(tape[hl:])}|")

    return tape[:hl], tape[hl:], steps


def show_bff_trace(tape_idx, epoch, max_steps=200):
    """Run step-by-step BFF trace for the pair containing tape_idx at epoch."""
    perm  = load_perm(epoch)
    steps = load_steps(epoch)
    if perm is None:
        print(f"  No pairing data for epoch {epoch}"); return
    npairs = CFG['npairs']

    pair_i = None
    if HAS_NUMPY:
        pa = perm[:npairs]; pb = perm[npairs:]
        ia = np.where(pa == tape_idx)[0]
        ib = np.where(pb == tape_idx)[0]
        if len(ia): pair_i = int(ia[0]); role = 'A'
        elif len(ib): pair_i = int(ib[0]); role = 'B'
    else:
        for i in range(npairs):
            if perm[i] == tape_idx: pair_i = i; role = 'A'; break
            if perm[i+npairs] == tape_idx: pair_i = i; role = 'B'; break

    if pair_i is None:
        print(f"  Tape {tape_idx} not in epoch {epoch} pairs"); return

    ai = int(perm[pair_i])
    bi = int(perm[pair_i + npairs])
    st = int(steps[pair_i]) if steps is not None else '?'
    print(f"\n  Epoch {epoch}: pair {pair_i}  A={ai}  B={bi}  steps={st}")

    soup_before = load_soup(epoch - 1)
    if soup_before is None:
        print(f"  Need soup snapshot for epoch {epoch-1}"); return

    tape_a = soup_before[ai].tolist() if HAS_NUMPY else soup_before[ai]
    tape_b = soup_before[bi].tolist() if HAS_NUMPY else soup_before[bi]
    print(f"  A before: |{tape_str(tape_a)}|")
    print(f"  B before: |{tape_str(tape_b)}|")
    bff_trace(tape_a, tape_b, max_steps=max_steps)


def search_tapes(pattern, epoch):
    """Find tapes whose instruction string contains pattern at given epoch."""
    soup = load_soup(epoch)
    if soup is None:
        return
    matches = []
    ss = CFG['soup_size']
    for i in range(ss):
        s = tape_str(soup[i])
        if pattern in s:
            matches.append((i, s))
    if not matches:
        print(f"  No tapes matching '{pattern}' at epoch {epoch}")
        return
    print(f"  {len(matches)} tapes matching '{pattern}' at epoch {epoch}:")
    for idx, s in matches[:20]:
        print(f"    tape {idx:6d}: |{s}|")
    if len(matches) > 20:
        print(f"    ... ({len(matches)-20} more)")


# ── Auto analysis ─────────────────────────────────────────────────────────────

def auto_analyze():
    epochs = available_epochs()
    if not epochs:
        print("No trace data found."); return
    print(f"=== Soup Trace Analysis: {TRACE_DIR} ===")
    print(f"Epochs available: {epochs}\n")

    # Per-epoch stats
    print("─── Per-epoch stats ───────────────────────────────────────")
    print(f"  {'epoch':>5}  {'mean_ops':>9}  {'median':>7}  {'unique_ids':>11}  {'modal_count':>12}")
    for ep in epochs:
        st = compute_stats(ep)
        if st:
            print(f"  {ep:>5}  {st['mean_ops']:>9.3f}  {st['median_ops']:>7.1f}"
                  f"  {st['unique']:>11,}  {st['modal_count']:>12,}")

    # Top pairs per epoch
    for ep in epochs[1:]:
        print(f"\n─── Top 5 pairs by step count: epoch {ep} ──────────────────")
        show_top_pairs(ep, k=5)

    # Lineage trace
    print(f"\n─── Lineage trace ──────────────────────────────────────────")
    trace_lineage()

    # BFF trace of most active pair in last non-zero epoch
    last_ep = epochs[-1]
    steps = load_steps(last_ep)
    if steps is not None:
        if HAS_NUMPY:
            best_pair = int(np.argmax(steps))
        else:
            best_pair = max(range(len(steps)), key=lambda i: steps[i])
        perm = load_perm(last_ep)
        ai = int(perm[best_pair])
        print(f"\n─── Step-by-step BFF trace: top pair in epoch {last_ep} (tape {ai}) ──")
        show_bff_trace(ai, last_ep, max_steps=100)


# ── REPL ──────────────────────────────────────────────────────────────────────

def repl():
    print(__doc__)
    print(f"Trace directory: {TRACE_DIR}")
    print(f"Config: {CFG}")
    print(f"Available epochs: {available_epochs()}")
    print("Type 'help' for commands.\n")

    while True:
        try:
            line = input("soup> ").strip()
        except (EOFError, KeyboardInterrupt):
            print(); break
        if not line:
            continue
        parts = line.split()
        cmd   = parts[0].lower()

        try:
            if cmd in ('quit', 'exit', 'q'):
                break
            elif cmd == 'help':
                print(__doc__)
            elif cmd == 'epochs':
                print(f"  Available: {available_epochs()}")
            elif cmd == 'stats':
                eps = [int(parts[1])] if len(parts) > 1 else available_epochs()
                for ep in eps:
                    show_lineage(ep)
            elif cmd == 'tape':
                n  = int(parts[1])
                ep = int(parts[2]) if len(parts) > 2 else available_epochs()[-1]
                show_tape(n, ep)
            elif cmd == 'top':
                ep = int(parts[1])
                k  = int(parts[2]) if len(parts) > 2 else 10
                show_top_pairs(ep, k)
            elif cmd == 'pair':
                n  = int(parts[1])
                ep = int(parts[2])
                show_pair(n, ep)
            elif cmd == 'lineage':
                ep = int(parts[1]) if len(parts) > 1 else available_epochs()[-1]
                show_lineage(ep)
            elif cmd == 'trace':
                trace_lineage()
            elif cmd == 'bff':
                n  = int(parts[1])
                ep = int(parts[2])
                ms = int(parts[3]) if len(parts) > 3 else 200
                show_bff_trace(n, ep, max_steps=ms)
            elif cmd == 'search':
                pat = parts[1]
                ep  = int(parts[2]) if len(parts) > 2 else available_epochs()[-1]
                search_tapes(pat, ep)
            else:
                print(f"  Unknown command: {cmd}  (type 'help')")
        except (IndexError, ValueError) as e:
            print(f"  Error: {e}")


# ── Entry point ────────────────────────────────────────────────────────────────

def main():
    global TRACE_DIR
    parser = argparse.ArgumentParser(description="Soup trace analyzer")
    parser.add_argument("trace_dir", help="Directory written by soup --trace-dir")
    parser.add_argument("--auto", action="store_true", help="Run automatic analysis and exit")
    args = parser.parse_args()

    TRACE_DIR = args.trace_dir
    load_metadata(TRACE_DIR)

    if args.auto:
        auto_analyze()
    else:
        repl()


if __name__ == '__main__':
    main()
