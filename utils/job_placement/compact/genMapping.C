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
  int numAllocCores = 0;

  jobSizes.resize(numJobs);
  for(int i=0; i<numJobs; i++) {
    jobSizes[i] = atoi(argv[i+6]);
    numAllocCores += jobSizes[i];
    // printf("%d ", jobSizes[i]);
  }

  int dims[5];
  int target = 0, coresperjob = 0, jobid = 0, totalcores = 0;

  /* currently only works for certain job sizes -- larger than a group
   * for single jobs and non-multiples of group size for job workloads */
  for(int currentGroup = 0; currentGroup <= (numAllocCores/(numrows * numcols * numnodesperrouter * numcores)); currentGroup++) {
    if(totalcores == numAllocCores)
      break;
    target = currentGroup * numrows * numcols * numnodesperrouter * numcores;
    for(int i = 0; i < (numrows * numcols * numnodesperrouter * numcores); i++) {
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
