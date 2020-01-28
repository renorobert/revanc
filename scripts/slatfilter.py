#!/usr/bin/env python

import numpy
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy.signal import savgol_filter
from scipy.signal import medfilt
import operator
import argparse
import sys
import os
import pprint

RED = "\033[91m"
END = "\033[0m"
ncacheline = 64
cachelines = []

def PML4_IDX(v): return (v >> 39) & 0x1FF
def PDPT_IDX(v): return (v >> 30) & 0x1FF
def PD_IDX(v):   return (v >> 21) & 0x1FF
def PT_IDX(v):   return (v >> 12) & 0x1FF

def get_cachelines(label, e):
	global cachelines
	pml4_l = PML4_IDX(e) >> 3
	pdpt_l = PDPT_IDX(e) >> 3
	pd_l = PD_IDX(e) >> 3
	pt_l = PT_IDX(e) >> 3
	print "%s\t0x%x:\t%d, %d, %d, %d" % (label, e,
			pml4_l, pdpt_l, pd_l, pt_l)
	cachelines += [pml4_l, pdpt_l, pd_l, pt_l]

parser = argparse.ArgumentParser()
parser.add_argument("filename", help="provide path to slat timing file")
args = parser.parse_args()

if not os.path.exists(args.filename):
        sys.exit("[!] File %s not found." % (args.filename))

print "Processing entries in file...\n"
data = numpy.genfromtxt(args.filename, delimiter=",", dtype=None, usecols=(1), skip_header=6)

trounds = data.size/ncacheline

pte_entries = numpy.genfromtxt(args.filename, delimiter=",", dtype=None, 
					usecols=(0, 1), skip_footer=data.size)
for entry in pte_entries:
	get_cachelines(entry[0], int(entry[1]))

print "\nSLAT cacheline candidates:"
pprint.pprint(set(cachelines))

# window size is odd
if (trounds % 2) == 0: window_size = trounds - 1
else: window_size = trounds

# Read time values for each cachelines into a dictionary of {cacheline:[timings]}
cache_n_timing = {}

for cacheline in range(ncacheline):
	sample = data[cacheline * trounds: (cacheline + 1) * trounds]
	sample = medfilt(sample, 9) 
	sample = savgol_filter(sample, window_size, 0)
	avg = sum(sample.tolist())/trounds
	cache_n_timing[cacheline] = avg

fig, ax = plt.subplots()
plt.figure(figsize=(22,12))
plt.plot(cache_n_timing.keys(), cache_n_timing.values(), drawstyle="steps", linewidth=4)

plt.xticks(numpy.arange(0, ncacheline + 1, 1.0), color="green", fontweight="bold")
plt.yticks(color="green", fontweight="bold")
plt.autoscale(enable=True, axis='x', tight=True) 
plt.grid(linewidth=4)

plt.title("CACHELINE vs TIME", fontsize=24, color="red", fontweight="bold")
plt.xlabel("CACHELINE", fontsize=24, color="red", fontweight="bold")
plt.ylabel("TIME", fontsize=24, color="red", fontweight="bold")

print "\nWriting filtered graph to slatfilter.png\n"
plt.savefig('results/slatfilter.png')

sorted_cache_score = sorted(cache_n_timing.items(), key=operator.itemgetter(1))[::-1]

for cache_score in sorted_cache_score:
        cacheline, score = cache_score
        if cacheline in cachelines:
                print "%sCacheline: %d,\tScore: %d%s\t[OK]" % (RED, cacheline, score, END)
        else:
                print "Cacheline: %d,\tScore: %d" % (cacheline, score)
