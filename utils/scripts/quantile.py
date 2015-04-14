#!/usr/bin/env python

import glob
import numpy as np
import matplotlib
from loaddata import *

# these properties need to be modified before pylab is imported
matplotlib.use('pdf')
matplotlib.rcParams.update({
    'patch.linewidth':          2,
    'font.size':                10,
    'font.family':              'sans-serif',
    'axes.titlesize':           'medium',
    'ytick.major.pad':          10,
    'legend.fontsize':          10,
    'legend.frameon':           False,
    'legend.borderaxespad':     1.0,
    'figure.figsize':           (5, 3),
})

import matplotlib.pyplot as pl

if __name__ == '__main__':
  from sys import argv

  num_jobs = int(argv[2])

  (alllinks, green, black, blue)  = readfile(argv[1], num_jobs)

  print np.percentile(alllinks, 0), np.percentile(alllinks, 25), np.percentile(alllinks, 50), np.percentile(alllinks, 75), np.percentile(alllinks, 100), np.average(alllinks)
  print np.percentile(green, 0), np.percentile(green, 25), np.percentile(green, 50), np.percentile(green, 75), np.percentile(green, 100), np.average(green)
  print np.percentile(black, 0), np.percentile(black, 25), np.percentile(black, 50), np.percentile(black, 75), np.percentile(black, 100), np.average(black)
  print np.percentile(blue, 0), np.percentile(blue, 25), np.percentile(blue, 50), np.percentile(blue, 75), np.percentile(blue, 100), np.average(blue)
