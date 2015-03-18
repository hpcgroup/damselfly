#!/usr/bin/env python

import sys
import re
import numpy as np
import struct

filename = sys.argv[1]
intracon = open(sys.argv[2], "wb")
intercon = open(sys.argv[3], "wb")

with open(filename) as ofile:
    matches = re.findall('c\d-\dc\ds\d+a0l\d+\((\d+):(\d):(\d+)\).(\w+).->.c\d-\dc\ds\d+a0l\d+\((\d+):(\d):(\d+)\)', ofile.read(), re.MULTILINE)

for match in matches:
    srcgrp = int(match[0])
    if(srcgrp > 12):
	srcgrp = srcgrp - 1
    srcrow = int(match[1])
    srccol = int(match[2])
    srcrouter = srcgrp*96 + srcrow*16 + srccol

    color = match[3]

    dstgrp = int(match[4])
    if(dstgrp > 12):
	dstgrp = dstgrp - 1
    dstrow = int(match[5])
    dstcol = int(match[6])
    dstrouter = dstgrp*96 + dstrow*16 + dstcol

    if srcgrp == 0:
	if color == 'blue':
	    # write to inter-con file
	    intercon.write(struct.pack('2i', srcrouter, dstrouter))
	    print 'INTER', srcrouter, dstrouter
	else:
	    # write to intra-con file
	    if color == 'green':
		intracon.write(struct.pack('3i', srcrouter, dstrouter, 0))
		print 'INTRA', srcrouter, dstrouter, 0
	    else:
		intracon.write(struct.pack('3i', srcrouter, dstrouter, 1))
		print 'INTRA', srcrouter, dstrouter, 1
    else:
	if color == 'blue':
	    # only write the inter-con file
	    intercon.write(struct.pack('2i', srcrouter, dstrouter))
	    print 'INTER', srcrouter, dstrouter

intracon.close()
intercon.close()
