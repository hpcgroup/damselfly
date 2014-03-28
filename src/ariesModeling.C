#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cassert>
#include <list>
#include <vector>
#include <sys/time.h>
#include <cfloat>

#ifndef STATIC_ROUTING
#define STATIC_ROUTING 0
#endif

#ifndef DIRECT_ROUTING
#define DIRECT_ROUTING 0
#endif

#define USE_THREADS 0

#if !STATIC_ROUTING || DIRECT_ROUTING
#if USE_THREADS
#error Can not compile threaded version without static and indirect routing
#endif
#endif

using namespace std;

#if USE_THREADS
#include <omp.h>
#endif

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
#define BLACK_BW 15360
#define BLUE_BW 12288

#define PCI_BW 16384

#define CUTOFF_BW 1

#define PACKET_SIZE 64

#define NUM_ITERS 30
#define PATHS_PER_ITER 10
#define MAX_ITERS 100

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

 #define A_PRIME 13
 #define B_PRIME 19

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
#if USE_THREADS
   for(int i = 0; i < omp_get_num_threads() - 1; i++) {
     rand_r(seed);
   }
#endif
   return rand_r(seed);
}

// coordinates
typedef struct Coords {
  int coords[NUM_COORDS];
} Coords;

typedef struct Aries {
  float linksO[32]; // 0-15 Green, 16-21 Black
  float linksB[32]; //remaining link bandwidth
  float pciSO[4], pciRO[4]; //send and recv PCI
  float pciSB[4], pciRB[4]; //remaining PCI bandwidth
  int localRank; //rank within the group
#if !STATIC_ROUTING && !DIRECT_ROUTING
  float linksSum[32]; //needed to continously get traffic
#endif
} Aries;

typedef struct Hop {
  int aries, link;
} Hop;

typedef vector<Hop> Path;

// a message
typedef struct Msg {
  int src, dst;
  int srcPCI, dstPCI;
  float bytes; //in MB
  float bw; //allocated
  vector< Path > paths;
  vector< bool > expand;
  vector< float > loads;
  vector< float > allocated;
} Msg;

int numMsgs; // number of messages to be sent
list<Msg> msgs; // messages left to be routed
int numAries; // number of Aries in the system
int numPEs; // number of ranks = numAries * product of last 2 coords
Coords maxCoords; //dimensionality
Coords *coords; // rank to coordinates
Aries *aries; // link status of current nodes
int ariesPerGroup;

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

  if(argc == 1) {
    printf("Usage: %s <conffile> <mapfile> <commfile>\n", argv[0]);
    exit(1);
  }

  printf("Processing command line and conffile\n");

  FILE *conffile = fopen(argv[1],"r");
  nulltest((void*)conffile, "configuration file");

  FILE *mapfile = fopen(argv[2],"r");

  FILE *commfile = fopen(argv[3], "r");
  nulltest((void*)commfile, "communication file");

  fscanf(conffile, "%d", &numAries);
  positivetest((double)numAries, "number of Aries routers");

  for(int i = 0; i < NUM_COORDS; i++) {
    fscanf(conffile, "%d", &maxCoords.coords[i]);
    positivetest((double)maxCoords.coords[i], "a dimension");
  }
  ariesPerGroup = maxCoords.coords[TIER2] * maxCoords.coords[TIER3];

  assert(numAries == (ariesPerGroup*maxCoords.coords[TIER1]));

  numPEs = numAries * maxCoords.coords[NUM_LEVELS] * maxCoords.coords[NUM_LEVELS + 1];

  aries = new Aries[numAries];
  nulltest((void*)aries, "aries status array");

  coords = new Coords[numPEs];
  nulltest((void*)coords, "coordinates of PEs");

  /* Read the mapping of MPI ranks to hardware nodes */
  if(mapfile == NULL) {
    printf("Mapfile not provided; using default mapping\n");
    for(int i = 0; i < numPEs; i++) {
      int rank = i;
      for(int j = NUM_COORDS - 1; j >= 0; j--) {
        coords[i].coords[j] = rank % maxCoords.coords[j];
        rank /= maxCoords.coords[j];
      }
    }
  } else {
    printf("Reading mapfile\n");
    for(int i = 0; i < numPEs; i++) {
      for(int j = 0; j < NUM_COORDS; j++) {
        fscanf(mapfile, "%d", &coords[i].coords[j]);
      }
    }
  }

  numMsgs = 0;
  double sum = 0;

  /* Read the communication graph which is in edge-list format */
  printf("Reading messages\n");
  float MB = 1024 * 1024;
  struct timeval startRead, endRead;
  gettimeofday(&startRead, NULL);
  while(!feof(commfile)) {
    Msg newmsg;
    fscanf(commfile, "%d %d %f\n", &newmsg.src, &newmsg.dst, &newmsg.bytes);
    newmsg.bytes /= MB;
    newmsg.srcPCI = coords[newmsg.src].coords[NUM_LEVELS];
    newmsg.dstPCI = coords[newmsg.dst].coords[NUM_LEVELS];
    coordstoAriesRank(newmsg.src, coords[newmsg.src]);
    coordstoAriesRank(newmsg.dst, coords[newmsg.dst]);
    newmsg.bw = 0;
    msgs.push_back(newmsg);
    sum += newmsg.bytes;
  }
  numMsgs = msgs.size();

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
  calculateAndPrint(startRead, endRead, "time to read communication pattern");

  printf("Modeling for following system will be performed:\n");
  printf("numlevels: %d, dims: %d %d %d %d %d\n", NUM_LEVELS, maxCoords.coords[0], maxCoords.coords[1], maxCoords.coords[2], maxCoords.coords[3], maxCoords.coords[4]);
  printf("numAries: %d, numPEs: %d, numMsgs: %d, total volume: %.0lf MB\n", numAries, numPEs, numMsgs, sum);

  printf("Starting modeling \n");
  struct timeval startModel, endModel;
  gettimeofday(&startModel, NULL);
  model();
  gettimeofday(&endModel, NULL);
  calculateAndPrint(startModel, endModel, "time to model");

  fclose(conffile);
  if(mapfile != NULL)
    fclose(mapfile);
  fclose(commfile);
  delete[] aries;
  delete[] coords;
}

inline void printStats() {
  float maxLoad = 0, minLoad = FLT_MAX, maxPCI = 0, minPCI = FLT_MAX;
  double totalLinkLoad = 0, totalPCILoad = 0;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < 4; j++) {
      maxPCI = MAX(maxPCI, aries[i].pciSO[j]);
      minPCI = MIN(minPCI, aries[i].pciSO[j]);
      totalPCILoad += aries[i].pciSO[j];

      maxPCI = MAX(maxPCI, aries[i].pciRO[j]);
      minPCI = MIN(minPCI, aries[i].pciRO[j]);
      totalPCILoad += aries[i].pciRO[j];
    }

    for(int j = 0; j < BLUE_END; j++) {
      maxLoad = MAX(maxLoad, aries[i].linksO[j]);
      minLoad = MIN(minLoad, aries[i].linksO[j]);
      totalLinkLoad += aries[i].linksO[j];
    }
  }

  long long totalLinks = numAries*(15+5+((numAries/ariesPerGroup) + (numAries % ariesPerGroup) ? 1 : 0));

  printf("******************Summary*****************\n");
  printf("STATIC_ROUTING %d DIRECT_ROUTING %d\n",STATIC_ROUTING,DIRECT_ROUTING);
  printf("maxLoad %.2f MB -- minLoad %.2f MB\n",maxLoad,minLoad);
  printf("maxPCI %.2f MB -- minPCI %.2f MB\n",maxPCI,minPCI);
  printf("averageLinkLoad %.2lf averagePCILoad %.2lf\n", totalLinkLoad/totalLinks, totalPCILoad/(numAries*8));
}

#if STATIC_ROUTING
/* for static routing, the linksO and pci(S/R)O stores the volumne of traffic that passes
through them in MB; linksB and pci(S/R)B stores the total bandwidth in MB */
void model() {
  memset(aries, 0, numAries*sizeof(Aries));
  //initialize upper bounds
  for(int i = 0; i < numAries; i++) {
    aries[i].localRank = coords[i].coords[TIER2] * maxCoords.coords[TIER3] + coords[i].coords[TIER3];
    for(int j = 0; j < BLUE_END; j++) {
      if(j < GREEN_END) aries[i].linksB[j] = GREEN_BW;
      else if(j < BLACK_END) aries[i].linksB[j] = BLACK_BW;
      else aries[i].linksB[j] = BLUE_BW;
    }
    for(int j = 0; j < 4; j++) {
      aries[i].pciSB[j] = PCI_BW;
      aries[i].pciRB[j] = PCI_BW;
    }
  }

  //get initial counts
  addLoads();
  printStats();
}

#if DIRECT_ROUTING

inline void addLoads() {
  addPathsToMsgs();
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
    aries[currmsg.src].pciSO[currmsg.srcPCI] += currmsg.bytes;
    aries[currmsg.dst].pciRO[currmsg.dstPCI] += currmsg.bytes;
    float perPath = currmsg.bytes/currmsg.paths.size();
    for(int i = 0; i < currmsg.paths.size(); i++) {
      for(int j = 0; j < currmsg.paths[i].size(); j++) {
        aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link] += perPath;
      }
    }
  }
}

inline void addPathsToMsgs() {
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); ) {
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    //if same aries, continue
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]
      && src.coords[TIER3] == dst.coords[TIER3]) {
      msgit = msgs.erase(msgit );
      continue;
    }

    //in the same lowest tier - direct connection
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
    }
    msgit++;
  }
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
#else // not using DIRECT_ROUTING

int primes[] = {2, 1399, 2161, 4051, 5779, 6911, 7883, 10141, 12163, 13309, 15121, 16889, 18311, 20443, 21169, 23029};
inline void addLoads() {
  unsigned seed = time(NULL);
  mysrand(seed);
  float MB = 1024*1024;
  float perPacket = PACKET_SIZE/MB;
  int count = 0;
  int printFreq = MAX(10, numMsgs/10);
#if USE_THREADS
#pragma omp parallel
{
  float **tempAries = new float*[numAries];
  for(int i = 0; i < numAries; i++) {
    tempAries[i] = new float[BLUE_END];
    for(int j = 0; j < BLUE_END; j++) {
      tempAries[i][j] = 0;
    }
  }
  #pragma omp master
  {
    printf("Number of threads %d\n",omp_get_num_threads());
  }

  unsigned lseed = primes[0];
  for(int i = 0; i < omp_get_thread_num(); i++) {
    rand_r(&lseed);
  }
  #endif

  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
#if USE_THREADS
    #pragma omp master
    {
#endif
      count++;
      if(count % printFreq == 0) {
        printf("Modeling at msg num %d\n", count);
      }
#if USE_THREADS
    }
#endif
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]
      && src.coords[TIER3] == dst.coords[TIER3]) continue;

#if USE_THREADS
    #pragma omp master
    {
#endif
      aries[currmsg.src].pciSO[currmsg.srcPCI] += currmsg.bytes;
      aries[currmsg.dst].pciRO[currmsg.dstPCI] += currmsg.bytes;
#if USE_THREADS
    }
#endif
    int numPackets = currmsg.bytes/perPacket;
    numPackets = MAX(1, numPackets);

    Path p;
#if USE_THREADS
#pragma omp for
#endif
    for(int i = 0; i < numPackets; i++) {
      p.clear();
#if USE_THREADS
      getRandomPath(src, currmsg.src, dst, currmsg.dst, p, &lseed);
#else
      getRandomPath(src, currmsg.src, dst, currmsg.dst, p);
#endif
      for(int j = 0; j < p.size(); j++) {
#if USE_THREADS
        tempAries[p[j].aries][p[j].link] += perPacket;
#else
        aries[p[j].aries].linksO[p[j].link] += perPacket;
#endif
      }
    }
  }
#if USE_THREADS
  #pragma omp critical
  {
    for(int i = 0; i < numAries; i++) {
      for(int j = 0; j < BLUE_END; j++) {
        aries[i].linksO[j] +=  tempAries[i][j];
      }
      delete [] tempAries[i];
    }
    delete [] tempAries;
  }
}
#endif
}
#endif // not using DIRECT_ROUTING
#endif // STATIC_ROUTING

inline void getRandomPath(Coords src, int srcNum, Coords &dst, int dstNum, Path & p, unsigned *seed) {
  //same group
  if(src.coords[TIER1] == dst.coords[TIER1]) {
#if USE_THREADS
    int valiantNum = myrand_r(seed) % ariesPerGroup;
#else
    int valiantNum = myrand() % ariesPerGroup;
#endif
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
#if USE_THREADS
    int valiantNum = myrand_r(seed) % maxCoords.coords[TIER1];
#else
    int valiantNum = myrand() % maxCoords.coords[TIER1];
#endif
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
#if USE_THREADS
      valiantNum = myrand_r(seed) % ariesPerGroup;
#else
      valiantNum = myrand() % ariesPerGroup;
#endif
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
#if USE_THREADS
    if(myrand_r(seed) % 2) { //first GREEN, then BLACK
#else
    if(myrand() % 2) { //first GREEN, then BLACK
#endif
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

#if !STATIC_ROUTING
#if DIRECT_ROUTING
void model() {
  memset(aries, 0, numAries*sizeof(Aries));
  //initialize upper bounds
  for(int i = 0; i < numAries; i++) {
    aries[i].localRank = coords[i].coords[TIER2] * maxCoords.coords[TIER3] + coords[i].coords[TIER3];
    for(int j = 0; j < BLUE_END; j++) {
      if(j < GREEN_END) aries[i].linksB[j] = GREEN_BW/(float)NUM_ITERS;
      else if(j < BLACK_END) aries[i].linksB[j] = BLACK_BW/(float)NUM_ITERS;
      else aries[i].linksB[j] = BLUE_BW/(float)NUM_ITERS;
    }
    for(int j = 0; j < 4; j++) {
      aries[i].pciSB[j] = PCI_BW/(float)NUM_ITERS;
      aries[i].pciRB[j] = PCI_BW/(float)NUM_ITERS;
    }
  }

  // intial set up
  addPathsToMsgs();
  markExpansionRequests();
  updateMessageAndLinks();

  bool expand = true;

  int iter;
  for(iter = 0; iter < MAX_ITERS && expand; iter++) {
    expand = false;

    //reset needed to zero
    for(int i = 0; i < numAries; i++) {
      for(int j = 0; j < 4; j++) {
        aries[i].pciSO[j] = 0;
        aries[i].pciRO[j] = 0;
      }
      for(int j = 0; j < BLUE_END; j++) {
        aries[i].linksO[j] = 0;
      }
    }

    //add bandwidth incrementally
    if(iter < NUM_ITERS) {
      for(int i = 0; i < numAries; i++) {
        for(int j = 0; j < BLUE_END; j++) {
          if(j < GREEN_END) aries[i].linksB[j] += GREEN_BW/(float)NUM_ITERS;
          else if(j < BLACK_END) aries[i].linksB[j] += BLACK_BW/(float)NUM_ITERS;
          else aries[i].linksB[j] += BLUE_BW/(float)NUM_ITERS;
        }
        for(int j = 0; j < 4; j++) {
          aries[i].pciSB[j] += PCI_BW/(float)NUM_ITERS;
          aries[i].pciRB[j] += PCI_BW/(float)NUM_ITERS;
        }
      }
    }
    selectExpansionRequests(expand);
    markExpansionRequests();
    updateMessageAndLinks();
  }

  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < 4; j++) {
      aries[i].pciSO[j] = 0;
      aries[i].pciRO[j] = 0;
    }
    for(int j = 0; j < BLUE_END; j++) {
      aries[i].linksO[j] = 0;
    }
  }

  printf("Number of iterations executed: %d\n", iter);
  computeSummary();
  printStats();
}

inline void updateMessageAndLinks() {
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
    for(int i = 0; i < currmsg.paths.size(); i++) {
      if(currmsg.expand[i]) {
        float min = FLT_MAX;
        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          min = MIN(min, (currmsg.loads[i]/aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link])*aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link]);
        }
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.src].pciSO[currmsg.srcPCI])*aries[currmsg.src].pciSB[currmsg.srcPCI]);
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.dst].pciRO[currmsg.dstPCI])*aries[currmsg.dst].pciRB[currmsg.dstPCI]);

        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link] -= min;
        }
        aries[currmsg.src].pciSB[currmsg.srcPCI] -= min;
        aries[currmsg.dst].pciRB[currmsg.dstPCI] -= min;
        currmsg.bw +=  min;
        currmsg.allocated[i] += min;
      }
    }
  }
}

inline void computeSummary() {
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
    aries[currmsg.src].pciSO[currmsg.srcPCI] += currmsg.bytes;
    aries[currmsg.dst].pciRO[currmsg.dstPCI] += currmsg.bytes;
    for(int i = 0; i < currmsg.paths.size(); i++) {
      float pathLoad = currmsg.bytes*(currmsg.allocated[i]/currmsg.bw);
      for(int j = 0; j < currmsg.paths[i].size(); j++) {
        aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link] += pathLoad;
      }
    }
  }
}

inline void addPathsToMsgs() {
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end();) {
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    //if same aries, remove message from list
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]
      && src.coords[TIER3] == dst.coords[TIER3]) {
       msgit = msgs.erase(msgit);
       continue;
    }

    //in the same lowest tier - direct connection
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]) {
      currmsg.paths.resize(1);
      currmsg.paths[0].resize(1);
      currmsg.paths[0][0].aries = currmsg.src;
      currmsg.paths[0][0].link = dst.coords[TIER3]; //GREEN
      currmsg.expand.push_back(true);
      currmsg.loads.push_back(1.0);
      currmsg.allocated.push_back(0.0);
    } else if(src.coords[TIER1] == dst.coords[TIER1]) { //in the same second tier
      if(src.coords[TIER3] == dst.coords[TIER3]) { //aligned on tier3 - direct connection
        currmsg.paths.resize(1);
        currmsg.paths[0].resize(1);
        currmsg.paths[0][0].aries = currmsg.src;
        currmsg.paths[0][0].link = BLACK_START + dst.coords[TIER2];
        currmsg.expand.push_back(true);
        currmsg.loads.push_back(1.0);
        currmsg.allocated.push_back(0.0);
      } else {  // else two paths of length 2
        currmsg.paths.resize(2);
        addToPath(currmsg.paths, src, currmsg.src, dst, 0, 1);
        currmsg.expand.resize(2, true);
        currmsg.loads.resize(2, 0.5);
        currmsg.allocated.resize(2, 0.0);
      }
    } else {
      addFullPaths(currmsg.paths, src, currmsg.src, dst, currmsg.dst);
      currmsg.expand.resize(currmsg.paths.size(), true);
      currmsg.loads.resize(currmsg.paths.size(), 1.0/currmsg.paths.size());
      currmsg.allocated.resize(currmsg.paths.size(), 0.0);
    }
    msgit++;
  }
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
  if(localConnection/maxCoords.coords[TIER3] == src.coords[TIER2] || localConnection % maxCoords.coords[TIER3] == src.coords[TIER3]) {
    pathCount = 1;
  } else {
    pathCount = 2;
  }
  int remoteConnection = src.coords[TIER1] % ariesPerGroup;
  if(remoteConnection/maxCoords.coords[TIER3] == dst.coords[TIER2] || remoteConnection % maxCoords.coords[TIER3] == dst.coords[TIER3]) {
    pathCount *= 1;
  } else {
    pathCount *= 2;
  }

  paths.resize(pathCount);
  Coords interNode; int interNum;
  //add hops within the src group
  if(localConnection == aries[srcNum].localRank) {
    interNode = src;
    interNum = srcNum;
  } else {
    interNode.coords[TIER1] = src.coords[TIER1];
    interNode.coords[TIER2] = localConnection/maxCoords.coords[TIER3];
    interNode.coords[TIER3] = localConnection % maxCoords.coords[TIER3];
    coordstoAriesRank(interNum, interNode);
    if(interNode.coords[TIER2] == src.coords[TIER2]) { //if use only 1 GREEN LINK
      Hop h;
      h.aries = srcNum;
      h.link = interNode.coords[TIER3];
      for(int i = 0; i < pathCount; i++) {
        paths[i].push_back(h);
      }
    } else if(interNode.coords[TIER3] == src.coords[TIER3]) {
      Hop h;
      h.aries = srcNum;
      h.link = BLACK_START + interNode.coords[TIER2];
      for(int i = 0; i < pathCount; i++) {
        paths[i].push_back(h);
      }
    } else {
      for(int i = 0; i < pathCount/2; i++) {
        addToPath(paths, src, srcNum, interNode, i, ((pathCount > 2) ? i + 2 : i + 1));
      }
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
      h.link = dst.coords[TIER3]; //GREEN
      for(int i = 0; i < pathCount; i++) {
        paths[i].push_back(h);
      }
    } else if(interNode.coords[TIER3] == dst.coords[TIER3]) {
      Hop h;
      h.aries = interNum;
      h.link = BLACK_START + dst.coords[TIER2];
      for(int i = 0; i < pathCount; i++) {
        paths[i].push_back(h);
      }
    } else {
      for(int i = 0; i < pathCount; i += 2) {
        addToPath(paths, interNode, interNum, dst, i, i+1);
      }
    }
  }
}
#else

int round;

void model() {
  long long seed = time(NULL);

  //delete self messages
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); ) {
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
     if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]
      && src.coords[TIER3] == dst.coords[TIER3]) {
      msgit = msgs.erase(msgit );
      continue;
    } else msgit++;
  }

  //we do not have enough memory to store every path for every message
  //hence, we repeat the entire computation :)
  for(round = 0; round < 2; round++) {
    memset(aries, 0, numAries*sizeof(Aries));
    //initialize upper bounds
    for(int i = 0; i < numAries; i++) {
      aries[i].localRank = coords[i].coords[TIER2] * maxCoords.coords[TIER3] + coords[i].coords[TIER3];
      for(int j = 0; j < BLUE_END; j++) {
        if(j < GREEN_END) aries[i].linksB[j] = GREEN_BW/(float)NUM_ITERS;
        else if(j < BLACK_END) aries[i].linksB[j] = BLACK_BW/(float)NUM_ITERS;
        else aries[i].linksB[j] = BLUE_BW/(float)NUM_ITERS;
      }
      for(int j = 0; j < 4; j++) {
        aries[i].pciSB[j] = PCI_BW/(float)NUM_ITERS;
        aries[i].pciRB[j] = PCI_BW/(float)NUM_ITERS;
      }
    }

    mysrand(seed);
    // intial set up
    addPathsToMsgs();
    //set load, expansion
    for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
      Msg &currmsg = *msgit;
      currmsg.expand.resize(PATHS_PER_ITER, true);
      currmsg.loads.resize(PATHS_PER_ITER, 1.0/PATHS_PER_ITER);
    }

    markExpansionRequests();
    updateMessageAndLinks();

    bool expand = true;

    int iter;
    for(iter = 0; iter < MAX_ITERS && expand; iter++) {
      expand = false;

      //reset needed to zero
      for(int i = 0; i < numAries; i++) {
        for(int j = 0; j < 4; j++) {
          aries[i].pciSO[j] = 0;
          aries[i].pciRO[j] = 0;
        }
        for(int j = 0; j < BLUE_END; j++) {
          aries[i].linksO[j] = 0;
        }
      }

      //add bandwidth incrementally
      if(iter < NUM_ITERS) {
        for(int i = 0; i < numAries; i++) {
          for(int j = 0; j < BLUE_END; j++) {
            if(j < GREEN_END) aries[i].linksB[j] += GREEN_BW/(float)NUM_ITERS;
            else if(j < BLACK_END) aries[i].linksB[j] += BLACK_BW/(float)NUM_ITERS;
            else aries[i].linksB[j] += BLUE_BW/(float)NUM_ITERS;
          }
          for(int j = 0; j < 4; j++) {
            aries[i].pciSB[j] += PCI_BW/(float)NUM_ITERS;
            aries[i].pciRB[j] += PCI_BW/(float)NUM_ITERS;
          }
        }
      }
      selectExpansionRequests(expand);
      markExpansionRequests();
      updateMessageAndLinks();
    }
    printf("Number of iterations executed: %d\n", iter);
  }

  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < BLUE_END; j++) {
      aries[i].linksO[j] = aries[i].linksSum[j];
    }
  }

  printStats();
}

inline void addPathsToMsgs() {
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    currmsg.paths.clear();
    currmsg.paths.resize(PATHS_PER_ITER);
    for(int i = 0; i < PATHS_PER_ITER; i++) {
      getRandomPath(src, currmsg.src, dst, currmsg.dst, currmsg.paths[i]);
    }
  }
}

inline void updateMessageAndLinks() {
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
    for(int i = 0; i < currmsg.paths.size(); i++) {
      if(currmsg.expand[i]) {
        float min = FLT_MAX;
        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          min = MIN(min, (currmsg.loads[i]/aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link])*aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link]);
        }
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.src].pciSO[currmsg.srcPCI])*aries[currmsg.src].pciSB[currmsg.srcPCI]);
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.dst].pciRO[currmsg.dstPCI])*aries[currmsg.dst].pciRB[currmsg.dstPCI]);

        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link] -= min;
        }
        aries[currmsg.src].pciSB[currmsg.srcPCI] -= min;
        aries[currmsg.dst].pciRB[currmsg.dstPCI] -= min;

        if(round == 0) {
          currmsg.bw +=  min;
        } else {
          float pathLoad = currmsg.bytes*(min/currmsg.bw);
          for(int j = 0; j < currmsg.paths[i].size(); j++) {
            aries[currmsg.paths[i][j].aries].linksSum[currmsg.paths[i][j].link] += pathLoad;
          }
        }
      }
    }
  }
}

#endif // !DIRECT_ROUTING

inline void markExpansionRequests() {
  //mark all links
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
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
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;

    if(aries[currmsg.src].pciSB[currmsg.srcPCI] < CUTOFF_BW || aries[currmsg.dst].pciRB[currmsg.dstPCI] < CUTOFF_BW) {
      for(int i = 0; i < currmsg.paths.size(); i++) {
        currmsg.expand[i] = false;
      }
      continue;
    }

    float sum = 0;

    for(int i = 0; i < currmsg.paths.size(); i++) {
      currmsg.loads[i] = FLT_MAX;
      for(int j = 0; j < currmsg.paths[i].size(); j++) {
        currmsg.loads[i] = MIN(currmsg.loads[i], aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link]);
      }
      sum += currmsg.loads[i];
    }

    for(int i = 0; i < currmsg.paths.size(); i++) {
      if(currmsg.loads[i] < CUTOFF_BW) {
        currmsg.expand[i] = false;
      } else {
        currmsg.expand[i] = true;
        currmsg.loads[i] = currmsg.loads[i]/sum;
        expand = true;
      }
    }
  }
}
#endif // !STATIC ROUTING
