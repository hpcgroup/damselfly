#include "stdio.h"
#include "stdlib.h"

int main(int argc, char** argv) {

  FILE *input = fopen(argv[1], "r");
  FILE *output = fopen(argv[2], "wb");
  
  int src, dst;
  double bytes;

  while(!feof(input)) {
    fscanf(input, "%d %d %lf\n", &src, &dst, &bytes);
    fwrite(&src, sizeof(int), 1, output);
    fwrite(&dst, sizeof(int), 1, output);
    fwrite(&bytes, sizeof(double), 1, output);
  }

  fclose(input);
  fclose(output);
}
