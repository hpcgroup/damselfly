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

  int numRouters = numgroups*numchassis*numrouters;
  int *placed = new int[numRouters];
  int *jobmap = new int[jobsize/(numpcis*numcores)];

  srand(101429);

  memset(placed, 0, sizeof(int)*numRouters);
  int currentRouter = 0;
  for(int i = 0; i < 16*(jobsize/(numpcis*numcores)); i++) {
  	int target = rand() % numRouters;
  	if(placed[target] == 0) {
  		placed[target] = 1;
  		jobmap[currentRouter] = target;
  		currentRouter++;
  		if(currentRouter == (jobsize/(numpcis*numcores))) {
  			break;
  		}
  	}
  }

  int target = 0;
  if(currentRouter != (jobsize/(numpcis*numcores))) {
  	for(; currentRouter < (jobsize/(numpcis*numcores)); currentRouter++) {
  		while(placed[target] == 1) {
  			target++;
  		}
  		placed[target] = 1;
  		jobmap[currentRouter] = target;
  	}
  }

  int dims[5];
  for(currentRouter = 0; currentRouter < (jobsize/(numpcis*numcores)); currentRouter++) {
  	target = jobmap[currentRouter]*numpcis*numcores;
  	for(int i = 0; i < (numpcis*numcores); i++) {
  		rankToCoords(target, dims);
  		for(int j = 0; j < 5; j++) {
	  		printf("%d ", dims[j]);
	  	}
	  	printf("\n");
	  	target++;
  	}
  }
}
