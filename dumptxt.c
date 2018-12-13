#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "config.h"

#define DEBUG 1

void usage(char *progname)
{
  fprintf(stderr, "Usage: %s -f <file>\n", progname);
}

int main(int argc, char *argv[]) {
  int infile = -1;
  unsigned char *fmap=NULL;
  struct stat filestats;
  uint16_t *stri;
  int offset = 0, o;
  while ((o=getopt(argc, argv, "f:"))!=-1) {
    switch (o)
      {
      case 'f':
        if ((infile=open(optarg, O_RDONLY)) == -1) {
          perror(optarg);
          return -1;
        }
        break;
      default:
        usage(argv[0]);
        return -1;
        break;
      }
  }
  if (infile == -1) {
    usage(argv[0]);
    return -1;
  }
  if (fstat(infile, &filestats) != 0) {
    perror("fstat()");
    return -2;
  }
#ifdef DEBUG
  fprintf(stderr, "File is %lld bytes.\n", filestats.st_size);
#endif
  fmap = mmap(NULL, filestats.st_size, PROT_READ, MAP_FILE | MAP_PRIVATE, infile, 0);
  if (fmap == MAP_FAILED) {
    perror("mmap()");
    return -3;
  }
  stri = (uint16_t *)(&(fmap[offset]));
  while (*stri != 0) {
    offset += 2;
    printf("%d:\t%s\n", *stri, &(fmap[offset]));
    offset += (strlen((char *)(&(fmap[offset]))) + 2) & ~1;
    stri = (uint16_t *)(&(fmap[offset]));
  }
  munmap(fmap, filestats.st_size);
  close(infile);
  return 0;
}
