Damselfly v1.0
==============

Damselfly is a model-based parallel network simulator. It can simulate
communication patterns of High Performance Computing applications on different
network topologies. It outputs steady-state network traffic for a communication
pattern, which can help in studying network congestion and its impact on
performance.

Limitation: can be run on up to 16,384 MPI processes. The primes.h file has a
limited number of entries.

### Build
```bash
mkdir build && cd build
cmake ..
make
```

Sample cmake command for Blue Gene/Q:
```bash
    cmake -DCMAKE_C_COMPILER=mpixlc_r -DCMAKE_CXX_COMPILER=mpixlcxx_r -DCMAKE_BUILD_TYPE=Release -DJOB_TRAFFIC=ON ..
```

### Run

```
    mpirun -np 8 ./damselfly conffile map_file out_file
```
where:
* conffile - configuration file (format described below)
* map_file - if non-existent, default is used (Blue Gene style, i.e. coordinates in each line represents a rank; first "n1" ranks assumed for job 1, next "n2" ranks assumed for job 2, etc.)
* out_file - generated, contains the bytes on links

##### Format of conffile
```
#total number of routers
#groups #rows #columns #nodes #cores
filename for intra group connection (binary format)
filename for inter group connection (binary format)
filename #ranks #edges (binary)
filename #ranks #edges (binary)
... as many files as needed
```

Code for generating sample connection files are in the utils directory.

### Reference

Any published work which utilizes this API should include the following
reference:

```
Nikhil Jain, Abhinav Bhatele, Xiang Ni, Nicholas J. Wright, and Laxmikant V.
Kale. Maximizing Throughput on a Dragonfly Network. In Proceedings of the
International Conference for High Performance Computing, Networking, Storage
and Analysis, SC '14, November 2014. LLNL-CONF-653557.
```

## Release

Copyright (c) 2014, Lawrence Livermore National Security, LLC.
Produced at the Lawrence Livermore National Laboratory.

Written by:
```
    Nikhil Jain <nikhil.jain@acm.org>
    Abhinav Bhatele <bhatele@llnl.gov>
    Peer-Timo Bremer <ptbremer@llnl.gov>
```

LLNL-CODE-678961. All rights reserved.

This file is part of Damselfly. For details, see:
https://github.com/LLNL/damselfly.
Please also read the LICENSE file for our notice and the LGPL.
