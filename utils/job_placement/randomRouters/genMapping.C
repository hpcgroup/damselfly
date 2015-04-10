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

  int numRouters = numgroups * numrows * numcols;
  int numAllocRouters = jobsize/(numnodesperrouter*numcores);

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
  for(currentRouter = 0; currentRouter < numAllocRouters; currentRouter++) {
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
