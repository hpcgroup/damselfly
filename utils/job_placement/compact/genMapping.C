#include <cstdio>
#include <cstdlib>
#include <string.h>

int numgroups, numrows, numcols, numnodesperrouter, numcores;

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
  int jobsize = atoi(argv[6]);

  int dims[5];
  int target = 0;
  for(int currentGroup = 0; currentGroup < (jobsize/(numrows*numcols*numnodesperrouter*numcores)); currentGroup++) {
  	target = currentGroup*numrows*numcols*numnodesperrouter*numcores;
  	for(int i = 0; i < (numrows*numcols*numnodesperrouter*numcores); i++) {
  		rankToCoords(target, dims);
  		for(int j = 0; j < 5; j++) {
	  		printf("%d ", dims[j]);
	  	}
	  	printf("\n");
	  	target++;
  	}
  }
}
