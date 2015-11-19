//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
//
// Written by:
//     Nikhil Jain <nikhil.jain@acm.org>
//     Abhinav Bhatele <bhatele@llnl.gov>
//     Peer-Timo Bremer <ptbremer@llnl.gov>
//
// LLNL-CODE-678961. All rights reserved.
//
// This file is part of Damselfly. For details, see:
// https://github.com/scalability-llnl/damselfly
// Please also read the LICENSE file for our notice and the LGPL.
//////////////////////////////////////////////////////////////////////////////

#include <cstdio>
#include <cstdlib>
#include <string.h>
#include <math.h>
#include <vector>

using namespace std;

int numgroups, numrows, numcols, numnodesperrouter, numcores;
vector<int> jobSizes;

void rankToCoords(int rank, int *dims) {
  dims[4] = rank % numcores;
  rank /= numcores;

  dims[3] = rank % numnodesperrouter;
  rank /= numnodesperrouter;

  dims[2] = rank % numcols;
  rank /= numcols;

  dims[1] = rank % numrows;
  rank /= numrows;

  dims[0] = rank % numgroups;
}

int main(int argc, char**argv) {

  numgroups = atoi(argv[1]);
  numrows = atoi(argv[2]);
  numcols = atoi(argv[3]);
  numnodesperrouter = atoi(argv[4]);
  numcores = atoi(argv[5]);
  FILE *binout = fopen(argv[6], "wb");
  int numJobs = argc - 7;
  int numAllocCores = 0;

  jobSizes.resize(numJobs);
  for(int i=0; i<numJobs; i++) {
    jobSizes[i] = atoi(argv[i+7]);
    numAllocCores += jobSizes[i];
    // printf("%d ", jobSizes[i]);
  }

  int numRouters = numgroups * numrows * numcols;
  int numAllocRouters = ceil((double)(numAllocCores)/(double)(numnodesperrouter*numcores));

  int *placed = new int[numRouters];
  int *jobmap = new int[numAllocRouters];
  memset(placed, 0, sizeof(int)*numRouters);

  srand(101429);

  int currentRouter = 0;
  /* try and find as many random routers as possible in 16*numAllocNodes trials */
  for(int i = 0; i < 16 * numAllocRouters; i++) {
    int target = rand() % numRouters;
    if(placed[target] == 0) {
      placed[target] = 1;
      jobmap[currentRouter] = target;
      currentRouter++;
      if(currentRouter == numAllocRouters) {
	break;
      }
    }
  }

  /* fill remaining routers in jobmap if any in order */
  int target = 0;
  for(; currentRouter < numAllocRouters; currentRouter++) {
    while(placed[target] == 1) {
      target++;
    }
    placed[target] = 1;
    jobmap[currentRouter] = target;
    target++;
  }

  int dims[5];
  int coresperjob = 0, jobid = 0, totalcores = 0;

  printf("g,r,c,n,core,jobid\n");

  for(currentRouter = 0; currentRouter < numAllocRouters; currentRouter++) {
    if(totalcores == numAllocCores)
      break;
    target = jobmap[currentRouter] * numnodesperrouter * numcores;
    for(int i = 0; i < (numnodesperrouter*numcores); i++) {
      rankToCoords(target, dims);
      for(int j = 0; j < 5; j++) {
	printf("%d,", dims[j]);
	fwrite(&dims[j], sizeof(int), 1, binout);
      }
      printf("%d\n", jobid);
      fwrite(&jobid, sizeof(int), 1, binout);
      target++;

      coresperjob++;
      totalcores++;
      if(coresperjob == jobSizes[jobid]) {
        // printf("%d %d\n", coresperjob, jobid);
        jobid++;
        coresperjob = 0;
      }
      if(totalcores == numAllocCores)
        break;
    }
  }

  fclose(binout);
}
