#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cassert>
#include <list>
#include <vector>
#include <sys/time.h>
#include <cfloat>
#include <mpi.h>

#define BIASED_ROUTING 1

using namespace std;

#define TIER1 0
#define TIER2 1
#define TIER3 2
#define NUM_LEVELS 3
#define NUM_COORDS (NUM_LEVELS + 2)

#define GREEN_START 0
#define GREEN_END 16
#define BLACK_START GREEN_END
#define BLACK_END 22
#define BLUE_START BLACK_END
#define BLUE_END 32

#define GREEN_BW 5120
#define BLACK_BW 5120
#define BLUE_BW 5120

#define PCI_BW 16384

#define PACKET_SIZE 64

#define CUTOFF_BW 0.01
#define NUM_ITERS 50
#define PATHS_PER_ITER 4
#define MAX_ITERS 200

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

typedef double myreal;
unsigned rand_seed;

inline void mysrand(unsigned seed) {
  //rand_seed = seed;
  srand(seed);
}

inline unsigned myrand () {
   //rand_seed = A_PRIME * (rand_seed) + B_PRIME;
   //return rand_seed;
   return rand();
}

inline unsigned myrand_r (unsigned *seed) {
   //*seed = A_PRIME * (*seed) + B_PRIME;
   //return *seed;
   return rand_r(seed);
}

// coordinates
typedef struct Coords {
  int coords[NUM_COORDS];
} Coords;

typedef struct Aries {
  myreal linksO[32]; // 0-15 Green, 16-21 Black
  myreal linksB[32], linksB_t[32]; //remaining link bandwidth
  myreal pciSO[4], pciRO[4]; //send and recv PCI
  myreal pciSB[4], pciRB[4], pciSB_t[4], pciRB_t[4]; //remaining PCI bandwidth
  int localRank; //rank within the group
  myreal linksSum[32]; //needed to continously get traffic
} Aries;

typedef struct Hop {
  int aries;
  short link;
} Hop;

typedef vector<Hop> Path;

// a message
typedef struct Msg {
  int src, dst;
  short srcPCI, dstPCI;
  myreal bytes; //in MB
  myreal bw; //allocated
  vector< Path > paths;
  short dcount;
  vector< bool > expand;
  vector< myreal > loads;
  vector< myreal > allocated;
} Msg;

typedef struct MsgSDB {
  int src, dst;
  double bytes;
} MsgSDB;

unsigned long long numMsgs; // number of messages to be sent
vector<Msg> msgsV; // messages left to be routed
int numAries; // number of Aries in the system
int numPEs; // number of ranks = numAries * product of last 2 coords
Coords maxCoords; //dimensionality
Coords *coords; // rank to coordinates
Aries *aries; // link status of current nodes
int ariesPerGroup;
FILE *outputFile;
int myRank, numRanks;

//forward declaration
void model();
inline void addLoads();
inline void addPathsToMsgs();
inline void addToPath(vector< Path > & paths, Coords src, int srcNum, Coords &dst, int loc1, int loc2);
inline void addFullPaths(vector< Path > & paths, Coords src, int srcNum, Coords &dst, int dstNum);
inline void getRandomPath(Coords src, int srcNum, Coords &dst, int dstNum, Path & p, unsigned *seed = 0);
inline void addIntraPath(Coords & src, int srcNum, Coords &dst, int dstNum, Path &p, unsigned *seed = 0);
inline void addInterPath(Coords & src, int srcNum, Coords &dst, int dstNum, Path &p, unsigned *seed = 0);
inline void selectExpansionRequests(bool & expand);
inline void markExpansionRequests();
inline void updateMessageAndLinks();
inline void computeSummary();
inline void printStats();

#include "primes.h"

//we use a simple dimensional ordered ranking for Aries - nothing to do with the
//actual ranking; used only for data management
inline void coordstoAriesRank(int &ariesRank, Coords &ncoord) {
  ariesRank = 0;
  int prod = 1;
  for(int i = NUM_LEVELS - 1; i >= 0; i--) {
    ariesRank += ncoord.coords[i]*prod;
    prod *= maxCoords.coords[i];
  }
}

inline void ariesRanktoCoords(int ariesRank, Coords &ncoord) {
  for(int i = NUM_LEVELS - 1; i >= 0; i--) {
    ncoord.coords[i] = ariesRank % maxCoords.coords[i];
    ariesRank /= maxCoords.coords[i];
  }
}

//some test to detect errors quickly
inline void nulltest(void *test, string testfor) {
  if(test == NULL) {
    printf("Test failed: %s\n",testfor.c_str());
    exit(1);
  }
}

inline void positivetest(double val, string valfor) {
  if(val <= 0) {
    printf("Non-positive value for %s\n", valfor.c_str());
    exit(1);
  }
}

inline void calculateAndPrint(struct timeval & start, struct timeval & end, string out) {
  double time = 0;
  time = (end.tv_sec - start.tv_sec) * 1000;
  time += ((end.tv_usec - start.tv_usec)/1000.0);

  printf("%s : %.3lf ms\n", out.c_str(), time);
}

int main(int argc, char**argv) {
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
  MPI_Comm_size(MPI_COMM_WORLD, &numRanks);

  if(argc == 1) {
    printf("Usage: %s <conffile> <mapfile> <commfile>\n", argv[0]);
    exit(1);
  }

  if(!myRank) {
    printf("Processing command line and conffile\n");
  }

  FILE *conffile = fopen(argv[1],"r");
  nulltest((void*)conffile, "configuration file");

  FILE *mapfile = fopen(argv[2],"r");

  FILE *commfile = fopen(argv[3], "rb");
  nulltest((void*)commfile, "communication file");

  outputFile = fopen(argv[4], "w");
  nulltest((void*)outputFile, "output file");

  fscanf(conffile, "%d", &numAries);
  positivetest((double)numAries, "number of Aries routers");

  for(int i = 0; i < NUM_COORDS; i++) {
    fscanf(conffile, "%d", &maxCoords.coords[i]);
    positivetest((double)maxCoords.coords[i], "a dimension");
  }
  ariesPerGroup = maxCoords.coords[TIER2] * maxCoords.coords[TIER3];

  fscanf(conffile, "%llu", &numMsgs);
  positivetest((double)numMsgs, "number of messages");

  assert(numAries == (ariesPerGroup*maxCoords.coords[TIER1]));

  numPEs = numAries * maxCoords.coords[NUM_LEVELS] * maxCoords.coords[NUM_LEVELS + 1];

  aries = new Aries[numAries];
  nulltest((void*)aries, "aries status array");

  coords = new Coords[numPEs];
  nulltest((void*)coords, "coordinates of PEs");

  /* Read the mapping of MPI ranks to hardware nodes */
  if(mapfile == NULL) {
    if(!myRank)
      printf("Mapfile not provided; using default mapping\n");
    for(int i = 0; i < numPEs; i++) {
      int rank = i;
      for(int j = NUM_COORDS - 1; j >= 0; j--) {
        coords[i].coords[j] = rank % maxCoords.coords[j];
        rank /= maxCoords.coords[j];
      }
    }
  } else {
    if(!myRank)
      printf("Reading mapfile\n");
    for(int i = 0; i < numPEs; i++) {
      for(int j = 0; j < NUM_COORDS; j++) {
        fscanf(mapfile, "%d", &coords[i].coords[j]);
      }
    }
  }

  double sum = 0;

  /* Read the communication graph which is in edge-list format */
  if(!myRank)
    printf("Reading messages\n");
  myreal MB = 1024 * 1024;
  struct timeval startRead, endRead;
  gettimeofday(&startRead, NULL);
  unsigned long long begin = (myRank*numMsgs)/numRanks;
  unsigned long long end = ((myRank+1)*numMsgs)/numRanks;
  unsigned long long currentCount = begin;
  fseek(commfile, begin*16, SEEK_SET);
  MsgSDB newMsgSDB;

  while(!feof(commfile)) {
    Msg newmsg;
    fread(&newMsgSDB, sizeof(MsgSDB), 1, commfile);
    newmsg.src = newMsgSDB.src;
    newmsg.dst = newMsgSDB.dst;
    newmsg.bytes = newMsgSDB.bytes;
    if(currentCount >= end) break;
    Coords& src = coords[newmsg.src];
    Coords& dst = coords[newmsg.dst];
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]
      && src.coords[TIER3] == dst.coords[TIER3]) {
      currentCount++;
      continue;
    }
    newmsg.bytes /= MB;
    newmsg.srcPCI = coords[newmsg.src].coords[NUM_LEVELS];
    newmsg.dstPCI = coords[newmsg.dst].coords[NUM_LEVELS];
    coordstoAriesRank(newmsg.src, coords[newmsg.src]);
    coordstoAriesRank(newmsg.dst, coords[newmsg.dst]);
    newmsg.bw = 0;
    msgsV.push_back(newmsg);
    sum += newmsg.bytes;
    currentCount++;
  }
  numMsgs = msgsV.size();

  //reuse coords for default
  delete[] coords;
  coords = new Coords[numAries];
  for(int i = 0; i < numAries; i++) {
    int rank = i;
    for(int j = NUM_LEVELS - 1; j >= 0; j--) {
      coords[i].coords[j] = rank % maxCoords.coords[j];
      rank /= maxCoords.coords[j];
    }
  }

  gettimeofday(&endRead, NULL);
  if(!myRank)
    calculateAndPrint(startRead, endRead, "time to read communication pattern");

  if(!myRank) {
    printf("Modeling for following system will be performed:\n");
    printf("numlevels: %d, dims: %d %d %d %d %d\n", NUM_LEVELS, maxCoords.coords[0], maxCoords.coords[1], maxCoords.coords[2], maxCoords.coords[3], maxCoords.coords[4]);
    printf("numAries: %d, numPEs: %d, numMsgs: %d, total volume: %.0lf MB\n", numAries, numPEs, numMsgs, sum);

    printf("Starting modeling \n");
  }

  struct timeval startModel, endModel;
  gettimeofday(&startModel, NULL);
  model();
  gettimeofday(&endModel, NULL);
  if(!myRank) {
    calculateAndPrint(startModel, endModel, "time to model");
  }

  fclose(conffile);
  if(mapfile != NULL)
    fclose(mapfile);
  fclose(commfile);
  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();
  delete[] aries;
  delete[] coords;
  exit(0);
}

inline void printStats() {
  myreal maxLoad = 0, minLoad = FLT_MAX;
  double totalLinkLoad = 0;
  unsigned long long linkCount = 0;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < BLUE_END; j++) {
      fprintf(outputFile, "%llu %lf\n", linkCount++, aries[i].linksO[j]);
      maxLoad = MAX(maxLoad, aries[i].linksO[j]);
      minLoad = MIN(minLoad, aries[i].linksO[j]);
      totalLinkLoad += aries[i].linksO[j];
    }
  }

  fclose(outputFile);

  long long totalLinks = numAries*(15+5+((numAries/ariesPerGroup) + (numAries % ariesPerGroup) ? 1 : 0));

  printf("******************Summary*****************\n");
  printf("maxLoad %.2f MB -- minLoad %.2f MB\n",maxLoad,minLoad);
  printf("averageLinkLoad %.2lf MB \n", totalLinkLoad/totalLinks);
}

void addToPath(vector< Path > & paths, Coords src, int srcNum, Coords &dst, int loc1, int loc2) {
  int interAries;
  Hop h;
  //first travel TIER3, then TIER2
  h.aries = srcNum;
  h.link = dst.coords[TIER3]; //GREEN
  paths[loc1].push_back(h);
  int back3 = src.coords[TIER3];
  src.coords[TIER3] = dst.coords[TIER3];
  coordstoAriesRank(interAries, src);
  h.aries = interAries;
  h.link = BLACK_START + dst.coords[TIER2];
  paths[loc1].push_back(h);

  //first travel TIER2, then TIER3
  h.aries = srcNum;
  h.link = BLACK_START + dst.coords[TIER2];
  paths[loc2].push_back(h);
  src.coords[TIER3] = back3;
  src.coords[TIER2] = dst.coords[TIER2];
  coordstoAriesRank(interAries, src);
  h.aries = interAries;
  h.link = dst.coords[TIER3];
  paths[loc2].push_back(h);
}

void addFullPaths(vector< Path > & paths, Coords src, int srcNum, Coords &dst, int dstNum) {
  int pathCount;
  int localConnection = dst.coords[TIER1] % ariesPerGroup;
  //find total number of paths
  if(localConnection/maxCoords.coords[TIER3] == src.coords[TIER2] || localConnection % maxCoords.coords[TIER3] == src.coords[TIER3]) { //in same TIER2 or aligned on TIER3
    pathCount = 1;
  } else {
    pathCount = 2;
  }
  int remoteConnection = src.coords[TIER1] % ariesPerGroup;
  if(pathCount == 1 && !(remoteConnection/maxCoords.coords[TIER3] == dst.coords[TIER2] || remoteConnection % maxCoords.coords[TIER3] == dst.coords[TIER3])) {
    pathCount = 2;
  }

  paths.resize(pathCount);
  Coords interNode;
  int interNum;
  //add hops within the src group
  if(localConnection == aries[srcNum].localRank) { //src connects to destination group
    interNode = src;
    interNum = srcNum;
  } else {
    interNode.coords[TIER1] = src.coords[TIER1];
    interNode.coords[TIER2] = localConnection/maxCoords.coords[TIER3];
    interNode.coords[TIER3] = localConnection % maxCoords.coords[TIER3];
    coordstoAriesRank(interNum, interNode);
    if(interNode.coords[TIER2] == src.coords[TIER2]) { //if use only 1 GREEN link
      Hop h;
      h.aries = srcNum;
      h.link = interNode.coords[TIER3];
      for(int i = 0; i < pathCount; i++) {
        paths[i].push_back(h);
      }
    } else if(interNode.coords[TIER3] == src.coords[TIER3]) { //if use only 1 BLACK link
      Hop h;
      h.aries = srcNum;
      h.link = BLACK_START + interNode.coords[TIER2];
      for(int i = 0; i < pathCount; i++) {
        paths[i].push_back(h);
      }
    } else {
      addToPath(paths, src, srcNum, interNode, 0, 1);
    }
  }

  //add a BLUE connection to all paths
  Hop h;
  h.aries = interNum;
  h.link = BLUE_START + dst.coords[TIER1]/ariesPerGroup;
  for(int i = 0; i < pathCount; i++) {
    paths[i].push_back(h);
  }

  //add hops within the dst group
  if(remoteConnection != aries[dstNum].localRank) {
    interNode.coords[TIER1] = dst.coords[TIER1];
    interNode.coords[TIER2] = remoteConnection/maxCoords.coords[TIER3];
    interNode.coords[TIER3] = remoteConnection % maxCoords.coords[TIER3];
    coordstoAriesRank(interNum, interNode);
    if(interNode.coords[TIER2] == dst.coords[TIER2]) { //if use only 1 GREEN LINK
      Hop h;
      h.aries = interNum;
      h.link = dst.coords[TIER3];
      for(int i = 0; i < pathCount; i++) {
        paths[i].push_back(h);
      }
    } else if(interNode.coords[TIER3] == dst.coords[TIER3]) { //if use only 1 BLACK link
      Hop h;
      h.aries = interNum;
      h.link = BLACK_START + dst.coords[TIER2];
      for(int i = 0; i < pathCount; i++) {
        paths[i].push_back(h);
      }
    } else {
      addToPath(paths, interNode, interNum, dst, 0, 1);
    }
  }
}

inline void getRandomPath(Coords src, int srcNum, Coords &dst, int dstNum, Path & p, unsigned *seed) {
  //same group
  if(src.coords[TIER1] == dst.coords[TIER1]) {
    int valiantNum = myrand() % ariesPerGroup;
    Coords valiantNode;
    valiantNode.coords[TIER1] = src.coords[TIER1];
    valiantNode.coords[TIER2] = valiantNum / maxCoords.coords[TIER3];
    valiantNode.coords[TIER3] = valiantNum % maxCoords.coords[TIER3];
    coordstoAriesRank(valiantNum, valiantNode);
    if(valiantNum != srcNum) {
      addIntraPath(src, srcNum, valiantNode, valiantNum, p, seed);
    }
    if(valiantNum != dstNum) {
      addIntraPath(valiantNode, valiantNum, dst, dstNum, p, seed);
    }
  } else { //different groups
    int valiantNum = myrand() % maxCoords.coords[TIER1];
    Coords valiantNode;
    bool noValiantS = false;
    bool noValiantD = false;

    if(valiantNum == src.coords[TIER1]) {
      noValiantS = true;
      valiantNode = src;
      valiantNum = srcNum;
    } else if(valiantNum == dst.coords[TIER1]) {
      noValiantD = true;
      valiantNode = dst;
      valiantNum = dstNum;
    } else {
      valiantNode.coords[TIER1] = valiantNum;
      valiantNum = myrand() % ariesPerGroup;
      valiantNode.coords[TIER2] = valiantNum / maxCoords.coords[TIER3];
      valiantNode.coords[TIER3] = valiantNum % maxCoords.coords[TIER3];
      coordstoAriesRank(valiantNum, valiantNode);
    }

    if(!noValiantS) {
     addInterPath(src, srcNum, valiantNode, valiantNum, p, seed);
    }

    if(!noValiantD) {
      addInterPath(valiantNode, valiantNum, dst, dstNum, p, seed);
    }
  }
}

inline void addIntraPath(Coords & src, int srcNum, Coords &dst, int dstNum, Path &p, unsigned *seed) {
  Hop h;
  if(src.coords[TIER2] == dst.coords[TIER2]) { //if use 1 GREEN
    h.aries = srcNum;
    h.link = dst.coords[TIER3];
    p.push_back(h);
  } else if (src.coords[TIER3] == dst.coords[TIER3]) { //if use 1 BLACK
    h.aries = srcNum;
    h.link = BLACK_START + dst.coords[TIER2];
    p.push_back(h);
  } else { //two paths, choose 1
    if(myrand() % 2) { //first GREEN, then BLACK
      h.aries = srcNum;
      h.link = dst.coords[TIER3];
      p.push_back(h);
      src.coords[TIER3] = dst.coords[TIER3];
      coordstoAriesRank(srcNum, src);
      h.aries = srcNum;
      h.link = BLACK_START + dst.coords[TIER2];
      p.push_back(h);
    } else { //first BLACK, then GREEN
      h.aries = srcNum;
      h.link = BLACK_START + dst.coords[TIER2];
      p.push_back(h);
      src.coords[TIER2] = dst.coords[TIER2];
      coordstoAriesRank(srcNum, src);
      h.aries = srcNum;
      h.link = dst.coords[TIER3];
      p.push_back(h);
    }
  }
}

inline void addInterPath(Coords & src, int srcNum, Coords &dst, int dstNum, Path &p, unsigned * seed) {
    Coords interNode;
    int interNum;

    int localConnection = dst.coords[TIER1] % ariesPerGroup;
    if(localConnection == aries[srcNum].localRank) {
      interNode = src;
      interNum = srcNum;
    } else {
      interNode.coords[TIER1] = src.coords[TIER1];
      interNode.coords[TIER2] = localConnection / maxCoords.coords[TIER3];
      interNode.coords[TIER3] = localConnection % maxCoords.coords[TIER3];
      coordstoAriesRank(interNum, interNode);
      addIntraPath(src, srcNum, interNode, interNum, p, seed);
    }

    //BLUE link
    Hop h;
    h.aries = interNum;
    h.link = BLUE_START + dst.coords[TIER1]/ariesPerGroup;
    p.push_back(h);

    localConnection = src.coords[TIER1] % ariesPerGroup;
    if(localConnection != aries[dstNum].localRank) {
      interNode.coords[TIER1] = dst.coords[TIER1];
      interNode.coords[TIER2] = localConnection / maxCoords.coords[TIER3];
      interNode.coords[TIER3] = localConnection % maxCoords.coords[TIER3];
      coordstoAriesRank(interNum, interNode);
      addIntraPath(interNode, interNum, dst, dstNum, p, seed);
    }
}

double *linkLoads, *pciLoads;
int round;

void model() {

  linkLoads = new double[numAries*BLUE_END];
  pciLoads  = new double[numAries*8];
  unsigned int seed = primes[myRank];

  //we do not have enough memory to store every path for every message
  //hence, we repeat the entire computation :)
  for(round = 0; round < 2; round++) {
    memset(aries, 0, numAries*sizeof(Aries));
    //initialize upper bounds
    for(int i = 0; i < numAries; i++) {
      aries[i].localRank = coords[i].coords[TIER2] * maxCoords.coords[TIER3] + coords[i].coords[TIER3];
      for(int j = 0; j < BLUE_END; j++) {
        if(j < GREEN_END) aries[i].linksB[j] = GREEN_BW/(myreal)NUM_ITERS;
        else if(j < BLACK_END) aries[i].linksB[j] = BLACK_BW/(myreal)NUM_ITERS;
        else aries[i].linksB[j] = BLUE_BW/(myreal)NUM_ITERS;
      }
      for(int j = 0; j < 4; j++) {
        aries[i].pciSB[j] = PCI_BW/(myreal)NUM_ITERS;
        aries[i].pciRB[j] = PCI_BW/(myreal)NUM_ITERS;
      }
    }

    mysrand(seed);
    // intial set up
    addPathsToMsgs();
    //set load, expansion
    for(size_t m = 0; m < msgsV.size(); m++) {
      Msg &currmsg = msgsV[m];
      currmsg.expand.clear();
      currmsg.loads.clear();
      currmsg.expand.resize(PATHS_PER_ITER, true);
      currmsg.loads.resize(PATHS_PER_ITER, currmsg.bytes*1.0/PATHS_PER_ITER);
    }

    markExpansionRequests();
    updateMessageAndLinks();

    bool expand = true;

    int iter;
    for(iter = 0; iter < MAX_ITERS && expand; iter++) {
      expand = false;
      if(!myRank && iter % 50 == 0)
        printf("Iter %d\n",iter);

      unsigned long long linkCount = 0;
      unsigned long long pciCount = 0;
      for(int i = 0; i < numAries; i++) {
        for(int j = 0; j < 4; j++) {
          pciLoads[pciCount++] = aries[i].pciSB_t[j];
          pciLoads[pciCount++] = aries[i].pciRB_t[j];
        }
        for(int j = 0; j < BLUE_END; j++) {
          linkLoads[linkCount++] = aries[i].linksB_t[j];
        }
      }

      MPI_Allreduce(MPI_IN_PLACE, linkLoads, numAries*BLUE_END, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      MPI_Allreduce(MPI_IN_PLACE, pciLoads, numAries*8, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

      linkCount = 0;
      pciCount = 0;
      for(int i = 0; i < numAries; i++) {
        for(int j = 0; j < 4; j++) {
          aries[i].pciSB_t[j] = pciLoads[pciCount++];
          aries[i].pciRB_t[j] = pciLoads[pciCount++];
        }
        for(int j = 0; j < BLUE_END; j++) {
          aries[i].linksB_t[j] = linkLoads[linkCount++];
        }
      }

      for(int i = 0; i < numAries; i++) {
        for(int j = 0; j < 4; j++) {
          aries[i].pciSB[j] -= aries[i].pciSB_t[j];
          aries[i].pciRB[j] -= aries[i].pciRB_t[j];
          aries[i].pciSB_t[j] = aries[i].pciSO[j] = 0;
          aries[i].pciRB_t[j] = aries[i].pciRO[j] = 0;
        }
        for(int j = 0; j < BLUE_END; j++) {
          aries[i].linksB[j] -= aries[i].linksB_t[j];
          aries[i].linksB_t[j] = aries[i].linksO[j] = 0;
        }
      }

      //add bandwidth incrementally
      if(iter < NUM_ITERS) {
        for(int i = 0; i < numAries; i++) {
          for(int j = 0; j < BLUE_END; j++) {
            if(j < GREEN_END) aries[i].linksB[j] += GREEN_BW/(myreal)NUM_ITERS;
            else if(j < BLACK_END) aries[i].linksB[j] += BLACK_BW/(myreal)NUM_ITERS;
            else aries[i].linksB[j] += BLUE_BW/(myreal)NUM_ITERS;
          }
          for(int j = 0; j < 4; j++) {
            aries[i].pciSB[j] += PCI_BW/(myreal)NUM_ITERS;
            aries[i].pciRB[j] += PCI_BW/(myreal)NUM_ITERS;
          }
        }
      }
      addPathsToMsgs();
      selectExpansionRequests(expand);
      markExpansionRequests();
      updateMessageAndLinks();
      int expandl = (expand) ? 1 : 0;
      int globale;
      MPI_Allreduce(&expandl, &globale, 1, MPI_INTEGER, MPI_SUM, MPI_COMM_WORLD);
      expand = (globale > 0) ? true : false;
    }
    if(!myRank)
      printf("Number of iterations executed: %d\n", iter);
  }

  unsigned long long linkCount = 0;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < BLUE_END; j++) {
      linkLoads[linkCount++] = aries[i].linksSum[j];
    }
  }

  MPI_Allreduce(MPI_IN_PLACE, linkLoads, numAries*BLUE_END, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  linkCount = 0;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < BLUE_END; j++) {
      aries[i].linksO[j] = linkLoads[linkCount++];
    }
  }

  if(myRank) {
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    exit(0);
  }

  printStats();
}

inline void addPathsToMsgs() {
  for(size_t m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    currmsg.paths.clear();
#if ! BIASED_ROUTING
    currmsg.paths.resize(PATHS_PER_ITER);
    for(int i = 0; i < PATHS_PER_ITER; i++) {
      getRandomPath(src, currmsg.src, dst, currmsg.dst, currmsg.paths[i]);
    }
#else 
    // add upto 2 direct paths
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]) {
      currmsg.paths.resize(1);
      currmsg.paths[0].resize(1);
      currmsg.paths[0][0].aries = currmsg.src;
      currmsg.paths[0][0].link = dst.coords[TIER3]; //GREEN
    } else if(src.coords[TIER1] == dst.coords[TIER1]) {  //in the same second tier
      if(src.coords[TIER3] == dst.coords[TIER3]) { //aligned on tier3 - direct connection
        currmsg.paths.resize(1);
        currmsg.paths[0].resize(1);
        currmsg.paths[0][0].aries = currmsg.src;
        currmsg.paths[0][0].link = BLACK_START + dst.coords[TIER2];
      } else { // else two paths of length 2
        currmsg.paths.resize(2);
        addToPath(currmsg.paths, src, currmsg.src, dst, 0, 1);
      }
    } else {
      addFullPaths(currmsg.paths, src, currmsg.src, dst, currmsg.dst);
    };
    //add rest from indirect paths
    currmsg.dcount = currmsg.paths.size();
    currmsg.paths.resize(PATHS_PER_ITER);
    for(int i = currmsg.dcount; i < PATHS_PER_ITER; i++) {
      getRandomPath(src, currmsg.src, dst, currmsg.dst, currmsg.paths[i]);
    }
#endif
  }
}

inline void updateMessageAndLinks() {
  unsigned long long linkCount = 0;
  unsigned long long pciCount = 0;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < 4; j++) {
      pciLoads[pciCount++] = aries[i].pciSO[j];
      pciLoads[pciCount++] = aries[i].pciRO[j];
    }
    for(int j = 0; j < BLUE_END; j++) {
      linkLoads[linkCount++] = aries[i].linksO[j];
    }
  }

  MPI_Allreduce(MPI_IN_PLACE, linkLoads, numAries*BLUE_END, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(MPI_IN_PLACE, pciLoads, numAries*8, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  linkCount = 0;
  pciCount = 0;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < 4; j++) {
      aries[i].pciSO[j] = pciLoads[pciCount++];
      aries[i].pciRO[j] = pciLoads[pciCount++];
    }
    for(int j = 0; j < BLUE_END; j++) {
      aries[i].linksO[j] = linkLoads[linkCount++];
    }
  }

  for(size_t m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    for(size_t i = 0; i < currmsg.paths.size(); i++) {
      if(currmsg.expand[i]) {
        myreal min = FLT_MAX;
        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          min = MIN(min, (currmsg.loads[i]/aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link])*aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link]);
        }
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.src].pciSO[currmsg.srcPCI])*aries[currmsg.src].pciSB[currmsg.srcPCI]);
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.dst].pciRO[currmsg.dstPCI])*aries[currmsg.dst].pciRB[currmsg.dstPCI]);

        for(size_t j = 0; j < currmsg.paths[i].size(); j++) {
          aries[currmsg.paths[i][j].aries].linksB_t[currmsg.paths[i][j].link] += min;
        }
        aries[currmsg.src].pciSB_t[currmsg.srcPCI] += min;
        aries[currmsg.dst].pciRB_t[currmsg.dstPCI] += min;

        if(round == 0) {
          currmsg.bw +=  min;
        } else {
          myreal pathLoad = currmsg.bytes*(min/currmsg.bw);
          for(size_t j = 0; j < currmsg.paths[i].size(); j++) {
            aries[currmsg.paths[i][j].aries].linksSum[currmsg.paths[i][j].link] += pathLoad;
          }
        }
      }
    }
  }
}

inline void markExpansionRequests() {
  //mark all links
  for(size_t m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    for(int i = 0; i < currmsg.paths.size(); i++) {
      if(currmsg.expand[i]) {
        aries[currmsg.src].pciSO[currmsg.srcPCI] += currmsg.loads[i];
        aries[currmsg.dst].pciRO[currmsg.dstPCI] += currmsg.loads[i];
        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link] += currmsg.loads[i];
        }
      }
    }
  }
}

void selectExpansionRequests(bool & expand) {
  for(size_t m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    if(aries[currmsg.src].pciSB[currmsg.srcPCI] < CUTOFF_BW || aries[currmsg.dst].pciRB[currmsg.dstPCI] < CUTOFF_BW) {
      for(int i = 0; i < currmsg.paths.size(); i++) {
        currmsg.expand[i] = false;
      }
      continue;
    }

    myreal sum = 0;

    for(size_t i = 0; i < currmsg.paths.size(); i++) {
      currmsg.loads[i] = FLT_MAX;
      for(int j = 0; j < currmsg.paths[i].size(); j++) {
        currmsg.loads[i] = MIN(currmsg.loads[i], aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link]);
      }
      sum += currmsg.loads[i];
    }

    for(size_t i = 0; i < currmsg.paths.size(); i++) {
      if(currmsg.loads[i] < CUTOFF_BW) {
        currmsg.expand[i] = false;
      } else {
        currmsg.expand[i] = true;
        currmsg.loads[i] = currmsg.bytes*currmsg.loads[i]/sum;
        expand = true;
      }
    }
  }
}
