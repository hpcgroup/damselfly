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

  color = argv[1]

  filelist = sorted(glob.glob('single/*/links-*.csv'))
  # for i in range(0,14):
  #   temp = filelist[5*i]
  #   filelist[5*i] = filelist[5*i+4]
  #   filelist[5*i+4] = temp

  for filename in filelist:
    (alllinks, green, black, blue) = readfile(filename)

    if color == 'total':
      print "%s %0.3f %0.3f %0.3f" % (filename, np.percentile(alllinks, 0), np.average(alllinks), np.percentile(alllinks, 100))
    if color == 'green':
      print "%s %0.3f %0.3f %0.3f" % (filename, np.percentile(green, 0), np.average(green), np.percentile(green, 100))
    if color == 'black':
      print "%s %0.3f %0.3f %0.3f" % (filename, np.percentile(black, 0), np.average(black), np.percentile(black, 100))
    if color == 'blue':
      print "%s %0.3f %0.3f %0.3f" % (filename, np.percentile(blue, 0), np.average(blue), np.percentile(blue, 100))
