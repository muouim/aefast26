import sys
import matplotlib.pyplot as plt
import re
import numpy as np
import pandas as pd
import chardet
from matplotlib.font_manager import FontProperties
from matplotlib.patches import FancyArrowPatch
import json
plt.rcParams['font.family'] = 'Times New Roman'

def runtime():
    df = pd.read_csv(f'micro_read_zip.csv')
        
    fig, ax1 = plt.subplots(1,1,figsize=(4, 2.5), dpi=120)
    width = 0.8
    bar_colors = ['#808080', '#1D3557', '#FFDF92', '#457B9D', '#A8DADB', '#497D74', '#E73847']
    
    x = df['Index']
    y1 = df['RDMA']
    y2 = df['Sherman']
    y3 = df['dLSM']
    y4 = df['ROLEX']
    y5 = df['SMART']
    y6 = df['CHIME']
    y7 = df['DMTree']

    line3 = ax1.plot(x, y3, color=bar_colors[2], marker='d', markersize=8, label="dLSM", linewidth=2)
    line2 = ax1.plot(x, y2, color=bar_colors[1], marker='^', markersize=8, label="Sherman", linewidth=2)
    line4 = ax1.plot(x, y4, color=bar_colors[3], marker='*', markersize=8, label="ROLEX", linewidth=2)
    line5 = ax1.plot(x, y5, color=bar_colors[4], marker='h', markersize=8, label="SMART", linewidth=2)
    line6 = ax1.plot(x, y6, color=bar_colors[5], marker='p', markersize=8, label="CHIME", linewidth=2)
    line7 = ax1.plot(x, y7, color=bar_colors[6], marker='o', markersize=8, label="DMTree", linewidth=2)

    line = ax1.plot(x, y1, color=bar_colors[0], linestyle='--', label='', linewidth=2)

    #ax1.text(30, 51, u'Expected Read', fontsize=16, color='#E73847')
    ax1.set_ylabel(u'Throughput (Mops)', fontsize=18)
    ax1.yaxis.label.set_position((-0.1, 0.48))
    ax1.set_ylim(0,70)
    ax1.tick_params(axis='y', labelsize=18, direction='in')
    ax1.set_xlim(0,450)
    ax1.set_xticks([0, 100, 200, 300, 400])
    ax1.set_xticklabels(['0','100','200','300','400'], fontsize=18)
    ax1.set_xlabel(u'Number of Client Threads', fontsize=18)

    #ax1.legend(fontsize=12, ncols=3,
    #           handlelength=0.5, handletextpad=0.25, columnspacing=1, loc='upper left') 
    ax1.grid(True, which='both', linestyle='--', color='gray', linewidth=0.5)
    ax1.set_axisbelow(True) 
    plt.tight_layout()
    plt.savefig('micro_read_zip.pdf')
    #plt.show()
    
 
if __name__ == '__main__':
    runtime()

