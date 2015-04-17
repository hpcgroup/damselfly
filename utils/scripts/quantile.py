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

  filelist = sorted(glob.glob(argv[2]+'/*/links-*.csv'))

  file1 = open("%s-total.dat" % fileprefix, "w")
  file2 = open("%s-green.dat" % fileprefix, "w")
  file3 = open("%s-black.dat" % fileprefix, "w")
  file4 = open("%s-blue.dat" % fileprefix, "w")

  max_num_jobs = 0
  job_counters = np.zeros(len(filelist))

  count = 0
  for filename in filelist:
      (job_counters[count], alllinks, green, black, blue) = readfile(filename, -1)

      if(job_counters[count] > max_num_jobs):
        max_num_jobs = job_counters[count]
      count = count + 1

      file1.write("%s %0.3f %0.3f %0.3f %d %0.3f %0.3f\n" % (filename, np.percentile(alllinks, 0), np.average(alllinks), np.percentile(alllinks, 100), np.count_nonzero(alllinks), np.sum(alllinks), np.sum(alllinks)/np.count_nonzero(alllinks)))
      file2.write("%s %0.3f %0.3f %0.3f %d %0.3f %0.3f\n" % (filename, np.percentile(green, 0), np.average(green), np.percentile(green, 100), np.count_nonzero(green), np.sum(green), np.sum(green)/np.count_nonzero(green)))
      file3.write("%s %0.3f %0.3f %0.3f %d %0.3f %0.3f\n" % (filename, np.percentile(black, 0), np.average(black), np.percentile(black, 100), np.count_nonzero(black), np.sum(black), np.sum(black)/np.count_nonzero(black)))
      file4.write("%s %0.3f %0.3f %0.3f %d %0.3f %0.3f\n" % (filename, np.percentile(blue, 0), np.average(blue), np.percentile(blue, 100), np.count_nonzero(blue), np.sum(blue), np.sum(blue)/np.count_nonzero(blue)))

  file1.close()
  file2.close()
  file3.close()
  file4.close()

  file1 = open(fileprefix + '-total-perjob.dat', "w")
  file2 = open(fileprefix + '-green-perjob.dat', "w")
  file3 = open(fileprefix + '-black-perjob.dat', "w")
  file4 = open(fileprefix + '-blue-perjob.dat', "w")

  count = 0
  for filename in filelist:
      if(job_counters[count] > 0):
	  for job in range(int(max_num_jobs)) :
	      (num_jobs, alllinks, green, black, blue) = readfile(filename, job)

	      file1.write("%s job%d %d %0.3f %0.3f\n" % (filename, job, np.count_nonzero(alllinks), np.sum(alllinks), np.sum(alllinks)/np.count_nonzero(alllinks)))
	      file2.write("%s job%d %d %0.3f %0.3f\n" % (filename, job, np.count_nonzero(green), np.sum(green), np.sum(green)/np.count_nonzero(green)))
	      file3.write("%s job%d %d %0.3f %0.3f\n" % (filename, job, np.count_nonzero(black), np.sum(black), np.sum(black)/np.count_nonzero(black)))
	      file4.write("%s job%d %d %0.3f %0.3f\n" % (filename, job, np.count_nonzero(blue), np.sum(blue), np.sum(blue)/np.count_nonzero(blue)))

      count = count + 1

  file1.close()
  file2.close()
  file3.close()
  file4.close()

