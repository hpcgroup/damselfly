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

  int numRouters = numgroups*numrows*numcols;
  int *placed = new int[numRouters];
  int *jobmap = new int[jobsize/(numnodesperrouter*numcores)];

  srand(101429);

  memset(placed, 0, sizeof(int)*numRouters);
  int currentRouter = 0;
  for(int i = 0; i < 16*(jobsize/(numnodesperrouter*numcores)); i++) {
    int target = rand() % numRouters;
    if(placed[target] == 0) {
      placed[target] = 1;
      jobmap[currentRouter] = target;
      currentRouter++;
      if(currentRouter == (jobsize/(numnodesperrouter*numcores))) {
	break;
      }
    }
  }

  int target = 0;
  if(currentRouter != (jobsize/(numnodesperrouter*numcores))) {
    for(; currentRouter < (jobsize/(numnodesperrouter*numcores)); currentRouter++) {
      while(placed[target] == 1) {
	target++;
      }
      placed[target] = 1;
      jobmap[currentRouter] = target;
    }
  }

  int dims[5];
  for(currentRouter = 0; currentRouter < (jobsize/(numnodesperrouter*numcores)); currentRouter++) {
    target = jobmap[currentRouter]*numnodesperrouter*numcores;
    for(int i = 0; i < (numnodesperrouter*numcores); i++) {
      rankToCoords(target, dims);
      for(int j = 0; j < 5; j++) {
	printf("%d ", dims[j]);
      }
      printf("\n");
      target++;
    }
  }
}
