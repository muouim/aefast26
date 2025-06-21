import re
import csv
import os

print("========== Start Parsing ==========")

# List of baselines, workloads, compute nodes
baselines = ["dmtree", "fptree", "sherman", "smart", "rolex", "dlsm", "chime"]
workloads = ["ycsb-a", "ycsb-b", "ycsb-c", "ycsb-d", "ycsb-e", "ycsb-f"]
nodes = ['node1', 'node2', 'node4', 'node5', 'node6', 'node7']

# List of distributions
distributions = ["zipfian", "uniform"]

# Regular expression to extract Transaction throughput (MOPS)
throughput_pattern = r'# Transaction throughput \(MOPS\):\s*(\d+\.\d+)'

for distribution in distributions:
    print(f"--- Parsing for distribution: {distribution} ---")
    data = []  # Reset for each distribution

    for baseline in baselines:
        all_files_found = True  # Assume all files exist for this baseline

        for workload in workloads:
            row = [baseline, workload]  # First columns are Index and workload name
            total_throughput = 0.0  # Initialize total throughput

            for node in nodes:
                file_path = f"./AE/Data/{node}-exp0_{baseline}_{workload}-{distribution}-thread72-coro4.txt"
                throughput = 0.0

                if not os.path.exists(file_path):
                    all_files_found = False
                    continue

                with open(file_path, 'r') as file:
                    for line in file:
                        match = re.search(throughput_pattern, line)
                        if match:
                            throughput += float(match.group(1))

                row.append(throughput)
                total_throughput += throughput

            # Insert total throughput before node columns
            row.insert(2, total_throughput)
            data.append(row)

        if all_files_found:
            print(f"Successfully parsed all workloads for baseline: {baseline}")

    # Save to CSV for the current distribution
    output_file = f'ycsb_results_{distribution}.csv'
    with open(output_file, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["Index", "Workload", "Total", "Node1", "Node2", "Node4", "Node5", "Node6", "Node7"])
        writer.writerows(data)

    print(f"--> Saved result to {output_file}")

print("======================================")
print("Finished parsing all distributions")
print("======================================")
