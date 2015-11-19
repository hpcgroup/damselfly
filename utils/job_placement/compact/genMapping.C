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
vector<int> jobSizesNodes;

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
  int numAllocNodes = 0;

  jobSizes.resize(numJobs);
  jobSizesNodes.resize(numJobs);
  for(int i=0; i<numJobs; i++) {
    jobSizes[i] = atoi(argv[i+7]);
    jobSizesNodes[i] = ceil((double)jobSizes[i] / (double)numcores);
    numAllocNodes += jobSizesNodes[i];
  }

  int dims[5];
  int target, coresperjob = 0, jobid = 0;

  printf("g,r,c,n,core,jobid\n");

  for(int n = 0; n < numAllocNodes; n++) {
    for(int c = 0; c < numcores; c++) {
      target = n * numcores + c;
      rankToCoords(target, dims);
      for(int j = 0; j < 5; j++) {
	printf("%d,", dims[j]);
	fwrite(&dims[j], sizeof(int), 1, binout);
      }
      printf("%d\n", jobid);
      fwrite(&jobid, sizeof(int), 1, binout);

      coresperjob++;
      if(coresperjob == jobSizes[jobid]) {
	jobid++;
	coresperjob = 0;
	break;
      }
    }
  }

  fclose(binout);
}
