#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cassert>
#include <list>
#include <vector>
#include <map>
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
#define BLACK_END 32
#define BLUE_START BLACK_END
#define BLUE_END 42

#define GREEN 0
#define BLACK 1
#define BLUE 2

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

struct Link {
  int offset, type;
};
struct bLink {
  int offset, dest;
};

vector< map< int, vector<Link> > > intraGroupLinks;
vector< map< int, vector<bLink> > > interGroupLinks;
vector< vector< vector<int> > > connectionList;

inline void mysrand(unsigned seed) {
  srand(seed);
}

inline unsigned myrand () {
   return rand();
}

inline unsigned myrand_r (unsigned *seed) {
   return rand_r(seed);
}

// coordinates
typedef struct Coords {
  int coords[NUM_COORDS];
} Coords;

typedef struct Aries {
  myreal linksO[BLUE_END]; // 0-15 Green, 16-21 Black
  myreal linksB[BLUE_END], linksB_t[BLUE_END]; //remaining link bandwidth
  myreal pciSO[4], pciRO[4]; //send and recv PCI
  myreal pciSB[4], pciRB[4], pciSB_t[4], pciRB_t[4]; //remaining PCI bandwidth
  int localRank; //rank within the group
  myreal linksSum[BLUE_END]; //needed to continously get traffic
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

struct IntraGroupLink {
  int src, dest, type;
};

struct InterGroupLink {
  int src, dest;
};

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
double *linkLoads, *pciLoads;
int round;

//forward declaration
void model();
inline void addLoads();
inline void addPathsToMsgs();
inline void getDirectPath(Coords src, int srcNum, Coords &dst, int dstNum, 
    Path & p, unsigned *seed = 0);
inline void getRandomPath(Coords src, int srcNum, Coords &dst, int dstNum, 
    Path & p, unsigned *seed = 0);
inline void addIntraPath(Coords & src, int srcNum, Coords &dst, int dstNum, 
    Path &p, unsigned *seed = 0);
inline void addInterPath(Coords & src, int srcNum, Coords &dst, int dstNum, 
    Path &p, unsigned *seed = 0);
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

inline void calculateAndPrint(struct timeval & start, struct timeval & end, 
    string out) {
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
    printf("Usage: %s <conffile> <mapfile> <outfile>\n", argv[0]);
    exit(1);
  }

  if(!myRank) {
    printf("Processing command line and conffile\n");
  }

  FILE *conffile = fopen(argv[1],"r");
  nulltest((void*)conffile, "configuration file");

  FILE *mapfile = fopen(argv[2],"r");

  if(!myRank) {
    outputFile = fopen(argv[3], "w");
    nulltest((void*)outputFile, "output file");
  }

  fscanf(conffile, "%d", &numAries);
  positivetest((double)numAries, "number of Aries routers");

  for(int i = 0; i < NUM_COORDS; i++) {
    fscanf(conffile, "%d", &maxCoords.coords[i]);
    positivetest((double)maxCoords.coords[i], "a dimension");
  }
  ariesPerGroup = maxCoords.coords[TIER2] * maxCoords.coords[TIER3];

  assert(numAries == (ariesPerGroup*maxCoords.coords[TIER1]));

  numPEs = numAries * maxCoords.coords[NUM_LEVELS] * 
           maxCoords.coords[NUM_LEVELS + 1];

  aries = new Aries[numAries];
  nulltest((void*)aries, "aries status array");

  coords = new Coords[numPEs];
  nulltest((void*)coords, "coordinates of PEs");

  /* Read the mapping of MPI ranks to hardware nodes */
  if(mapfile == NULL) {
    if(!myRank)
      printf("Mapfile does not exist; using default mapping\n");
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
  if(mapfile != NULL)
    fclose(mapfile);

  // read intra group connections, store from a router's perspective
  // all links to the same router form a vector
  char intraFile[256] = {0};
  fscanf(conffile, "%s", intraFile);
  FILE *groupFile = fopen(intraFile, "rb");
  if(!myRank)
    printf("Reading intraGroup file %s\n", intraFile);
  
  {
    vector< int > greenOffsets, blackOffsets;
    greenOffsets.resize(ariesPerGroup, 0);
    blackOffsets.resize(ariesPerGroup, 0);
    intraGroupLinks.resize(ariesPerGroup);
    IntraGroupLink newLink;

    while(fread(&newLink, sizeof(IntraGroupLink), 1, groupFile) != 0) {
      Link tmpLink;
      tmpLink.type = newLink.type;
      if(tmpLink.type == GREEN) {
        tmpLink.offset = greenOffsets[newLink.src]++;
      } else {
        tmpLink.offset = BLACK_START + blackOffsets[newLink.src]++;
      }
      intraGroupLinks[newLink.src][newLink.dest].push_back(tmpLink);
    }
  }

  fclose(groupFile);

  // read inter group connections, store from a router's perspective
  // also create a group level table that tells all the connecting routers
  char interFile[256] = {0};
  fscanf(conffile, "%s", interFile);
  FILE *systemFile = fopen(interFile, "rb");
  if(!myRank)
    printf("Reading interGroup file %s\n", interFile);
  
  {
    vector< int > blueOffsets;
    blueOffsets.resize(numAries, 0);
    interGroupLinks.resize(numAries);
    connectionList.resize(maxCoords.coords[0]);
    for(int g = 0; g < connectionList.size(); g++) {
      connectionList[g].resize(maxCoords.coords[0]);
    }
    InterGroupLink newLink;

    while(fread(&newLink, sizeof(InterGroupLink), 1, systemFile) != 0) {
      bLink tmpLink;
      tmpLink.dest = newLink.dest;
      int srcG = newLink.src / ariesPerGroup;
      int destG = newLink.dest / ariesPerGroup;
      tmpLink.offset = BLUE_START + blueOffsets[newLink.src]++;
      interGroupLinks[newLink.src][destG].push_back(tmpLink);
      int r;
      for(r = 0; r < connectionList[srcG][destG].size(); r++) {
        if(connectionList[srcG][destG][r] == newLink.src) break;
      }
      if(r == connectionList[srcG][destG].size()) {
        connectionList[srcG][destG].push_back(newLink.src);
      }
    }
  }

  fclose(systemFile);

#if DUMP_CONNECTIONS
  printf("Dumping intra-group connections\n");
  for(int a = 0; a < intraGroupLinks.size(); a++) {
    printf("Connections for router %d\n", a);
    map< int, vector<Link> >  &curMap = intraGroupLinks[a];
    map< int, vector<Link> >::iterator it = curMap.begin();
    for(; it != curMap.end(); it++) {
      printf(" ( %d - ", it->first);
      for(int l = 0; l < it->second.size(); l++) {
        printf("%d,%d ", it->second[l].offset, it->second[l].type);
      }
      printf(")");
    }
    printf("\n");
  }
#endif
#if DUMP_CONNECTIONS
  printf("Dumping inter-group connections\n");
  for(int a = 0; a < interGroupLinks.size(); a++) {
    printf("Connections for router %d\n", a);
    map< int, vector<Link> >  &curMap = interGroupLinks[a];
    map< int, vector<Link> >::iterator it = curMap.begin();
    for(; it != curMap.end(); it++) {
      printf(" ( %d - ", it->first);
      for(int l = 0; l < it->second.size(); l++) {
        printf("%d,%d ", it->second[l].offset, it->second[l].dest);
      }
      printf(")");
    }
    printf("\n");
  }
#endif

#if DUMP_CONNECTIONS
  printf("Dumping source aries for global connections\n");
  for(int g = 0; g < maxCoords.coords[0]; g++) {
    for(int g1 = 0; g1 < maxCoords.coords[0]; g1++) {
      printf(" ( ");
      for(int l = 0; l < connectionList[g][g1].size(); l++) {
        printf("%d ", connectionList[g][g1][l]); 
      }
      printf(")");
    }
    printf("\n");
  }
#endif
  
  double sum = 0;
  myreal MB = 1024 * 1024;
  int currRankBase = 0;

  /* Read the communication graph which is in edge-list format */
  if(!myRank)
    printf("Reading messages\n");

  struct timeval startRead, endRead;
  gettimeofday(&startRead, NULL);
    
  while(!feof(conffile)) {
    char cur_comm_file[256] = {0};
    int cur_ranks;

    fscanf(conffile, "%s", cur_comm_file);
    if(strcmp(cur_comm_file, "") != 0) {
      FILE *commfile = fopen(cur_comm_file, "rb");
      nulltest((void*)commfile, "communication file");

      fscanf(conffile, "%d", &cur_ranks);
      positivetest((double)cur_ranks, "number of ranks");
      fscanf(conffile, "%llu", &numMsgs);
      positivetest((double)numMsgs, "number of messages");

      unsigned long long begin = (myRank*numMsgs)/numRanks;
      unsigned long long end = ((myRank+1)*numMsgs)/numRanks;
      unsigned long long currentCount = begin;
      fseek(commfile, begin*16, SEEK_SET);
      MsgSDB newMsgSDB;

      if(!myRank)
        printf("Reading  %s %d %llu\n", cur_comm_file, numRanks, numMsgs);

      while(!feof(commfile)) {
        Msg newmsg;
        if(fread(&newMsgSDB, sizeof(MsgSDB), 1, commfile) != 0) {
          newmsg.src = newMsgSDB.src + currRankBase;
          newmsg.dst = newMsgSDB.dst + currRankBase;
          newmsg.bytes = newMsgSDB.bytes;
          if(currentCount >= end) break;
          Coords& src = coords[newmsg.src];
          Coords& dst = coords[newmsg.dst];
          if(src.coords[TIER1] == dst.coords[TIER1] 
              && src.coords[TIER2] == dst.coords[TIER2]
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
      }
      currRankBase = cur_ranks;
      fclose(commfile);
    }
  }
  fclose(conffile);

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
    printf("numlevels: %d, dims: %d %d %d %d %d\n", NUM_LEVELS, 
           maxCoords.coords[0], maxCoords.coords[1], maxCoords.coords[2], 
           maxCoords.coords[3], maxCoords.coords[4]);
    printf("numAries: %d, numPEs: %d, numMsgs: %llu, total volume: %.0lf MB\n", 
          numAries, numPEs, numMsgs, sum);

    printf("Starting modeling \n");
  }

  struct timeval startModel, endModel;
  gettimeofday(&startModel, NULL);
  model();
  gettimeofday(&endModel, NULL);
  if(!myRank) {
    calculateAndPrint(startModel, endModel, "time to model");
  }

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

  long long totalLinks = numAries*(15+5+((numAries/ariesPerGroup) + 
                         (numAries % ariesPerGroup) ? 1 : 0));

  printf("******************Summary*****************\n");
  printf("maxLoad %.2f MB -- minLoad %.2f MB\n",maxLoad,minLoad);
  printf("averageLinkLoad %.2lf MB \n", totalLinkLoad/totalLinks);
}

inline void getDirectPath(Coords src, int srcNum, Coords &dst, int dstNum, 
    Path & p, unsigned *seed) {
  //same group
  if(src.coords[TIER1] == dst.coords[TIER1]) {
    addIntraPath(src, srcNum, dst, dstNum, p, seed);
  } else { //different groups
    addInterPath(src, srcNum, dst, dstNum, p, seed);
  }
}

inline void getRandomPath(Coords src, int srcNum, Coords &dst, int dstNum, 
    Path & p, unsigned *seed) {
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

inline void addIntraPath(Coords & src, int srcNum, Coords &dst, int dstNum, 
    Path &p, unsigned *seed) {
  Hop h;
  int usedInter = 0;
  if(src.coords[TIER2] == dst.coords[TIER2] || 
     src.coords[TIER3] == dst.coords[TIER3]) { //if use just 1 link
    h.aries = srcNum;
    vector< Link > & intraLinks =
      intraGroupLinks[aries[srcNum].localRank][aries[dstNum].localRank];
    if(intraLinks.size() > 1) {
      usedInter = myrand() % intraLinks.size();   
    } 
    h.link = intraLinks[usedInter].offset;
    p.push_back(h);
  } else { //two paths, choose 1
    Coords gNeighbor, bNeighbor;
    int gRank, bRank;
    gNeighbor = bNeighbor = src;
    gNeighbor.coords[TIER3] = dst.coords[TIER3];
    bNeighbor.coords[TIER2] = dst.coords[TIER2];
    coordstoAriesRank(gRank, gNeighbor);
    coordstoAriesRank(bRank, bNeighbor);
    vector< Link > & gLinks =
      intraGroupLinks[aries[srcNum].localRank][aries[gRank].localRank];
    vector< Link > & bLinks =
      intraGroupLinks[aries[srcNum].localRank][aries[bRank].localRank];
    int totalPaths = gLinks.size() + bLinks.size();

    if((myrand() % totalPaths) < gLinks.size()) { //first GREEN, then BLACK
      h.aries = srcNum;
      if(gLinks.size() > 1) {
        usedInter = myrand() % gLinks.size();   
      } 
      h.link = gLinks[usedInter].offset;
      p.push_back(h);
      h.aries = gRank;
      vector< Link > & gbLinks =
        intraGroupLinks[aries[gRank].localRank][aries[dstNum].localRank];
      if(gbLinks.size() > 1) {
        usedInter = myrand() % gbLinks.size();   
      } else {
        usedInter = 0;
      }
      h.link = gbLinks[usedInter].offset;
      p.push_back(h);
    } else { //first BLACK, then GREEN
      h.aries = srcNum;
      if(bLinks.size() > 1) {
        usedInter = myrand() % bLinks.size();   
      } 
      h.link = bLinks[usedInter].offset;
      p.push_back(h);
      h.aries = bRank;
      vector< Link > & bgLinks =
        intraGroupLinks[aries[bRank].localRank][aries[dstNum].localRank];
      if(bgLinks.size() > 1) {
        usedInter = myrand() % bgLinks.size();   
      } else {
        usedInter = 0;
      }
      h.link = bgLinks[usedInter].offset;
      p.push_back(h);
    }
  }
}

inline void addInterPath(Coords & src, int srcNum, Coords &dst, int dstNum, 
    Path &p, unsigned * seed) {
    Coords interNode;
    int usedInter = 0, interNum;
    int dstG = dst.coords[TIER1];

    vector< int > & intNodes =
      connectionList[src.coords[TIER1]][dstG];
    if(intNodes.size() > 1) {
      usedInter = myrand() % intNodes.size();   
    }
    usedInter = intNodes[usedInter];
    if(usedInter == srcNum) {
      interNode = src;
      interNum = srcNum;
    } else {
      assert(src.coords[TIER1] == coords[usedInter].coords[TIER1]);
      interNode.coords[TIER1] = coords[usedInter].coords[TIER1];
      interNode.coords[TIER2] = coords[usedInter].coords[TIER2];
      interNode.coords[TIER3] = coords[usedInter].coords[TIER3];
      coordstoAriesRank(interNum, interNode);
      assert(interNum == usedInter);
      addIntraPath(src, srcNum, interNode, interNum, p, seed);
    }

    //BLUE link
    Hop h;
    h.aries = interNum;
    vector< bLink > & interConnections = interGroupLinks[interNum][dstG]; 
    usedInter = myrand() % interConnections.size();   
    h.link = interConnections[usedInter].offset;
    usedInter = interConnections[usedInter].dest;
    p.push_back(h);

    if(usedInter != dstNum) {
      assert(coords[usedInter].coords[TIER1] == dst.coords[TIER1]);
      interNode.coords[TIER1] = coords[usedInter].coords[TIER1];
      interNode.coords[TIER2] = coords[usedInter].coords[TIER2];
      interNode.coords[TIER3] = coords[usedInter].coords[TIER3]; 
      coordstoAriesRank(interNum, interNode);
      assert(usedInter == interNum);
      addIntraPath(interNode, interNum, dst, dstNum, p, seed);
    }
}

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
      aries[i].localRank = coords[i].coords[TIER2] * maxCoords.coords[TIER3] + 
                           coords[i].coords[TIER3];
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

      MPI_Allreduce(MPI_IN_PLACE, linkLoads, numAries*BLUE_END, MPI_DOUBLE, 
                    MPI_SUM, MPI_COMM_WORLD);
      MPI_Allreduce(MPI_IN_PLACE, pciLoads, numAries*8, MPI_DOUBLE, MPI_SUM, 
                    MPI_COMM_WORLD);

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

  MPI_Allreduce(MPI_IN_PLACE, linkLoads, numAries*BLUE_END, MPI_DOUBLE, MPI_SUM, 
                MPI_COMM_WORLD);

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
  int indirect = 2;
  for(size_t m = 0; m < msgsV.size(); m++) {
    Msg &currmsg = msgsV[m];
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    currmsg.paths.clear();
    currmsg.paths.resize(PATHS_PER_ITER);
    // add 2 direct paths
    for(int i = 0; i < 2; i++) {
      getDirectPath(src, currmsg.src, dst, currmsg.dst, currmsg.paths[i]);
    };
#if NO_PATH_REPITITION
    int compare = 1;
    if(currmsg.paths[0].size() != currmsg.paths[1].size()) compare = 0;
    if(compare) {
      for(int i = 0; i < currmsg.paths[0].size(); i++) {
        if((currmsg.paths[0][i].aries != currmsg.paths[1][i].aries) || 
           (currmsg.paths[0][i].link != currmsg.paths[1][i].link)) {
          compare = 0;
          break;
        }
      }
      if(compare) {
        currmsg.paths.resize(1);
        currmsg.paths.resize(PATHS_PER_ITER);
        indirect = 1;
      }
    }
#endif
    //add rest from indirect paths
    for(int i = indirect; i < PATHS_PER_ITER; i++) {
      getRandomPath(src, currmsg.src, dst, currmsg.dst, currmsg.paths[i]);
    }
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

  MPI_Allreduce(MPI_IN_PLACE, linkLoads, numAries*BLUE_END, MPI_DOUBLE, MPI_SUM, 
                MPI_COMM_WORLD);
  MPI_Allreduce(MPI_IN_PLACE, pciLoads, numAries*8, MPI_DOUBLE, MPI_SUM, 
                MPI_COMM_WORLD);

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
        min = MIN(min, (currmsg.loads[i]/aries[currmsg.src].pciSO[currmsg.srcPCI])
                       *aries[currmsg.src].pciSB[currmsg.srcPCI]);
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
    if(aries[currmsg.src].pciSB[currmsg.srcPCI] < CUTOFF_BW || 
       aries[currmsg.dst].pciRB[currmsg.dstPCI] < CUTOFF_BW) {
      for(int i = 0; i < currmsg.paths.size(); i++) {
        currmsg.expand[i] = false;
      }
      continue;
    }

    myreal sum = 0;

    for(size_t i = 0; i < currmsg.paths.size(); i++) {
      currmsg.loads[i] = FLT_MAX;
      for(int j = 0; j < currmsg.paths[i].size(); j++) {
        currmsg.loads[i] = MIN(currmsg.loads[i], 
        aries[currmsg.paths[i][j].aries].linksB[currmsg.paths[i][j].link]);
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
