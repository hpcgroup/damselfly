//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
//
// Written by:
//     Nikhil Jain <nikhil.jain@acm.org>
//     Abhinav Bhatele <bhatele@llnl.gov>
//     Peer-Timo Bremer <ptbremer@llnl.gov>
//
// LLNL-CODE-678961. All rights reserved.
//
// This file is part of Damselfly. For details, see:
// https://github.com/LLNL/damselfly
// Please also read the LICENSE file for our notice and the LGPL.
//////////////////////////////////////////////////////////////////////////////

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
