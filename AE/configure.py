#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Initialize configuration: 
1. If node_id < 0 (memory node), modify `dsmSize` in multiple `Common.h` files.
   For ROLEX, force dsmSize = 80 and modelRegionSize = 4 * GB.
   For SMART, force dsmSize = 145.
   Otherwise, set dsmSize = 110.
   Additionally, modify `#define HASH_TABLE_SIZE` in `CHIME/include/Hash.h` to 200000.
2. If node_id >= 0 (compute node), modify `COMPUTE_ID` in `ycsbc.cc` in some baselines (e.g., dLSM).
"""

import argparse
import os
import re

# ========== Parse input arguments ==========
parser = argparse.ArgumentParser(description='Update dsmSize, HASH_TABLE_SIZE, or COMPUTE_ID based on node type.')
parser.add_argument('--node_id', type=int, required=True, help='-1 = memory node, >=0 = compute node')
parser.add_argument('--file_path', type=str, default='include/Common.h', help='Path to Common.h')
parser.add_argument('--dsm_size', type=int, default=110, help='New value for dsmSize (used if node_id < 0)')
args = parser.parse_args()

# ========== Target file list ==========
mn_target_files = [
    "Sherman/include/Common.h",
    "ROLEX/include/Common.h",
    "SMART/include/Common.h",
    "CHIME/include/Common.h",
]

ycsbc_file = "dLSM/benchmarks/ycsbc.cc"
hash_file = "CHIME/include/Hash.h"  # CHIME Hash.h file to modify HASH_TABLE_SIZE

# ========== Memory node: modify dsmSize and memory configuration==========
if args.node_id < 0:
    pattern_dsm = re.compile(r'^(.*constexpr\s+uint64_t\s+dsmSize\s*=\s*)\d+(\s*;\s*//.*)?$')
    # Adjust memory configuration on memory nodes to minimize non-essential memory usage
    pattern_index_cache = re.compile(r'^(.*constexpr\s+int\s+kIndexCacheSize\s*=\s*)(.+?)(\s*;\s*(//.*)?)?\s*$')
    pattern_model_region = re.compile(r'^(.*constexpr\s+uint64_t\s+modelRegionSize\s*=\s*)(.+?)(\s*\*\s*GB\s*;\s*(//.*)?)?$')
    pattern_hash_table_size = re.compile(r'^(#define\s+HASH_TABLE_SIZE\s+)\d+(\s*//.*)?$')

    for file_path in mn_target_files:
        if not os.path.isfile(file_path):
            print(f"[WARN] File not found: {file_path}")
            continue

        is_rolex = "ROLEX" in file_path
        is_smart = "SMART" in file_path
        modified = False
        new_lines = []
        with open(file_path, 'r', encoding='utf-8') as f:
            for line in f:
                if (m := pattern_dsm.match(line)):
                    # Set dsmSize based on file type
                    if is_rolex:
                        dsm_value = 80
                    elif is_smart:
                        dsm_value = 145
                    else:
                        dsm_value = args.dsm_size
                    new_lines.append(f"{m.group(1)}{dsm_value}{m.group(2) or ''}\n")
                    modified = True
                elif (m := pattern_index_cache.match(line)):
                    new_lines.append(f"{m.group(1)}1024 * 1;{f' {m.group(4)}' if m.group(4) else ''}\n")
                    modified = True
                elif is_rolex and (m := pattern_model_region.match(line)):
                    new_lines.append(f"{m.group(1)}4 * GB;{f' // {m.group(4)}' if m.group(4) else ''}\n")
                    modified = True
                else:
                    new_lines.append(line)

        with open(file_path, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)

        if modified:
            print(f"[INFO] Updated dsmSize and/or kIndexCacheSize in {file_path}")
        else:
            print(f"[INFO] No change in {file_path}")

    # Modify HASH_TABLE_SIZE in CHIME/include/Hash.h
    if os.path.isfile(hash_file):
        with open(hash_file, 'r', encoding='utf-8') as f:
            hash_file_lines = f.readlines()

        modified = False
        new_hash_file_lines = []
        for line in hash_file_lines:
            if (m := pattern_hash_table_size.match(line)):
                new_hash_file_lines.append(f"{m.group(1)}200000{m.group(2) or ''}\n")
                modified = True
            else:
                new_hash_file_lines.append(line)

        if modified:
            with open(hash_file, 'w', encoding='utf-8') as f:
                f.writelines(new_hash_file_lines)
            print(f"[INFO] Updated HASH_TABLE_SIZE in {hash_file}")
        else:
            print(f"[INFO] No change to HASH_TABLE_SIZE in {hash_file}")

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
