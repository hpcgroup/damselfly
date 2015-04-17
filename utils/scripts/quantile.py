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

  fileprefix = argv[1]

  filelist = sorted(glob.glob('multi/4jobs-1/*/links-*.csv'))
  # for i in range(0,5):
  #     temp = filelist[5*i]
  #     filelist[5*i] = filelist[5*i+4]
  #     filelist[5*i+4] = temp

  file1 = open("%s-total.dat" % fileprefix, "w")
  file2 = open("%s-green.dat" % fileprefix, "w")
  file3 = open("%s-black.dat" % fileprefix, "w")
  file4 = open("%s-blue.dat" % fileprefix, "w")

  for filename in filelist:
      (alllinks, green, black, blue) = readfile(filename)

      file1.write("%s %0.3f %0.3f %0.3f\n" % (filename, np.percentile(alllinks, 0), np.average(alllinks), np.percentile(alllinks, 100)))
      file2.write("%s %0.3f %0.3f %0.3f\n" % (filename, np.percentile(green, 0), np.average(green), np.percentile(green, 100)))
      file3.write("%s %0.3f %0.3f %0.3f\n" % (filename, np.percentile(black, 0), np.average(black), np.percentile(black, 100)))
      file4.write("%s %0.3f %0.3f %0.3f\n" % (filename, np.percentile(blue, 0), np.average(blue), np.percentile(blue, 100)))

file1.close()
file2.close()
file3.close()
file4.close()
