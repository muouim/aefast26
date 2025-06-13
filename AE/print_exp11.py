import re
import csv

# List of baselines, workloads, compute nodes
baselines = ["dmtree", "fptree", "sherman", "smart", "rolex", "dlsm", "chime"]
workloads = ["ycsb-c", "insert-only", "update-only", "scan-only"]
nodes = ['node1', 'node2', 'node4', 'node5', 'node6', 'node7']

# Define the distribution
distribution = "zipfian"

# Regular expression to extract Transaction throughput (MOPS)
throughput_pattern = r'# Transaction throughput \(MOPS\):\s*(\d+\.\d+)'

# List to store the data for each Index and workload
data = []

# Iterate over each baseline and workload
for baseline in baselines:
    for workload in workloads:
        row = [baseline, workload]  # First columns are Index (baseline) and workload name
        
        total_throughput = 0.0  # Initialize total throughput for each baseline and workload
        
        # Iterate over nodes to collect the Transaction throughput (MOPS) data
        for node in nodes:
            # Include the distribution variable in the filename
            file_path = f"./AE/Data/{node}-exp0_{baseline}_{workload}-{distribution}-thread72-coro4.txt"
            throughput = 0.0
            
            try:
                with open(file_path, 'r') as file:
                    for line in file:
                        # Extract Transaction throughput (MOPS)
                        throughput_match = re.search(throughput_pattern, line)
                        if throughput_match:
                            throughput += float(throughput_match.group(1))
            except FileNotFoundError:
                print(f"File {file_path} not found.")
            
            # Add the throughput data for the current node to the row
            row.append(throughput)
            total_throughput += throughput
        
        # Insert the total throughput value before Node1
        row.insert(2, total_throughput)
        data.append(row)

# Save the data to a CSV file
output_file = 'simple_results.csv'
with open(output_file, 'w', newline='') as csvfile:
    writer = csv.writer(csvfile)
    # Write the header with Total before Node1
    writer.writerow(["Index", "Workload", "Total", "Node1", "Node2", "Node4", "Node5", "Node6", "Node7"])
    # Write the data
    writer.writerows(data)

print(f"Transaction throughput data has been saved to {output_file}")
