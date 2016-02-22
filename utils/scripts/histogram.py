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
from loaddata import *

np.set_printoptions(threshold='nan')

if __name__ == '__main__':
  from sys import argv

  numSims = int(argv[1])
  numJobs = int(argv[2])
  fileprefix = argv[3]

  outarray = np.empty([4, numJobs, 2*numSims*2]) # linkcolor, #jobs, (avg and max for 4 sims, alone vs parallel)

  for sim in range(1, numSims+1):
    filename = "default/4jobs-2/sim%d/links-4jobs.16k2-32k-64k.csv" % sim
    for job in range(0, numJobs):
      file2name = "default/4jobs-2/sim%d/links-4jobs.16k2-32k-64k-%d.csv" % (sim, job)
      (count, alllinks, green, black, blue) = readfile(file2name, -1)

      outarray[0][job][(sim-1)*4] = np.average(alllinks)
      outarray[0][job][(sim-1)*4+2] = np.amax(alllinks)
      outarray[1][job][(sim-1)*4] = np.average(green)
      outarray[1][job][(sim-1)*4+2] = np.amax(green)
      outarray[2][job][(sim-1)*4] = np.average(black)
      outarray[2][job][(sim-1)*4+2] = np.amax(black)
      outarray[3][job][(sim-1)*4] = np.average(blue)
      outarray[3][job][(sim-1)*4+2] = np.amax(blue)

      (count, alllinks, green, black, blue) = readfile(filename, job)

      outarray[0][job][(sim-1)*4+1] = np.average(alllinks)
      outarray[0][job][(sim-1)*4+3] = np.amax(alllinks)
      outarray[1][job][(sim-1)*4+1] = np.average(green)
      outarray[1][job][(sim-1)*4+3] = np.amax(green)
      outarray[2][job][(sim-1)*4+1] = np.average(black)
      outarray[2][job][(sim-1)*4+3] = np.amax(black)
      outarray[3][job][(sim-1)*4+1] = np.average(blue)
      outarray[3][job][(sim-1)*4+3] = np.amax(blue)

  file1 = open(fileprefix + '-total.dat', "w")
  file2 = open(fileprefix + '-green.dat', "w")
  file3 = open(fileprefix + '-black.dat', "w")
  file4 = open(fileprefix + '-blue.dat', "w")

  for job in range(0, numJobs):
    file1.write("%d " % job)
    for sim in range(0, 2*numSims*2):
      file1.write("%0.3f " % outarray[0][job][sim])
    file1.write("\n")

  for job in range(0, numJobs):
    file2.write("%d " % job)
    for sim in range(0, 2*numSims*2):
      file2.write("%0.3f " % outarray[1][job][sim])
    file2.write("\n")

  for job in range(0, numJobs):
    file3.write("%d " % job)
    for sim in range(0, 2*numSims*2):
      file3.write("%0.3f " % outarray[2][job][sim])
    file3.write("\n")

  for job in range(0, numJobs):
    file4.write("%d " % job)
    for sim in range(0, 2*numSims*2):
      file4.write("%0.3f " % outarray[3][job][sim])
    file4.write("\n")

  file1.close()
  file2.close()
  file3.close()
  file4.close()
