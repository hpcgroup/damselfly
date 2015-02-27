#include "stdio.h"
#include "stdlib.h"

//Usage ./binary num_groups num_rows num_columns intra_file inter_file

int main(int argc, char **argv) {
  int g = atoi(argv[1]);
  int r = atoi(argv[2]);
  int c = atoi(argv[3]);

  FILE *intra = fopen(argv[4], "wb");
  FILE *inter = fopen(argv[5], "wb");
 
  int router = 0;
  int green = 0, black = 1;
  for(int groups = 0; groups < g; groups++) {
    for(int rows = 0; rows < r; rows++) {
      for(int cols = 0; cols < c; cols++) {
        if(groups == 0) {
          for(int cols1 = 0; cols1 < c; cols1++) {
            if(cols1 != cols) {
              int dest = (rows * c) + cols1;
              fwrite(&router, sizeof(int), 1, intra);
              fwrite(&dest, sizeof(int), 1, intra);
              fwrite(&green, sizeof(int), 1, intra);
              //printf("%d %d %d\n", router, dest, green);
            }
          }
          for(int rows1 = 0; rows1 < r; rows1++) {
            if(rows1 != rows) {
              int dest = (rows1 * c) + cols;
              fwrite(&router, sizeof(int), 1, intra);
              fwrite(&dest, sizeof(int), 1, intra);
              fwrite(&black, sizeof(int), 1, intra);
              //printf("%d %d %d\n", router, dest, green);
            }
          }
        }
        int myOff = router % (r * c);
        int numLink = g / (r*c);
        if(g % (r*c) != 0) {
          if((router % (r*c)) < (g % (r*c))) {
            numLink++;
          }
        }
        int myG = router / (r * c);
        for(int blues = 0; blues < numLink; blues++) {
          int dest = (blues * r * c) + myOff;
          if(dest != myG) {
            dest = (dest * r * c ) + (myG % (r * c));
            fwrite(&router, sizeof(int), 1, inter);
            fwrite(&dest, sizeof(int), 1, inter);
            //printf("%d %d\n", router, dest);
          }
        }
        router++;
      }
    }
  }

  fclose(intra);
  fclose(inter);
}
