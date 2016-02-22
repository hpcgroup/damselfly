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

import numpy as np
import csv

# read counter file into a numpy array
def readfile(filename, jobIndex):
    temp = open(filename)
    csv_f = csv.reader(temp)
    first = next(csv_f)
    num_jobs = len(first) - 8

    if(jobIndex == -1):
      column = 'bytes'
    else:
      column = 'job' + str(jobIndex)

    input = open(filename, 'r')

    if (num_jobs == 0) :
      dtype={'names': ('sg', 'sr', 'sc',
          'dg', 'dr', 'dc',
          'color', 'bytes'),
        'formats': ('i4', 'i4', 'i4',
            'i4', 'i4', 'i4',
            'S2', 'f8')}
    elif (num_jobs == 1) :
      dtype={'names': ('sg', 'sr', 'sc',
          'dg', 'dr', 'dc',
          'color', 'bytes',
          'job0'),
        'formats': ('i4', 'i4', 'i4',
            'i4', 'i4', 'i4',
            'S2', 'f8',
            'f8')}
    elif (num_jobs == 4) :
      dtype={'names': ('sg', 'sr', 'sc',
          'dg', 'dr', 'dc',
          'color', 'bytes',
          'job0', 'job1', 'job2', 'job3'),
        'formats': ('i4', 'i4', 'i4',
            'i4', 'i4', 'i4',
            'S2', 'f8',
            'f8', 'f8', 'f8', 'f8')}
    else :
      dtype={'names': ('sg', 'sr', 'sc',
          'dg', 'dr', 'dc',
          'color', 'bytes',
          'job0', 'job1', 'job2', 'job3',
          'job4', 'job5', 'job6', 'job7'),
        'formats': ('i4', 'i4', 'i4',
            'i4', 'i4', 'i4',
            'S2', 'f8',
            'f8', 'f8', 'f8', 'f8',
            'f8', 'f8', 'f8', 'f8')}


    data = np.loadtxt(input, dtype=np.dtype(dtype), delimiter=",", skiprows=1)

    alllinks = data[column]
    green = data[np.where(data['color'] == 'g')][column]
    black = data[np.where(data['color'] == 'k')][column]
    blue = data[np.where(data['color'] == 'b')][column]
    return (num_jobs, alllinks, green, black, blue)


