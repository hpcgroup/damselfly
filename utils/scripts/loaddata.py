#!/usr/bin/env python

from sys import argv
import re
import glob
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
          'job4', 'job5', 'job6', 'job7'),
        'formats': ('i4', 'i4', 'i4',
            'i4', 'i4', 'i4',
            'S2', 'f8',
            'f8', 'f8', 'f8', 'f8',
            'f8', 'f8', 'f8', 'f8')}


    data = np.loadtxt(input, dtype=np.dtype(dtype),delimiter=",",skiprows=1)

    alllinks = data[column]
    green = data[np.where(data['color'] == 'g')][column]
    black = data[np.where(data['color'] == 'k')][column]
    blue = data[np.where(data['color'] == 'b')][column]
    return (num_jobs, alllinks, green, black, blue)


