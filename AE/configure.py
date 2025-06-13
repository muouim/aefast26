#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Initialize configuration: 
1. If node_id < 0 (memory node), modify `dsmSize` in multiple `Common.h` files.
2. If node_id >= 0 (compute node), modify `COMPUTE_ID` in `ycsbc.cc` in some baselines (e.g., dLSM).
"""

import argparse
import os
import re

# ========== Parse input arguments ==========
parser = argparse.ArgumentParser(description='Update dsmSize or COMPUTE_ID based on node type.')
parser.add_argument('--node_id', type=int, required=True, help='-1 = memory node, >=0 = compute node')
parser.add_argument('--file_path', type=str, default='include/Common.h', help='Path to Common.h')
parser.add_argument('--dsm_size', type=int, default=150, help='New value for dsmSize (used if node_id < 0)')
args = parser.parse_args()

# ========== Target file list ==========
mn_target_files = [
    "Sherman/include/Common.h",
    "ROLEX/include/Common.h",
    "SMART/include/Common.h",
    "CHIME/include/Common.h",
]

ycsbc_file = "dLSM/benchmarks/ycsbc.cc"

# ========== Memory node: modify dsmSize ==========
if args.node_id < 0:
    pattern_dsm = re.compile(r'^(.*constexpr\s+uint64_t\s+dsmSize\s*=\s*)\d+(\s*;\s*//.*)?$')
    for file_path in mn_target_files:
        if not os.path.isfile(file_path):
            print(f"[WARN] File not found: {file_path}")
            continue

        modified = False
        new_lines = []
        with open(file_path, 'r', encoding='utf-8') as f:
            for line in f:
                match = pattern_dsm.match(line)
                if match:
                    new_line = f"{match.group(1)}{args.dsm_size}{match.group(2) or ''}\n"
                    new_lines.append(new_line)
                    modified = True
                else:
                    new_lines.append(line)

        with open(file_path, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)

        if modified:
            print(f"[INFO] Updated dsmSize to {args.dsm_size} in {file_path}")
        else:
            print(f"[INFO] No change to dsmSize in {file_path}")

# ========== Compute node: modify COMPUTE_ID ==========
elif args.node_id >= 0:
    compute_id = args.node_id * 2
    pattern_compute = re.compile(r'^(#define\s+COMPUTE_ID\s+)-?\d+(\s*//.*)?$')

    if not os.path.isfile(ycsbc_file):
        print(f"[ERROR] File not found: {ycsbc_file}")
    else:
        modified = False
        new_lines = []
        with open(ycsbc_file, 'r', encoding='utf-8') as f:
            for line in f:
                match = pattern_compute.match(line)
                if match:
                    new_line = f"{match.group(1)}{compute_id}{match.group(2) or ''}\n"
                    new_lines.append(new_line)
                    modified = True
                else:
                    new_lines.append(line)

        with open(ycsbc_file, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)

        if modified:
            print(f"[INFO] Updated COMPUTE_ID to {compute_id} in {ycsbc_file}")
        else:
            print(f"[INFO] No change to COMPUTE_ID in {ycsbc_file}")

# ========== Defensive check (should not trigger) ==========
else:
    print("[ERROR] Invalid node_id: must be -1 (memory) or >= 0 (compute)")
