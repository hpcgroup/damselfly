#!/usr/bin/env python

import sys
import re
import numpy as np
import struct

filename = sys.argv[1]
intracon = open(sys.argv[2], "wb")
intercon = open(sys.argv[3], "wb")

def router(group, row, col):
    return group*96 + row*16 + col

numblack = np.zeros((1440,1440), dtype=np.int)
numblue = np.zeros((1440,1440), dtype=np.int)

with open(filename) as ofile:
    matches = re.findall('c\d-\dc\ds\d+a0l\d+\((\d+):(\d):(\d+)\).(\w+).->.c\d-\dc\ds\d+a0l\d+\((\d+):(\d):(\d+)\)', ofile.read(), re.MULTILINE)

for match in matches:
    srcgrp = int(match[0])
    if(srcgrp > 12):
	srcgrp = srcgrp - 1
    srcrow = int(match[1])
    srccol = int(match[2])
    srcrouter = router(srcgrp, srcrow, srccol)

    color = match[3]

    dstgrp = int(match[4])
    if(dstgrp > 12):
	dstgrp = dstgrp - 1
    dstrow = int(match[5])
    dstcol = int(match[6])
    dstrouter = router(dstgrp, dstrow, dstcol)

    # count number of black and blue links per router pair
    if color == 'black':
	numblack[srcrouter][dstrouter] += 1
    if color == 'blue':
	numblue[srcrouter][dstrouter] += 1

    if srcgrp == 0:
	if color == 'blue':
	    # write to inter-con file
	    intercon.write(struct.pack('2i', srcrouter, dstrouter))
	    print 'BLUE', srcrouter, dstrouter
	else:
	    # write to intra-con file
	    if color == 'green':
		intracon.write(struct.pack('3i', srcrouter, dstrouter, 0))
		print 'GREEN', srcrouter, dstrouter, 0
	    elif numblack[srcrouter][dstrouter] < 4:
		intracon.write(struct.pack('3i', srcrouter, dstrouter, 1))
		print 'BLACK', srcrouter, dstrouter, 1
    else:
	if color == 'blue':
	    # only write the inter-con file
	    intercon.write(struct.pack('2i', srcrouter, dstrouter))
	    print 'BLUE', srcrouter, dstrouter

intracon.close()
intercon.close()

