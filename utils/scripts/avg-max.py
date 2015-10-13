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

	  file1.write("%s %0.3f %0.3f\n" % (simname, np.average(alllinks), np.amax(alllinks)))
	  file2.write("%s %0.3f %0.3f\n" % (simname, np.average(green), np.amax(green)))
	  file3.write("%s %0.3f %0.3f\n" % (simname, np.average(black), np.amax(black)))
	  file4.write("%s %0.3f %0.3f\n" % (simname, np.average(blue), np.amax(blue)))

      file1.close()
      file2.close()
      file3.close()
      file4.close()

