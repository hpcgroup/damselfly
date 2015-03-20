#include <cstdio>
#include <cstdlib>
#include <string.h>
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
  int totalNumCores = 0;

  jobSizes.resize(numJobs);
  for(int i=0; i<numJobs; i++) {
    jobSizes[i] = atoi(argv[i+6]);
    totalNumCores += jobSizes[i];
    // printf("%d ", jobSizes[i]);
  }

  int numtotalnodes = numgroups*numrows*numcols*numnodesperrouter;
  int *placed = new int[numtotalnodes];
  int *jobmap = new int[totalNumCores/numcores];

  srand(101429);

  memset(placed, 0, sizeof(int)*numtotalnodes);
  int currentNode = 0;
  for(int i = 0; i < 8*(totalNumCores/numcores); i++) {
    int target = rand() % numtotalnodes;
    if(placed[target] == 0) {
      placed[target] = 1;
      jobmap[currentNode] = target;
      currentNode++;
      if(currentNode == (totalNumCores/numcores)) {
	break;
      }
    }
  }

  int target = 0;
  if(currentNode != (totalNumCores/numcores)) {
    for(; currentNode < (totalNumCores/numcores); currentNode++) {
      while(placed[target] == 1) {
	target++;
      }
      placed[target] = 1;
      jobmap[currentNode] = target;
    }
  }

  int dims[5];
  int coresperjob = 0, jobid = 0;

  for(currentNode = 0; currentNode < (totalNumCores/numcores); currentNode++) {
    target = jobmap[currentNode]*numcores;
    for(int i = 0; i < numcores; i++) {
      rankToCoords(target, dims);
      for(int j = 0; j < 5; j++) {
	printf("%d ", dims[j]);
      }
      printf("%d\n", jobid);
      target++;

      coresperjob++;
      if(coresperjob == jobSizes[jobid]) {
        // printf("%d %d\n", coresperjob, jobid);
        jobid++;
        coresperjob = 0;
      }
    }
  }
}
