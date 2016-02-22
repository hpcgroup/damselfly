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

  int numGroups = numgroups;
  int *placed = new int[numGroups];
  int *jobmap = new int[jobsize/(numchassis*numrouters*numpcis*numcores)];

  srand(101429);

  memset(placed, 0, sizeof(int)*numGroups);
  int currentGroup = 0;
  for(int i = 0; i < 64*(jobsize/(numchassis*numrouters*numpcis*numcores)); i++) {
  	int target = rand() % numGroups;
  	if(placed[target] == 0) {
  		placed[target] = 1;
  		jobmap[currentGroup] = target;
  		currentGroup++;
  		if(currentGroup == (jobsize/(numchassis*numrouters*numpcis*numcores))) {
  			break;
  		}
  	}
  }

  int target = 0;
  if(currentGroup != (jobsize/(numchassis*numrouters*numpcis*numcores))) {
  	for(; currentGroup < (jobsize/(numchassis*numrouters*numpcis*numcores)); currentGroup++) {
  		while(placed[target] == 1) {
  			target++;
  		}
  		placed[target] = 1;
  		jobmap[currentGroup] = target;
  	}
  }

  int dims[5];
  for(currentGroup = 0; currentGroup < (jobsize/(numchassis*numrouters*numpcis*numcores)); currentGroup++) {
  	target = jobmap[currentGroup]*numchassis*numrouters*numpcis*numcores;
  	for(int i = 0; i < (numchassis*numrouters*numpcis*numcores); i++) {
  		rankToCoords(target, dims);
  		for(int j = 0; j < 5; j++) {
	  		printf("%d ", dims[j]);
	  	}
	  	printf("\n");
	  	target++;
  	}
  }
}
