#!/usr/bin/env python

from sys import argv
import re
import glob
import numpy as np
import csv

# read counter file into a numpy array
def readfile(filename):
    temp = open(filename)
    csv_f = csv.reader(temp)
    first = next(csv_f)
    num_jobs = len(first) - 8

    input = open(filename, 'r')
    # print num_jobs, "num_jobs"
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
          'job4', 'job5', 'job5', 'job7'),
        'formats': ('i4', 'i4', 'i4',
            'i4', 'i4', 'i4',
            'S2', 'f8',
            'f8', 'f8', 'f8', 'f8',
            'f8', 'f8', 'f8', 'f8')}


    data = np.loadtxt(input, dtype=np.dtype(dtype),delimiter=",",skiprows=1)

    alllinks = data['bytes']
    green = data[np.where(data['color'] == 'g')]['bytes']
    black = data[np.where(data['color'] == 'k')]['bytes']
    blue = data[np.where(data['color'] == 'b')]['bytes']
    return (alllinks, green, black, blue)


