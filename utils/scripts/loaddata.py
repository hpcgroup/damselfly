#!/usr/bin/env python

from sys import argv
import re
import glob
import numpy as np

# read counter file into a numpy array
def readfile(filename):
    input = open(filename, 'r')

    dtype={'names': ('sg', 'sr', 'sc',
                     'dg', 'dr', 'dc',
                     'color', 'bytes',
                     'job0'),
    'formats': ('i4', 'i4', 'i4',
                'i4', 'i4', 'i4',
                'S2', 'f8',
                'f8')}

    data = np.loadtxt(input, dtype=np.dtype(dtype),delimiter=",",skiprows=1)

    alllinks = data['bytes']
    green = data[np.where(data['color'] == 'g')]['bytes']
    black = data[np.where(data['color'] == 'k')]['bytes']
    blue = data[np.where(data['color'] == 'b')]['bytes']
    print black
    return (alllinks, green, black, blue)


