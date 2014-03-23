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
#define STATIC_ROUTING 1
#endif

#ifndef DIRECT_ROUTING
#define DIRECT_ROUTING 1
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
#define BLACK_BW 15360
#define BLUE_BW 12288

#define PCI_BW 16384

#define PACKET_SIZE 16384

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

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
inline void getRandomPath(Coords src, int srcNum, Coords &dst, int dstNum, Path & p);
inline void addIntraPath(Coords & src, int srcNum, Coords &dst, int dstNum, Path &p);
inline void addInterPath(Coords & src, int srcNum, Coords &dst, int dstNum, Path &p);

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

#if STATIC_ROUTING
/* for static routing, the linksO and pci(S/R)O stores the volumne of traffic that passes
through them in MB; linksB and pci(S/R)B stores the total bandwidth in MB */
void model() {
  memset(aries, 0, numAries*sizeof(Aries));
  //initialize upper bounds
  for(int i = 0; i < numAries; i++) {
    aries[i].localRank = coords[i].coords[TIER2] * maxCoords.coords[TIER3] + coords[i].coords[TIER3];
    for(int j = 0; j < 32; j++) {
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

  float time, maxTimePCI = 0, maxTimeLink = 0, maxLoad = 0, minLoad = FLT_MAX, maxPCI = 0, minPCI = FLT_MAX;
  for(int i = 0; i < numAries; i++) {
    for(int j = 0; j < 4; j++) {
      time = aries[i].pciSO[j]/aries[i].pciSB[j];
      maxTimePCI = MAX(maxTimePCI, time);
      maxPCI = MAX(maxPCI, aries[i].pciSO[j]);
      minPCI = MIN(minPCI, aries[i].pciSO[j]);

      time = aries[i].pciRO[j]/aries[i].pciRB[j];
      maxTimePCI = MAX(maxTimePCI, time);
      maxPCI = MAX(maxPCI, aries[i].pciRO[j]);
      minPCI = MIN(minPCI, aries[i].pciRO[j]);
    }

    for(int j = 0; j < BLUE_END; j++) {
      time = aries[i].linksO[j]/aries[i].linksB[j];
      maxTimeLink = MAX(maxTimeLink, time);
      maxLoad = MAX(maxLoad, aries[i].linksO[j]);
      minLoad = MIN(minLoad, aries[i].linksO[j]);
    }
  }
  printf("******************Summary*****************\n");
  printf("STATIC_ROUTING %d DIRECT_ROUTING %d\n",STATIC_ROUTING,DIRECT_ROUTING);
  printf("maxLinkTime %.2f s\n",maxTimeLink);
  printf("maxPCITime %.2f s\n",maxTimePCI);
  printf("maxLoad %.2f MB -- minLoad %.2f MB\n",maxLoad,minLoad);
  printf("maxPCI %.2f MB -- minPCI %.2f MB\n",maxPCI,minPCI);
}

#if DIRECT_ROUTING

inline void addLoads() {
  addPathsToMsgs();
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
    if(currmsg.paths.size()) {
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
}

inline void addPathsToMsgs() {
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    //if same aries, continue
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]
      && src.coords[TIER3] == dst.coords[TIER3]) continue;

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

inline void addLoads() {
  srand(time(NULL));
  float MB = 1024*1024;
  float perPacket = PACKET_SIZE/MB;
  int count = 0;
  int printFreq = numMsgs/100;
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    count++;
    if(count % printFreq == 0) {
      printf("Modeling at msg num %d\n", count);
    }
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]
      && src.coords[TIER3] == dst.coords[TIER3]) continue;

    aries[currmsg.src].pciSO[currmsg.srcPCI] += currmsg.bytes;
    aries[currmsg.dst].pciRO[currmsg.dstPCI] += currmsg.bytes;
    int numPackets = currmsg.bytes/perPacket;
    numPackets = MAX(1, numPackets);

    for(int i = 0; i < numPackets; i++) {
      Path p;
      getRandomPath(src, currmsg.src, dst, currmsg.dst, p);
      for(int j = 0; j < p.size(); j++) {
        aries[p[j].aries].linksO[p[j].link] += perPacket;
      }
    }
  }
}

inline void getRandomPath(Coords src, int srcNum, Coords &dst, int dstNum, Path & p) {
  //same group
  if(src.coords[TIER1] == dst.coords[TIER1]) {
    int valiantNum = rand() % ariesPerGroup;
    Coords valiantNode;
    while(valiantNum == aries[srcNum].localRank) {
      valiantNum = rand() % ariesPerGroup;
    }
    valiantNode.coords[TIER1] = src.coords[TIER1];
    valiantNode.coords[TIER2] = valiantNum / maxCoords.coords[TIER3];
    valiantNode.coords[TIER3] = valiantNum % maxCoords.coords[TIER3];
    coordstoAriesRank(valiantNum, valiantNode);
    addIntraPath(src, srcNum, valiantNode, valiantNum, p);
    if(valiantNum != dstNum) {
      addIntraPath(valiantNode, valiantNum, dst, dstNum, p);
    }
  } else { //different groups
    int valiantNum = rand() % maxCoords.coords[TIER1];
    Coords valiantNode;
    bool noValiant = false;
    while(valiantNum == src.coords[TIER1]) {
      valiantNum = rand() % ariesPerGroup;
    }
    if(valiantNum == dst.coords[TIER1]) {
      noValiant = true;
      valiantNode = dst;
      valiantNum = dstNum;
    } else {
      valiantNode.coords[TIER1] = valiantNum;
      valiantNum = rand() % ariesPerGroup;
      valiantNode.coords[TIER2] = valiantNum / maxCoords.coords[TIER3];
      valiantNode.coords[TIER3] = valiantNum % maxCoords.coords[TIER3];
      coordstoAriesRank(valiantNum, valiantNode);
    }

    addInterPath(src, srcNum, valiantNode, valiantNum, p);

    if(!noValiant) {
      addInterPath(valiantNode, valiantNum, dst, dstNum, p);
    }
  }
}

inline void addIntraPath(Coords & src, int srcNum, Coords &dst, int dstNum, Path &p) {
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
    if(rand() % 2) { //first GREEN, then BLACK
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

inline void addInterPath(Coords & src, int srcNum, Coords &dst, int dstNum, Path &p) {
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
      addIntraPath(src, srcNum, interNode, interNum, p);
    }

    //BLUE link
    Hop h;
    h.aries = interNum;
    h.link = BLUE_START + dst.coords[TIER1]/ariesPerGroup;
    p.push_back(h);

    localConnection = src.coords[TIER1] % ariesPerGroup;
    if(localConnection != aries[dstNum].localRank) {
      interNode.coords[TIER1] = dst.coords[TIER1];
      interNode.coords[TIER2] = localConnection % maxCoords.coords[TIER3];
      interNode.coords[TIER3] = localConnection % maxCoords.coords[TIER3];
      coordstoAriesRank(interNum, interNode);
      addIntraPath(interNode, interNum, dst, dstNum, p);
    }
}

#endif // not using DIRECT_ROUTING
#endif // STATIC_ROUTING

#if !STATIC_ROUTING
void model() {
  memset(aries, 0, numAries*sizeof(Aries));
  //initialize upper bounds
  for(int i = 0; i < numAries; i++) {
    aries[i].localRank = coords[i].coords[TIER2] * maxCoords.coords[TIER3] + coords[i].coords[TIER3];
    for(int j = 0; j < 32; j++) {
      if(j < GREEN_END) aries[i].linksB[j] = GREEN_BW;
      else if(j < BLACK_END) aries[i].linksB[j] = BLACK_BW;
      else aries[i].linksB[j] = BLUE_BW;
    }
    for(int j = 0; j < 4; j++) {
      aries[i].pciSB[j] = PCI_BW;
      aries[i].pciRB[j] = PCI_BW;
    }
  }

  // perform static routing
  addPathsToMsgs();
}

#if DIRECT_ROUTING
inline int addPathsToMsgs() {
  for(list<Msg>::iterator msgit = msgs.begin(); msgit != msgs.end(); msgit++) {
    Msg &currmsg = *msgit;
    Coords &src = coords[currmsg.src], &dst = coords[currmsg.dst];
    //if same aries, return 0
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]
      && src.coords[TIER3] == dst.coords[TIER3]) return 0;

    //in the same lowest tier - direct connection
    if(src.coords[TIER1] == dst.coords[TIER1] && src.coords[TIER2] == dst.coords[TIER2]) {
      currmsg.paths.resize(1);
      currmsg.paths[0].resize(1);
      currmsg.paths[0][0].aries = currmsg.src;
      currmsg.paths[0][0].link = dst.coords[TIER3]; //GREEN
      return 1;
    } else if(src.coords[TIER1] == dst.coords[TIER1]) {
      //in the same second tier
      if(src.coords[TIER3] == dst.coords[TIER3]) {
        //aligned on tier3 - direct connection
        currmsg.paths.resize(1);
        currmsg.paths[0].resize(1);
        currmsg.paths[0][0].aries = currmsg.src;
        currmsg.paths[0][0].link = BLACK_START + dst.coords[TIER2];
        return 1;
      } // else two paths of length 2
      currmsg.paths.resize(2);
      addToPath(currmsg.paths, src, currmsg.src, dst, 0, 1);
      return 1;
    } else {
      addFullPaths(currmsg.paths, src, currmsg.src, dst, currmsg.dst);
      return 1;
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
    //if use only 1 GREEN LINK
    if(interNode.coords[TIER2] == src.coords[TIER2]) {
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
    //if use only 1 GREEN LINK
    if(interNode.coords[TIER2] == dst.coords[TIER2]) {
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
#endif // DYNAMIC ROUTING
#endif // !STATIC ROUTING
