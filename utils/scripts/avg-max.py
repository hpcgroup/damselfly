#!/usr/bin/env python

import glob
import numpy as np
import re
from loaddata import *

if __name__ == '__main__':
  from sys import argv

  dirprefix = argv[1]
  filelist = []

  filelist.append(sorted(glob.glob(dirprefix + '/4jobs-1/sim*/links-4jobs.32k4.csv')))
  filelist.append(sorted(glob.glob(dirprefix + '/4jobs-2/sim*/links-4jobs.16k2-32k-64k.csv')))
  filelist.append(sorted(glob.glob(dirprefix + '/8jobs-1/sim*/links-8jobs.16k8.csv')))
  filelist.append(sorted(glob.glob(dirprefix + '/8jobs-2/sim*/links-8jobs.8k6-16k-64k.csv')))

  for flist in filelist:
      wdname = re.search('\djobs-\d', flist[0]).group(0)

      file1 = open("%s-%s-total.dat" % (dirprefix, wdname), "w")
      file2 = open("%s-%s-green.dat" % (dirprefix, wdname), "w")
      file3 = open("%s-%s-black.dat" % (dirprefix, wdname), "w")
      file4 = open("%s-%s-blue.dat" % (dirprefix, wdname), "w")

      for filename in flist:
	  print filename
	  simname = re.search('sim\d+', filename).group(0)
	  (numjobs, alllinks, green, black, blue) = readfile(filename, -1)

	  linkavg = np.zeros(3)
	  linkmax = np.zeros(3)

	  linkavg[0] = np.average(green)
	  linkavg[1] = np.average(black)
	  linkavg[2] = np.average(blue)
	  linkmax[0] = np.amax(green)
	  linkmax[1] = np.amax(black)
	  linkmax[2] = np.amax(blue)
	  maxindex = np.argmax(linkmax)

	  file1.write("%s %0.3f %0.3f\n" % (simname, linkavg[maxindex], linkmax[maxindex]))
	  file2.write("%s %0.3f %0.3f\n" % (simname, linkavg[0], linkmax[0]))
	  file3.write("%s %0.3f %0.3f\n" % (simname, linkavg[1], linkmax[1]))
	  file4.write("%s %0.3f %0.3f\n" % (simname, linkavg[2], linkmax[2]))

      file1.close()
      file2.close()
      file3.close()
      file4.close()

