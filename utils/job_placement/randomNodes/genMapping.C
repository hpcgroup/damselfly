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
  int numJobs = argc - 6;
  int numAllocCores = 0;

  jobSizes.resize(numJobs);
  for(int i=0; i<numJobs; i++) {
    jobSizes[i] = atoi(argv[i+6]);
    numAllocCores += jobSizes[i];
    // printf("%d ", jobSizes[i]);
  }

  int numNodes = numgroups * numrows * numcols * numnodesperrouter;
  int numAllocNodes = ceil((double)(numAllocCores)/(double)(numcores));

  int *placed = new int[numNodes];
  int *jobmap = new int[numAllocNodes];
  memset(placed, 0, sizeof(int)*numNodes);

  srand(101429);

  int currentNode = 0;
  /* try and find as many random nodes as possible in 8*numAllocNodes trials */
  for(int i = 0; i < 8 * numAllocNodes; i++) {
    int target = rand() % numNodes;
    if(placed[target] == 0) {
      placed[target] = 1;
      jobmap[currentNode] = target;
      currentNode++;
      if(currentNode == numAllocNodes) {
	break;
      }
    }
  }

  /* fill remaining nodes in jobmap if any in order */
  int target = 0;
  for(; currentNode < numAllocNodes; currentNode++) {
    while(placed[target] == 1) {
      target++;
    }
    placed[target] = 1;
    jobmap[currentNode] = target;
    target++;
  }

  int dims[5];
  int coresperjob = 0, jobid = 0, totalcores = 0;

  for(currentNode = 0; currentNode < numAllocNodes; currentNode++) {
    if(totalcores == numAllocCores)
      break;
    target = jobmap[currentNode]*numcores;
    for(int i = 0; i < numcores; i++) {
      rankToCoords(target, dims);
      for(int j = 0; j < 5; j++) {
	printf("%d ", dims[j]);
      }
      printf("%d\n", jobid);
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
}
