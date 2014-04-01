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

#ifndef STATIC_ROUTING
#define STATIC_ROUTING 0
#endif

#ifndef DIRECT_ROUTING
#define DIRECT_ROUTING 0
#endif

#ifndef USE_THREADS
#define USE_THREADS 0
#endif

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

#define CUTOFF_BW 0.001
#define NUM_ITERS 50
#define PATHS_PER_ITER 4
#define MAX_ITERS 150

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
#if !STATIC_ROUTING && !DIRECT_ROUTING
  myreal linksSum[32]; //needed to continously get traffic
#endif
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
  vector< bool > expand;
  vector< myreal > loads;
  vector< myreal > allocated;
} Msg;

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

  FILE *commfile = fopen(argv[3], "r");
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
  unsigned long long currentCount = 0;
  while(!feof(commfile)) {
    Msg newmsg;
    fscanf(commfile, "%d %d %lf\n", &newmsg.src, &newmsg.dst, &newmsg.bytes);
    if(currentCount >= end) break;
    if(currentCount < begin) {
      currentCount++;
      continue;
    }
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
  if(myRank) {
    calculateAndPrint(startModel, endModel, "time to model");
  }

  fclose(conffile);
  if(mapfile != NULL)
    fclose(mapfile);
  fclose(commfile);
  delete[] aries;
  delete[] coords;
}

inline void printStats() {
  myreal maxLoad = 0, minLoad = FLT_MAX, maxPCI = 0, minPCI = FLT_MAX;
  double totalLinkLoad = 0, totalPCILoad = 0;
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
  printf("STATIC_ROUTING %d DIRECT_ROUTING %d\n",STATIC_ROUTING,DIRECT_ROUTING);
  printf("maxLoad %.2f MB -- minLoad %.2f MB\n",maxLoad,minLoad);
  printf("averageLinkLoad %.2lf \n", totalLinkLoad/totalLinks);
  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();
  exit(0);
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
  for(vector<Msg>::iterator msgit = msgsV.begin(); msgit != msgsV.end(); msgit++) {
    Msg &currmsg = *msgit;
    myreal perPath = currmsg.bytes/currmsg.paths.size();
    for(int i = 0; i < currmsg.paths.size(); i++) {
      for(int j = 0; j < currmsg.paths[i].size(); j++) {
        aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link] += perPath;
      }
    }
  }

  double *loads = new double[numAries*BLUE_END];
  memset(loads, 0, sizeof(double)*numAries*BLUE_END);
  unsigned long long linkCount = 0;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < BLUE_END; j++) {
      loads[linkCount++] = aries[i].linksO[j];
    }
  }

  double *sumLoads;
  if(!myRank) {
    sumLoads = new double[numAries*BLUE_END];
    memset(sumLoads, 0, sizeof(double)*numAries*BLUE_END);
  }

  MPI_Reduce(loads, sumLoads, numAries*BLUE_END, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  delete[] loads;

  if(!myRank) {
    linkCount = 0;
    for(int i = 0; i < numAries; i++) {
      for(int j = 0; j < BLUE_END; j++) {
        aries[i].linksO[j] = sumLoads[linkCount++];
      }
    }
    delete [] sumLoads;
  } else {
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    exit(0);
  }
}

inline void addPathsToMsgs() {
  for(vector<Msg>::iterator msgit = msgsV.begin(); msgit != msgsV.end(); msgit++ ) {
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];

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
    };
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

int primes[] = {1087,1091,1093,1097,1103,1109,1117,1123,1129,1151,
1153,1163,1171,1181,1187,1193,1201,1213,1217,1223,
1229,1231,1237,1249,1259,1277,1279,1283,1289,1291,
1297,1301,1303,1307,1319,1321,1327,1361,1367,1373,
1381,1399,1409,1423,1427,1429,1433,1439,1447,1451,
1453,1459,1471,1481,1483,1487,1489,1493,1499,1511,
1523,1531,1543,1549,1553,1559,1567,1571,1579,1583,
1597,1601,1607,1609,1613,1619,1621,1627,1637,1657,
1663,1667,1669,1693,1697,1699,1709,1721,1723,1733,
1741,1747,1753,1759,1777,1783,1787,1789,1801,1811,
1823,1831,1847,1861,1867,1871,1873,1877,1879,1889,
1901,1907,1913,1931,1933,1949,1951,1973,1979,1987,
1993,1997,1999,2003,2011,2017,2027,2029,2039,2053,
2063,2069,2081,2083,2087,2089,2099,2111,2113,2129,
2131,2137,2141,2143,2153,2161,2179,2203,2207,2213,
2221,2237,2239,2243,2251,2267,2269,2273,2281,2287,
2293,2297,2309,2311,2333,2339,2341,2347,2351,2357,
2371,2377,2381,2383,2389,2393,2399,2411,2417,2423,
2437,2441,2447,2459,2467,2473,2477,2503,2521,2531,
2539,2543,2549,2551,2557,2579,2591,2593,2609,2617,
2621,2633,2647,2657,2659,2663,2671,2677,2683,2687,
2689,2693,2699,2707,2711,2713,2719,2729,2731,2741,
2749,2753,2767,2777,2789,2791,2797,2801,2803,2819,
2833,2837,2843,2851,2857,2861,2879,2887,2897,2903,
2909,2917,2927,2939,2953,2957,2963,2969,2971,2999,
3001,3011,3019,3023,3037,3041,3049,3061,3067,3079,
3083,3089,3109,3119,3121,3137,3163,3167,3169,3181,
3187,3191,3203,3209,3217,3221,3229,3251,3253,3257,
3259,3271,3299,3301,3307,3313,3319,3323,3329,3331,
3343,3347,3359,3361,3371,3373,3389,3391,3407,3413,
3433,3449,3457,3461,3463,3467,3469,3491,3499,3511,
3517,3527,3529,3533,3539,3541,3547,3557,3559,3571,
3581,3583,3593,3607,3613,3617,3623,3631,3637,3643,
3659,3671,3673,3677,3691,3697,3701,3709,3719,3727,
3733,3739,3761,3767,3769,3779,3793,3797,3803,3821,
3823,3833,3847,3851,3853,3863,3877,3881,3889,3907,
3911,3917,3919,3923,3929,3931,3943,3947,3967,3989,
4001,4003,4007,4013,4019,4021,4027,4049,4051,4057,
4073,4079,4091,4093,4099,4111,4127,4129,4133,4139,
4153,4157,4159,4177,4201,4211,4217,4219,4229,4231,
4241,4243,4253,4259,4261,4271,4273,4283,4289,4297,
4327,4337,4339,4349,4357,4363,4373,4391,4397,4409,
4421,4423,4441,4447,4451,4457,4463,4481,4483,4493,
4507,4513,4517,4519,4523,4547,4549,4561,4567,4583,
4591,4597,4603,4621,4637,4639,4643,4649,4651,4657,
4663,4673,4679,4691,4703,4721,4723,4729,4733,4751,
4759,4783,4787,4789,4793,4799,4801,4813,4817,4831,
4861,4871,4877,4889,4903,4909,4919,4931,4933,4937,
4943,4951,4957,4967,4969,4973,4987,4993,4999,5003,
5009,5011,5021,5023,5039,5051,5059,5077,5081,5087,
5099,5101,5107,5113,5119,5147,5153,5167,5171,5179,
5189,5197,5209,5227,5231,5233,5237,5261,5273,5279,
5281,5297,5303,5309,5323,5333,5347,5351,5381,5387,
5393,5399,5407,5413,5417,5419,5431,5437,5441,5443};

inline void addLoads() {
  myreal MB = 1024*1024;
  myreal perPacket = PACKET_SIZE/MB;
  int count = 0;
  unsigned lseed = primes[myRank];

  if(!myRank)
    printf("Number of MPI ranks %d\n",numRanks);

  unsigned long long totalMsg = msgsV.size();
  unsigned long long printFreq = totalMsg/10;

  for(unsigned long long m = 0; m < totalMsg; m++) {
    if(!myRank) {
      if(m % printFreq == 0) {
        printf("Processing %llu\n", m);
      }
    }
    Msg &currmsg = msgsV[m];
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];

    int numPackets = currmsg.bytes/perPacket;
    numPackets = MAX(1, numPackets);

    Path p;
    for(int i = 0; i < numPackets; i++) {
      p.clear();
      getRandomPath(src, currmsg.src, dst, currmsg.dst, p, &lseed);
      for(int j = 0; j < p.size(); j++) {
        aries[p[j].aries].linksO[p[j].link] += perPacket;
      }
    }
  }
  double *loads = new double[numAries*BLUE_END];
  memset(loads, 0, sizeof(double)*numAries*BLUE_END);
  unsigned long long linkCount = 0;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < BLUE_END; j++) {
      loads[linkCount++] = aries[i].linksO[j];
    }
  }

  double *sumLoads;
  if(!myRank) {
    sumLoads = new double[numAries*BLUE_END];
    memset(sumLoads, 0, sizeof(double)*numAries*BLUE_END);
  }
  MPI_Reduce(loads, sumLoads, numAries*BLUE_END, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  delete[] loads;

  if(!myRank) {
    linkCount = 0;
    for(int i = 0; i < numAries; i++) {
      for(int j = 0; j < BLUE_END; j++) {
        aries[i].linksO[j] = sumLoads[linkCount++];
      }
    }
    delete [] sumLoads;
  } else {
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    exit(0);
  }
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

double *linkLoads, *pciLoads;

void model() {
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

  linkLoads = new double[numAries*BLUE_END];
  pciLoads  = new double[numAries*8];

  // intial set up
  addPathsToMsgs();
  markExpansionRequests();
  updateMessageAndLinks();

  bool expand = true;

  int iter;
  for(iter = 0; iter < MAX_ITERS && expand; iter++) {
    expand = false;

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

  if(!myRank)
    printf("Number of iterations executed: %d\n", iter);
  computeSummary();
  printStats();
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

  for(int m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    for(int i = 0; i < currmsg.paths.size(); i++) {
      if(currmsg.expand[i]) {
        myreal min = FLT_MAX;
        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          min = MIN(min, (currmsg.loads[i]/aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link])*aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link]);
        }
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.src].pciSO[currmsg.srcPCI])*aries[currmsg.src].pciSB[currmsg.srcPCI]);
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.dst].pciRO[currmsg.dstPCI])*aries[currmsg.dst].pciRB[currmsg.dstPCI]);

        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          aries[currmsg.paths[i][j].aries].linksB_t[currmsg.paths[i][j].link] += min;
        }
        aries[currmsg.src].pciSB_t[currmsg.srcPCI] += min;
        aries[currmsg.dst].pciRB_t[currmsg.dstPCI] += min;
        currmsg.bw +=  min;
        currmsg.allocated[i] += min;
      }
    }
  }
}

inline void computeSummary() {
  for(int m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    aries[currmsg.src].pciSO[currmsg.srcPCI] += currmsg.bytes;
    aries[currmsg.dst].pciRO[currmsg.dstPCI] += currmsg.bytes;
    for(int i = 0; i < currmsg.paths.size(); i++) {
      myreal pathLoad = currmsg.bytes*(currmsg.allocated[i]/currmsg.bw);
      for(int j = 0; j < currmsg.paths[i].size(); j++) {
        aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link] += pathLoad;
      }
    }
  }
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
  if(myRank) {
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    exit(0);
  }
}

inline void addPathsToMsgs() {
  for(int m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];

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
    for(int m = 0; m < msgsV.size(); m++) {
      Msg &currmsg = msgsV[m];
      currmsg.expand.resize(PATHS_PER_ITER, true);
      currmsg.loads.resize(PATHS_PER_ITER, 1.0/PATHS_PER_ITER);
    }

    markExpansionRequests();
    updateMessageAndLinks();

    bool expand = true;

    int iter;
    for(iter = 0; iter < MAX_ITERS && expand; iter++) {
      expand = false;
      if(iter % 50 == 0)
      printf("Iter %d\n",iter);
      //reset needed to zero
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
  for(int m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    currmsg.paths.clear();
    currmsg.paths.resize(PATHS_PER_ITER);
    for(int i = 0; i < PATHS_PER_ITER; i++) {
      getRandomPath(src, currmsg.src, dst, currmsg.dst, currmsg.paths[i]);
    }
  }
}

inline void updateMessageAndLinks() {
  for(int m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    for(int i = 0; i < currmsg.paths.size(); i++) {
      if(currmsg.expand[i]) {
        myreal min = FLT_MAX;
        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          min = MIN(min, (currmsg.loads[i]/aries[currmsg.paths[i][j].aries].linksO[currmsg.paths[i][j].link])*aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link]);
        }
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.src].pciSO[currmsg.srcPCI])*aries[currmsg.src].pciSB[currmsg.srcPCI]);
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.dst].pciRO[currmsg.dstPCI])*aries[currmsg.dst].pciRB[currmsg.dstPCI]);

        for(int j = 0; j < currmsg.paths[i].size(); j++) {
          aries[currmsg.paths[i][j].aries].linksB_t[currmsg.paths[i][j].link] += min;
        }
        aries[currmsg.src].pciSB_t[currmsg.srcPCI] += min;
        aries[currmsg.dst].pciRB_t[currmsg.dstPCI] += min;

        if(round == 0) {
          currmsg.bw +=  min;
        } else {
          myreal pathLoad = currmsg.bytes*(min/currmsg.bw);
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
  for(int m = 0; m < msgsV.size(); m++) {
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
  for(int m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    if(aries[currmsg.src].pciSB[currmsg.srcPCI] < CUTOFF_BW || aries[currmsg.dst].pciRB[currmsg.dstPCI] < CUTOFF_BW) {
      for(int i = 0; i < currmsg.paths.size(); i++) {
        currmsg.expand[i] = false;
      }
      continue;
    }

    myreal sum = 0;

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
