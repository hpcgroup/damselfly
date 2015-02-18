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

  int numNodes = numgroups*numchassis*numrouters*numpcis;
  int *placed = new int[numNodes];
  int *jobmap = new int[jobsize/numcores];

  srand(101429);

  memset(placed, 0, sizeof(int)*numNodes);
  int currentNode = 0;
  for(int i = 0; i < 8*(jobsize/numcores); i++) {
  	int target = rand() % numNodes;
  	if(placed[target] == 0) {
  		placed[target] = 1;
  		jobmap[currentNode] = target;
  		currentNode++;
  		if(currentNode == (jobsize/numcores)) {
  			break;
  		}
  	}
  }

  int target = 0;
  if(currentNode != (jobsize/numcores)) {
  	for(; currentNode < (jobsize/numcores); currentNode++) {
  		while(placed[target] == 1) {
  			target++;
  		}
  		placed[target] = 1;
  		jobmap[currentNode] = target;
  	}
  }

  int dims[5];
  for(currentNode = 0; currentNode < (jobsize/numcores); currentNode++) {
  	target = jobmap[currentNode]*numcores;
  	for(int i = 0; i < numcores; i++) {
  		rankToCoords(target, dims);
  		for(int j = 0; j < 5; j++) {
	  		printf("%d ", dims[j]);
	  	}
	  	printf("\n");
	  	target++;
  	}
  }
}
