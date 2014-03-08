/* A delta based simulator for deterministic routed networks.
   Key idea is to raise the abstraction of simulation even further by not
   considering packet level routing; rather this simulator tries to predict the
   network flow every delta time. I expect this to work well for deterministic
   routing with large messages. May augment in future to handle small messages.

   Author - Nikhil Jain
   Contact - nikhil.jain@acm.org

 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cassert>
#include <list>
#include <vector>
#include <sys/time.h>

using namespace std;
#define EXTRA_INFO 1
#define abs_x(x) ((x^(x>>31)) - (x>>31))

// coordinates
typedef struct Coords {
  int coords[6];
} Coords;

// status of the links
typedef struct PE {
  bool fifos[10];
} PE;

typedef struct Nodes {
  bool links[10];
  bool isblocked[10];
  bool torusfifo[10];
  float memory;
} Nodes;

// an edge in a message path
typedef struct Path {
  int node, link;
} Path;

// a message
typedef struct Msg {
  int src, dst, bytes, torusfifo, srcPE;
  vector< vector< vector<Path> > > paths;
} Msg;

long long int delta; // in nano sec
long long int newdelta = 0; // in nano sec
double memoryLimit = 26.00;
double peakbw;
double bw; // in GB/s
int numdims; // number of dimensions
int *dims; // dimensions
int *order; // routing order
int *dimloc; // location of dim in routing order
int numranks; // number of ranks
int numnodes; // number of nodes in the system
Coords *coords; // rank to coordinates
int nummessages; // number of messages to be sent
list<Msg> msgs; // messages left to be routed
list<Msg> dirmsgs[5]; // messages direction wise left to be routed
long long curtime; // current time of simulation in nano seconds
Nodes *nodes; // link status of current nodes
PE *pe;

//forward declaration
void simulate();

//we use a simple dimensional ordered ranking for nodes - nothing to do with the
//actual ranking; used only for data management
inline void coordstonoderank(int &noderank, const Coords &ncoord) {
  noderank = 0;
  int prod = 1;
  for(int i = numdims - 1; i >= 0; i--) {
    noderank += ncoord.coords[i]*prod;
    prod *= dims[i];
  }
}

inline void noderanktocoords(int noderank, Coords &ncoord) {
  for(int i = numdims - 1; i >= 0; i--) {
    ncoord.coords[i] = noderank % dims[i];
    noderank /= dims[i];
  }
}

//some test to detect errors quickly
inline void nulltest(void *test, char* testfor) {
  if(test == NULL) {
    printf("Test failed: %s\n",testfor);
    exit(1);
  }
}

inline void positivetest(double val, char *valfor) {
  if(val <= 0) {
    printf("Non-positive value for %s\n", valfor);
    exit(1);
  }
}

inline void calculateAndPrint(struct timeval & start, struct timeval & end, char *out) {
  double time = 0;
  time = (end.tv_sec - start.tv_sec) * 1000;
  time += ((end.tv_usec - start.tv_usec)/1000.0);

  printf("%s : %.3lf ms\n", out, time);
}

//get ranks of nodes and direction to travel in one dimension
//0 for plus, 1 for minus
inline void linkset(int src, int dst, int dimlen, int sumsrc, int sumdst, vector< vector<int> >& use, int &dir, int &fifodir) {
  dir = -1;

  use.resize(2);
  if(src == dst) return;

  if(dst > src) {
    if(((dst - src) < (dimlen/2)) || (((dst - src) == (dimlen/2)) && ((sumsrc % 2) == 0))) {
      dir = 0;
      for(int i = src; i < dst; i++) {
        use[0].push_back(i);
      }
    }
    if(((dst - src) > (dimlen/2)) || (((dst - src) == (dimlen/2)) && ((sumsrc % 2) == 1))) {
      dir = 1;
      for(int i = src; i >= 0; i--) {
        use[0].push_back(i);
      }
      for(int i = dimlen - 1; i > dst; i--) {
        use[0].push_back(i);
      }
    }
  } else {
    if(((src - dst) < (dimlen/2)) || (((src - dst) == (dimlen/2)) && ((sumsrc % 2) == 1))) {
      dir = 1;
      for(int i = src; i > dst; i--) {
        use[0].push_back(i);
      }
    }
    if(((src - dst) > (dimlen/2)) || (((src - dst) == (dimlen/2)) && ((sumsrc % 2) == 0))) {
      dir = 0;
      for(int i = src; i < dimlen; i++) {
        use[0].push_back(i);
      }
      for(int i = 0; i < dst; i++) {
        use[0].push_back(i);
      }
    }
  }

  if(dst > src) {
    if(((dst - src) < (dimlen/2)) || (((dst - src) == (dimlen/2)) && ((sumdst % 2) == 0))) {
      fifodir = 1;
    }
    if(((dst - src) > (dimlen/2)) || (((dst - src) == (dimlen/2)) && ((sumdst % 2) == 1))) {
      fifodir = 0;
    }
  } else {
    if(((src - dst) < (dimlen/2)) || (((src - dst) == (dimlen/2)) && ((sumdst % 2) == 1))) {
      fifodir = 0;
    }
    if(((src - dst) > (dimlen/2)) || (((src - dst) == (dimlen/2)) && ((sumdst % 2) == 0))) {
      fifodir = 1;
    }
  }
}

inline void linksete(int src, int dst, int dimlen, vector< vector<int> >& use, int &dir, int &fifodir) {
  int usearray;
  dir = -1;
  use.resize(2);
  if(src == dst) return;

  use[0].push_back(src);
  //use[1].push_back(src);
  dir = 0;
  fifodir = 1;
}


//compute paths each message will take
void compute_paths() {
  bool others;
  int count = 0;
#if EXTRA_INFO
  long long totalhops = 0;
  int ** nodest = new int*[numnodes];
  int * msghops = new int[nummessages];
  int msgcnt = 0;

  for(int i = 0; i < numnodes; i++) {
    nodest[i] = new int[10];
    for(int j = 0; j < 10; j++) {
      nodest[i][j] = 0;
    }
  }
#endif

  for(list<Msg>::iterator it = msgs.begin(); it != msgs.end();) {
    Msg &curmsg = *it;
    int firstDir = -1;
    int hasE = 0;
    curmsg.paths.resize(5);

#if EXTRA_INFO
    int curhops = 0;
#endif

    Coords srcnode, dstnode;
    noderanktocoords(curmsg.src, srcnode);
    noderanktocoords(curmsg.dst, dstnode);

    int torusfifo, d[5], sumsrc, sumdst, sumd;
    sumdst = sumsrc = sumd = 0;
    for(int i = 0; i < numdims; i++) {
      d[i] = abs(dstnode.coords[i] - srcnode.coords[i]);
      sumdst += dstnode.coords[i];
      sumsrc += srcnode.coords[i];
      sumd += d[i];
    }

    curmsg.torusfifo = sumd % 10;

    for(int i = 0; i < numdims; i++) {
      vector< vector<int> > use;
      int fifodir, dir;

      if(order[i] != 4) {
        linkset(srcnode.coords[order[i]], dstnode.coords[order[i]], dims[order[i]], sumsrc, sumdst, use, dir, fifodir);
      } else {
        linksete(srcnode.coords[order[i]], dstnode.coords[order[i]], dims[order[i]], use, dir, fifodir);
      }
      if((use[0].size() > dims[order[i]]/2) || (use[1].size() > dims[order[i]]/2) ) {
        printf("Weird routing: hops: %zd %zd, expected max %d\n", use[0].size(), use[1].size(), dims[order[i]/2]);
        exit(1);
      }

      curmsg.paths[order[i]].resize(2);
      if(!use[0].size()) continue;

#if EXTRA_INFO
      curhops += use[0].size();
#endif

      if((sumd - d[order[i]]) == 0) {
        curmsg.torusfifo = 2*order[i] + fifodir;
      }

      if(firstDir == -1) firstDir = order[i];
      if(order[i] == 4) hasE = 1;

      for(int k = 0; k < 2; k++) {
        Coords startnode = srcnode;
        int localdir = (k == 0) ? dir : (1 - dir);
        for(int j = 0; j < use[k].size(); j++) {
          Path p;
          startnode.coords[order[i]] = use[k][j];
          coordstonoderank(p.node, startnode);
          p.link = 2*order[i] + localdir;
          curmsg.paths[order[i]][k].push_back(p);
        }
      }
      srcnode.coords[order[i]] = dstnode.coords[order[i]];
    }

#if EXTRA_INFO
    if(firstDir != -1) {
      totalhops += curhops;
      msghops[msgcnt++] = curhops;
    }
#endif

    if(firstDir != -1 && firstDir != 4) {
      Msg copymsg = *it;
      it = msgs.erase(it);
      if(hasE) {
        copymsg.bytes = copymsg.bytes/2;
        dirmsgs[firstDir].push_back(copymsg);
        copymsg.paths[4][0][0].link = 8 + (1 - (copymsg.paths[4][0][0].link - 8));
        dirmsgs[firstDir].push_back(copymsg);
      } else {
        dirmsgs[firstDir].push_back(copymsg);
      }
#if EXTRA_INFO
      nodest[copymsg.src][copymsg.torusfifo]++;
#endif
      if((newdelta > (copymsg.bytes/bw + 1)) || (newdelta == 0)) {
        newdelta = copymsg.bytes/bw + 1;
      }
    } else if(firstDir == 4) {
      Msg copymsg = *it;
      it = msgs.erase(it);
      copymsg.bytes = copymsg.bytes/2;
      dirmsgs[firstDir].push_back(copymsg);
#if EXTRA_INFO
      nodest[copymsg.src][copymsg.torusfifo]++;
#endif
      copymsg.torusfifo = 8 + (1 - (copymsg.torusfifo - 8));
      copymsg.paths[firstDir][0][0].link = 8 + (1 - (copymsg.paths[firstDir][0][0].link - 8));
      dirmsgs[firstDir].push_back(copymsg);
      newdelta = copymsg.bytes/bw + 1;
    } else it++;
  }

#if EXTRA_INFO
  float avghops =  (float)totalhops/msgcnt;
  int maxhops = 0, aahops = 0, aamsg = 0;
  int maxnodest = 0;
  for(int i = 0; i < msgcnt; i++) {
    if(msghops[i] > maxhops) {
      maxhops = msghops[i];
    }
    if(msghops[i] >= avghops) {
      aamsg++;
      aahops += msghops[i];
    }
  }

  for(int i = 0; i < numnodes; i++) {
    for(int j = 0; j < 10; j++) {
      if(maxnodest < nodest[i][j])
        maxnodest = nodest[i][j];
      //printf("%d ",nodest[i][j]);
    }
    //printf("\n");
  }
  for(int i = 0; i < numnodes; i++) {
    delete [] nodest[i];
  }

  printf("Total_hops: %lld : Max_hops: %d : Avg_AA_hops: %f : Total_AA_hops: %d : MaxNodesT: %d \n", totalhops, maxhops, (float)aahops/(float)aamsg, aahops, maxnodest);

  delete [] nodest;
  delete [] msghops;
#endif
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
  nulltest((void*)mapfile, "mapping file");

  FILE *commfile = fopen(argv[3], "r");
  nulltest((void*)commfile, "communication file");

  fscanf(conffile, "%lld", &delta);
  positivetest((double)delta, "delta");

  fscanf(conffile, "%lf", &peakbw);
  positivetest((double)peakbw, "bandwidth");
  bw = .90 * peakbw;

  fscanf(conffile, "%d", &numdims);
  positivetest((double)numdims, "number of dimensions");

  dims = new int[numdims];
  nulltest((void*)dims, "dimension array");

  numnodes = 1;
  for(int i = 0; i < numdims; i++) {
    fscanf(conffile, "%d", &dims[i]);
    positivetest((double)dims[i], "a dimension");
    numnodes *= dims[i];
  }

  nodes = (Nodes*)malloc(numnodes * sizeof(Nodes));
  nulltest((void*)nodes, "node link status array");

  pe = (PE*)malloc(numnodes * 16 * sizeof(PE));
  nulltest((void*)pe, "pe status array");

  order = new int[numdims];
  nulltest((void*)order, "routing order array");

  for(int i = 0; i < numdims; i++) {
    fscanf(conffile, "%d", &order[i]);
  }

  dimloc = new int[numdims];
  nulltest((void*)dimloc, "location of dimension in routing order array");

  for(int i = 0; i < numdims; i++) {
    dimloc[order[i]] = i;
  }

  fscanf(conffile, "%d", &numranks);
  positivetest((double)numranks, "number of ranks");

  coords = new Coords[numranks];
  nulltest((void*)coords, "coordinates of ranks array");

  /* Read the mapping of MPI ranks to hardware nodes */
  printf("Reading mapfile\n");
  for(int i = 0; i < numranks; i++) {
    for(int j = 0; j <= numdims; j++) {
      fscanf(mapfile, "%d", &coords[i].coords[j]);
    }
  }

  nummessages = 0;
  double sum = 0;

  /* Read the communication graph which is in edge-list format */
  printf("Reading messages\n");
  struct timeval startRead, endRead;
  gettimeofday(&startRead, NULL);
  while(!feof(commfile)) {
    Msg newmsg;
    fscanf(commfile, "%d %d %d\n", &newmsg.src, &newmsg.dst, &newmsg.bytes);
    newmsg.srcPE = newmsg.src;
    /* change comm graph to be hardware node id based instead of MPI rank
     * based */
    coordstonoderank(newmsg.src, coords[newmsg.src]);
    coordstonoderank(newmsg.dst, coords[newmsg.dst]);
    msgs.push_back(newmsg);
    sum += newmsg.bytes;
    nummessages++;
  }
  gettimeofday(&endRead, NULL);
  calculateAndPrint(startRead, endRead, "time to read communication pattern");

  printf("Simulation for following system will be performed:\n");
  printf("numdims: %d, dims: %d %d %d %d %d\n", numdims, dims[0], dims[1], dims[2], dims[3], dims[4]);
  printf("numnodes: %d, numranks: %d, nummessages: %d, total volume: %.0lf\n", numnodes, numranks, nummessages, sum);

  printf("Computing paths\n");
  struct timeval startPath, endPath;
  gettimeofday(&startPath, NULL);

  /* We are using static dimension-ordered routing
  and we calculate the paths for each pair of hardware nodes beforehand

  Also divides the 'msgs' list into 5 lists based on which dimension is the
  message routed on first.

  And currently message going over ABCDE gets preference over message going over
  BCDE */
  compute_paths();
  gettimeofday(&endPath, NULL);
  calculateAndPrint(startPath, endPath, "time to compute paths for messages");

  printf("Routing order %d %d %d %d %d\n",order[0], order[1], order[2], order[3], order[4]);
  for(int i = 0; i < numdims; i++) {
    printf("#msgs starting in %d dimension is %zd\n", i, dirmsgs[i].size());
  }
  printf("#msgs local to node is %zd\n", msgs.size());

  printf("Starting simulation \n");
  curtime = 0;
  struct timeval startSim, endSim;
  gettimeofday(&startSim, NULL);

  /* Simulation of the communication pattern */
  simulate();
  gettimeofday(&endSim, NULL);
  calculateAndPrint(startSim, endSim, "time to simulate");

  printf("Simulated time spent in message transfer is %.3lf ms\n", curtime/(1000.0*1000));

  fclose(conffile);
  fclose(mapfile);
  fclose(commfile);
  delete [] dims;
  free(nodes);
  delete [] order;
  delete [] dimloc;
  delete [] coords;

}

void simulate() {
  bool canSend, prevSend, blocked;
  size_t msgsleft = 0;

  // delete local messages
  {
    list<Msg>().swap(msgs);
  }

  /* total number of messages to be processed */
  for(int i = 0; i < numdims; i++) {
    msgsleft += dirmsgs[i].size();
  }

  delta = newdelta;
  long long int mod = 50*delta;
  printf("Using delta as %lld\n",delta);
  while( msgsleft != 0) {
    if(curtime % mod == 0) {
      /*printf("current simulation time: %lld, msgs left: %zd\n", curtime, msgsleft);
      for(int i = 0; i < numdims; i++) {
        printf("Msg leftin %d: %zu\n", i, dirmsgs[i].size());
      }*/
    }
    curtime += delta;
    memset(nodes, 0, numnodes*sizeof(Nodes));
    memset(pe, 0, numnodes*16*sizeof(PE));

    /* go over one dimension at a time and send messages that can go
    simultaneously */
    for(int i = 0; i < numdims; i++) {
      /* go over messages in the particular dimension */
      for(list<Msg>::iterator msgit = dirmsgs[order[i]].begin(); msgit != dirmsgs[order[i]].end();) {
        Msg &currmsg = *msgit;
        canSend = true;
        prevSend = true;
        blocked = false;

        vector< vector< vector<Path> > > &paths = currmsg.paths;
	// not E dimension
        if(order[i] != 4) {
	  // does the node have enough memory b/w to send the message
          if((nodes[currmsg.src].memory + peakbw <= memoryLimit) &&
              (nodes[currmsg.dst].memory + peakbw <= memoryLimit)) {
	    // check if the torus fifo a message is trying to go on is free or not
            if(nodes[currmsg.src].torusfifo[currmsg.torusfifo] == false &&
              pe[currmsg.srcPE].fifos[currmsg.torusfifo] == false) {
	      // go over dimensions that the messages has to be routed over
              for(int j = i; j < numdims - 1; j++) {
                if(!paths[order[j]][0].size())    continue;
                vector<Path> &p = paths[order[j]][0];
		// trying to check whether the entire path for a message is free
		// ASSUMPTION: only one message can be routed over a path in one
		// time quanta
                for(vector<Path>::iterator pathit = p.begin(); pathit != p.end(); pathit++) {
                  Path &currp = *pathit;
                  if(nodes[currp.node].links[currp.link] == true) {
                    canSend = false;
                    prevSend = true;
                  } else if(nodes[currp.node].isblocked[currp.link] == true) {
                    if(prevSend == false) {
                      canSend = false;
                      blocked = true;
                      break;
                    } else {
                      prevSend = false;
                    }
                  } else {
                    prevSend = true;
                  }
                } // end for
                if(!canSend && blocked) break;
              }

	      // book keeping if the message can be send
              if(canSend) {
                canSend = false;
                int j = numdims - 1;
                for(int k = 0; k < 2; k++) {
                  if((k == 0) && !paths[order[j]][k].size())  {
                    canSend = true;
                    break;
                  }
                  if(canSend) break;
                  vector<Path> &p = paths[order[j]][k];
                  for(vector<Path>::iterator pathit = p.begin(); pathit != p.end(); pathit++) {
                    Path &currp = *pathit;
                    if(nodes[currp.node].links[currp.link] == false) {
                      nodes[currp.node].links[currp.link] = true;
                      canSend = true;
                      break;
                    }
                  }
                }
              }

              if(canSend) {
                nodes[currmsg.src].torusfifo[currmsg.torusfifo] = true;
                for(int j = i; j < numdims - 1; j++) {
                  if(!paths[order[j]][0].size())    continue;
                  vector<Path> &p = paths[order[j]][0];
                  for(vector<Path>::iterator pathit = p.begin(); pathit != p.end(); pathit++) {
                    Path &currp = *pathit;
                    nodes[currp.node].links[currp.link] = true;
                  }
                }
                currmsg.bytes -= (delta*bw);
                nodes[currmsg.src].memory += peakbw;
                nodes[currmsg.dst].memory += peakbw;
              } else {
                canSend = true;
                prevSend = true;
                for(int j = i; j < numdims - 1; j++) {
                  if(!paths[order[j]][0].size())    continue;
                  int k = 0;
                  vector<Path> &p = paths[order[j]][k];
                  for(vector<Path>::iterator pathit = p.begin(); pathit != p.end(); pathit++) {
                    Path &currp = *pathit;
                    if(nodes[currp.node].links[currp.link] == true) {
                      //if(blocked) {
                      //  nodes[currp.node].isblocked[currp.link] = true;
                      //} else {
                        canSend = false;
                        break;
                      //}
                      prevSend = true;
                    } else if(nodes[currp.node].isblocked[currp.link] == true) {
                      if(prevSend == false) {
                        canSend = false;
                        break;
                      } else {
                        prevSend = false;
                      }
                    } else {
                      nodes[currp.node].isblocked[currp.link] = true;
                      prevSend = true;
                    }
                  }
                  if(!canSend) break;
                }
              }
            }
            pe[currmsg.srcPE].fifos[currmsg.torusfifo] = true;
          }
        } else { // E messages
          if((nodes[currmsg.src].memory + peakbw <= memoryLimit) &&
              (nodes[currmsg.dst].memory + peakbw <= memoryLimit)) {
            for(int k = 0; k < 2; k++) {
              if(!paths[4][k].size())    continue;
              vector<Path> &p = paths[4][k];
              for(vector<Path>::iterator pathit = p.begin(); pathit != p.end(); pathit++) {
                Path &currp = *pathit;
                if(nodes[currp.node].links[currp.link] == false &&
                  nodes[currmsg.src].torusfifo[currmsg.torusfifo] == false &&
                  pe[currmsg.srcPE].fifos[currmsg.torusfifo] == false) {
                  nodes[currmsg.src].torusfifo[currmsg.torusfifo] = true;
                  nodes[currp.node].links[currp.link] = true;
                  pe[currmsg.srcPE].fifos[currmsg.torusfifo] = true;
                  currmsg.bytes -= (delta*bw);
                  nodes[currmsg.src].memory += peakbw;
                  nodes[currmsg.dst].memory += peakbw;
                }
              }
            }
          }
        }
        if(currmsg.bytes <= 0) msgit = dirmsgs[order[i]].erase(msgit);
        else msgit++;
      } // end for
    } // end for

    /* calculate the number of messages still left to be sent */
    msgsleft = 0;
    for(int i = 0; i < numdims; i++) {
      msgsleft += dirmsgs[i].size();
    }
  } // end of while loop
}
