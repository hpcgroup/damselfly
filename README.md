Damselfly
=========

This code is to model ideal flow of packets on Cray's XC30.

Limitation: can be run on at max 16,384 MPI ranks. The primes.h file has
limited number of entries. Simulated cores can be as many as desired.

Code is a bit messed up .. different branches of the repo simulates different
scenarios. Hopefully, this will be cleaned up with time.

Branch biased-only is currently active and should be closest to real machine's
behavior.

Compilation:
```bash
mkdir build && cd build
cmake ..
make
```

Sample cmake command for Blue Gene/Q:

    cmake -DCMAKE_C_COMPILER=mpixlc_r -DCMAKE_CXX_COMPILER=mpixlcxx_r -DCMAKE_BUILD_TYPE=Release -DJOB_TRAFFIC=ON ..

Run Command:

    mpirun -np 8 ./damselfly conffile map_file out_file

Where:

* conffile - configuration file (format described below)
* map_file - if non-existent, default is used (Blue Gene style, i.e. coordinates in each line represents a rank; first "n1" ranks assumed for job 1, next "n2" ranks assumed for job 2, etc.)
* out_file - generated, contains the bytes on links

---BEGIN Format of conffile
```
#total number of routers
#groups #rows #columns #nodes #cores
filename for intra group connection (binary format)
filename for inter group connection (binary format)
filename #ranks #edges (binary)
filename #ranks #edges (binary)
... as many files as needed
```
---END Format of conffile

Code for generating sample connection files are in utils folder.

## Release

Damselfly is released under an LGPL license. For more details see the [LICENSE](/LICENSE) file.

`LLNL-CODE-678961`
