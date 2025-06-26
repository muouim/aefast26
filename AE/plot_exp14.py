import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import FancyArrowPatch
from mpl_toolkits.axes_grid1.inset_locator import mark_inset, inset_axes
from matplotlib import rcParams, font_manager

font_prop = font_manager.FontProperties(family='DejaVu Serif')
rcParams['font.family'] = font_prop.get_name()

def runtime(distribution):
    # Load the CSV file with performance data
    df = pd.read_csv(f'ycsb_results_{distribution}.csv')

    # Define the baseline order and workloads to plot
    baselines = ["sherman", "dlsm", "rolex", "smart", "chime", "dmtree"]
    workloads = ["ycsb-a", "ycsb-b", "ycsb-c", "ycsb-d", "ycsb-e", "ycsb-f"]
    workload_labels = ['A', 'B', 'C', 'D', 'E', 'F']

    bar_colors = ['#1D3557', '#FABB6E', '#457B9D', '#A8DADB', '#497D74', '#E73847']
    hatchs = ['xxx','|||', '---', '\\\\\\', '///', '***']

    fig, ax = plt.subplots(1, 1, figsize=(7, 3), dpi=120)
    width = 0.8
    x_positions = [0.5 + i * (len(baselines) + 1) + np.arange(len(baselines)) for i in range(len(workloads))]
    bars = {}

    # Draw grouped bars for each workload
    for w_idx, workload in enumerate(workloads):
        x = x_positions[w_idx]
        for b_idx, baseline in enumerate(baselines):
            val = df[(df['Index'] == baseline) & (df['Workload'] == workload)]['Total']
            if not val.empty:
                height = val.values[0]
                bar = ax.bar(x[b_idx], height, width,
                             color=bar_colors[b_idx],
                             hatch=hatchs[b_idx],
                             edgecolor='black',
                             linewidth=0.75,
                             label=baseline if w_idx == 0 else "")
                if w_idx == 0:
                    bars[baseline] = bar  # Store for legend

    # Set main axis labels and styles
    ax.set_ylabel("Throughput (Mops)", fontsize=22)
    ax.set_ylim(0, 55)
    ax.set_xticks([np.mean(x) for x in x_positions])
    ax.set_xticklabels(workload_labels, fontsize=22)
    ax.tick_params(axis='y', labelsize=22, direction='in')

    # Add legend with baseline names
    handles = [bars[b][0] for b in baselines]
    labels = [b for b in baselines]
    ax.legend(handles, labels, loc='upper right', ncol=3, fontsize=10, frameon=False)

    # Create an inset axis for zooming into the 'ycsb-e' workload
    axins = inset_axes(ax, width="15%", height="20%", loc='lower left',
                       bbox_to_anchor=(0.68, 0.4, 1, 1),
                       bbox_transform=ax.transAxes)

    # Draw bars for the inset (ycsb-e)
    x_e = x_positions[4]  # Index 4 corresponds to ycsb-e
    for b_idx, baseline in enumerate(baselines):
        val = df[(df['Index'] == baseline) & (df['Workload'] == 'ycsb-e')]['Total']
        if not val.empty:
            height = val.values[0]
            axins.bar(x_e[b_idx], height, width,
                      color=bar_colors[b_idx],
                      hatch=hatchs[b_idx],
                      edgecolor='black',
                      linewidth=0.75)

    # Set inset axis limits and remove ticks
    axins.set_ylim(0, 5)
    axins.set_xticks([])
    axins.set_yticks([])

    # Link inset and main axis
    mark_inset(ax, axins, loc1=2, loc2=4, fc="none", ec="k", lw=1)

    # Add grid and manually adjust layout to prevent overflow
    ax.grid(True, which='both', linestyle='--', color='gray', linewidth=0.5)
    ax.set_axisbelow(True)
    fig.subplots_adjust(left=0.15, right=0.98, top=0.88, bottom=0.15)

    # Save the figure to a file named by distribution
    pdf_name = f'ycsb_{distribution}.pdf'
    plt.savefig(pdf_name)
    print(f"[✓] Saved plot to {pdf_name}")
    # plt.show()

if __name__ == '__main__':
    runtime('zipfian')
    runtime('uniform')
