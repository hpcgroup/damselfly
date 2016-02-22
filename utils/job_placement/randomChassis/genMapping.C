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
// https://github.com/LLNL/damselfly
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

  int numChassis = numgroups*numchassis;
  int *placed = new int[numChassis];
  int *jobmap = new int[jobsize/(numrouters*numpcis*numcores)];

  srand(101429);

  memset(placed, 0, sizeof(int)*numChassis);
  int currentChassis = 0;
  for(int i = 0; i < 32*(jobsize/(numrouters*numpcis*numcores)); i++) {
  	int target = rand() % numChassis;
  	if(placed[target] == 0) {
  		placed[target] = 1;
  		jobmap[currentChassis] = target;
  		currentChassis++;
  		if(currentChassis == (jobsize/(numrouters*numpcis*numcores))) {
  			break;
  		}
  	}
  }

  int target = 0;
  if(currentChassis != (jobsize/(numrouters*numpcis*numcores))) {
  	for(; currentChassis < (jobsize/(numrouters*numpcis*numcores)); currentChassis++) {
  		while(placed[target] == 1) {
  			target++;
  		}
  		placed[target] = 1;
  		jobmap[currentChassis] = target;
  	}
  }

  int dims[5];
  for(numChassis = 0; numChassis < (jobsize/(numrouters*numpcis*numcores)); numChassis++) {
  	target = jobmap[numChassis]*numrouters*numpcis*numcores;
  	for(int i = 0; i < (numrouters*numpcis*numcores); i++) {
  		rankToCoords(target, dims);
  		for(int j = 0; j < 5; j++) {
	  		printf("%d ", dims[j]);
	  	}
	  	printf("\n");
	  	target++;
  	}
  }
}
