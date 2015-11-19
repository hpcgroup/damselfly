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

int numgroups, numchassis, numrouters, numpcis, numcores;

void rankToCoords(int rank, int *dims) {
	dims[4] = rank % numcores;
	rank /= numcores;

	dims[3] = rank % numpcis;
	rank /= numpcis;

	dims[2] = rank % numrouters;
	rank /= numrouters;

	dims[1] = rank % numchassis;
	rank /= numchassis;

	dims[0] = rank % numgroups;
}

int main(int argc, char**argv) {

  numgroups = atoi(argv[1]);
  numchassis = atoi(argv[2]);
  numrouters = atoi(argv[3]);
  numpcis = atoi(argv[4]);
  numcores = atoi(argv[5]);
  int jobsize = atoi(argv[6]);

  int numNodes = numchassis*numrouters*numpcis;
  int *jobmap = new int[jobsize/(numcores)];

  int currentNode = 0, currentGroup = 0;
  for(int i = 0; i < (jobsize/(numcores)); i++) {
  	int target = currentGroup*numNodes + currentNode;
  	jobmap[i] = target;
      currentGroup++;
      if(currentGroup == numgroups) {
        currentGroup = 0;
        currentNode++;
      }
  }

  int dims[5], target;
  for(currentNode = 0; currentNode < (jobsize/(numcores)); currentNode++) {
  	target = jobmap[currentNode]*numcores;
  	for(int i = 0; i < (numcores); i++) {
  		rankToCoords(target, dims);
  		for(int j = 0; j < 5; j++) {
	  		printf("%d ", dims[j]);
	  	}
	  	printf("\n");
	  	target++;
  	}
  }
}
