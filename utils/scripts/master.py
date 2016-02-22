#!/usr/bin/env python

##############################################################################
# Copyright (c) 2014, Lawrence Livermore National Security, LLC.
# Produced at the Lawrence Livermore National Laboratory.
#
# Written by:
#     Nikhil Jain <nikhil.jain@acm.org>
#     Abhinav Bhatele <bhatele@llnl.gov>
#     Peer-Timo Bremer <ptbremer@llnl.gov>
#
# LLNL-CODE-678961. All rights reserved.
#
# This file is part of Damselfly. For details, see:
# https://github.com/LLNL/damselfly
# Please also read the LICENSE file for our notice and the LGPL.
##############################################################################

import glob
import numpy as np
import re
from loaddata import *

if __name__ == '__main__':
  from sys import argv

  netconfigs = ['default', 'M1K', 'M1KP1B', 'M1KP3B', 'P1B', 'P3B']
  datasets = ['4jobs-1', '4jobs-2', '8jobs-1', '8jobs-2']

  print "config,dataset,sim,jobid,color,min,avg,max,nonzero,nzavg"

  for nc in netconfigs:
    for ds in datasets:
      filelist = sorted(glob.glob(nc + '/' + ds + '/sim*/links*k[48].csv') + glob.glob(nc + '/' + ds + '/sim*/links*k.csv'))

      for filename in filelist:
	simid = re.search('sim\d+', filename).group(0)

	(numjobs, alllinks, green, black, blue) = readfile(filename, -1)
	for job in range(numjobs):
	  (numjobs, alllinks, green, black, blue) = readfile(filename, job)

	  print "%s,%s,%s,%d,%d,%0.3f,%0.3f,%0.3f,%d,%0.3f" % (nc, ds, simid, job, 0, np.amin(alllinks), np.average(alllinks), np.amax(alllinks), np.count_nonzero(alllinks), np.sum(alllinks)/np.count_nonzero(alllinks))
	  print "%s,%s,%s,%d,%d,%0.3f,%0.3f,%0.3f,%d,%0.3f" % (nc, ds, simid, job, 1, np.amin(green), np.average(green), np.amax(green), np.count_nonzero(green), np.sum(green)/np.count_nonzero(green))
	  print "%s,%s,%s,%d,%d,%0.3f,%0.3f,%0.3f,%d,%0.3f" % (nc, ds, simid, job, 2, np.amin(black), np.average(black), np.amax(black), np.count_nonzero(black), np.sum(black)/np.count_nonzero(black))
	  print "%s,%s,%s,%d,%d,%0.3f,%0.3f,%0.3f,%d,%0.3f" % (nc, ds, simid, job, 3, np.amin(blue), np.average(blue), np.amax(blue), np.count_nonzero(blue), np.sum(blue)/np.count_nonzero(blue))

