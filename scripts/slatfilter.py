import numpy
import matplotlib.pyplot as plt
from scipy.signal import medfilt, savgol_filter
import operator
import argparse
import pprint
from pandas import DataFrame

ncacheline = 64
cachelines = []

def get_cachelines(label, e):
	global cachelines
	pml4 = ((e >> 39) & 0x1FF) >> 3
	pdpt = ((e >> 30) & 0x1FF) >> 3
	pd   = ((e >> 21) & 0x1FF) >> 3
	pt   = ((e >> 12) & 0x1FF) >> 3
	print "%s\t0x%x:\t%d, %d, %d, %d" % (label, e, pml4, pdpt, pd, pt)
	cachelines += [pml4, pdpt, pd, pt]

parser = argparse.ArgumentParser()
parser.add_argument("filename", help="provide path to slat timing file")
args = parser.parse_args()

data = numpy.genfromtxt(args.filename, delimiter=",", dtype=None, usecols=(1), skip_header=6)
nrounds = data.size/ncacheline

pte_entries = numpy.genfromtxt(args.filename, delimiter=",", dtype=None, 
					usecols=(0, 1), encoding=None, skip_footer=data.size)
for entry in pte_entries:
	get_cachelines(entry[0], int(entry[1]))

print "\nSLAT cacheline candidates:"
pprint.pprint(set(cachelines))

# Read time values for each cachelines into a dictionary of {cacheline:[timings]}
cache_n_timing = {}
cache_list =[]

for cacheline in range(ncacheline):
	sample = data[cacheline * nrounds: (cacheline + 1) * nrounds]
	sample = medfilt(sample) 
	sample = savgol_filter(sample, nrounds - 1, 0)
	cache_list.append(sample)
	avg = sum(sample.tolist())/nrounds
	cache_n_timing[cacheline] = avg

g_index = [s for s in range(ncacheline)]
g_cols  = [r for r in range(nrounds)]
df = DataFrame(cache_list, index = g_index, columns = g_cols)

plt.pcolor(df)
plt.savefig("results/slatfilter.png")

sorted_cache_score = sorted(cache_n_timing.items(), key=operator.itemgetter(1))[::-1]

for cache_score in sorted_cache_score:
        cacheline, score = cache_score
        if cacheline in cachelines:
                print "Cacheline: %d,\tScore: %d\t[OK]" % (cacheline, score)
        else:
                print "Cacheline: %d,\tScore: %d" % (cacheline, score)
