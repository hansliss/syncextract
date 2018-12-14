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
#define BLOCKSIZE 0x200
#define BUFSIZE 131072

void usage(char *progname)
{
  fprintf(stderr, "Usage: %s -f <file> [-d <output dir>]\n", progname);
}

typedef struct direntrymap_s {
  unsigned char filename[8];
  uint16_t offset_blocks;
  uint16_t length_blocks;
  uint16_t checksum;
  uint16_t type;
} *direntrymap;

typedef struct direntry_s {
  unsigned char filename[9];
  int offset;
  int length;
  uint16_t checksum;
  uint16_t type;
  struct direntry_s *next;
} *direntry;

void addDirentry(direntry *dir, unsigned char *filename, int offset, int length, int type, int checksum) {
  if (*dir != NULL && strcasecmp((char *)((*dir)->filename), (char *)filename) < 0) {
    addDirentry(&((*dir)->next), filename, offset, length, type, checksum);
  } else {
    direntry tmp = (direntry)malloc(sizeof(struct direntry_s));
    tmp->next = *dir;
    strcpy((char *)(tmp->filename), (char *)filename);
    tmp->offset = offset;
    tmp->length = length;
    tmp->type = type;
    tmp->checksum = checksum;
    *dir = tmp;
  }
}

void freeDir(direntry *dir) {
  if (*dir != NULL) {
    freeDir(&((*dir)->next));
    free(*dir);
    *dir = NULL;
  }
}

int array_null(unsigned char *array, int numelem) {
  int i=0;
  while (i<numelem) {
    if (array[i] != 0) {
      return 0;
    }
    i++;
  }
  return 1;
}

// Free space: filename all nulls, offset_blocks and length_block non-empty
// End of directory: all nulls

int readDir(direntry *directory, unsigned char *filbuf, char *dirname) {
  int count= 0;
  int i = 0;
  int done = 0;
  int free = 0;
  unsigned char fname[9];
  direntrymap dmap;
  while (!done) {
    dmap = (direntrymap)(&(filbuf[i]));
    if (array_null(dmap->filename, sizeof(dmap->filename))) {
      if (dmap->offset_blocks == 0 && dmap->length_blocks == 0 && dmap->checksum == 0)
	done = 1;
      else {
	free += dmap->length_blocks * BLOCKSIZE;
#ifdef DEBUG
	fprintf(stderr, "Adding free space: %d bytes.\n", dmap->length_blocks * BLOCKSIZE);
#endif
      }
    } else {
      memcpy(fname, dmap->filename, 8);
      fname[8] = '\0';
      addDirentry(directory, fname, dmap->offset_blocks * BLOCKSIZE, dmap->length_blocks * BLOCKSIZE, dmap->type, dmap->checksum);
#ifdef DEBUG
      fprintf(stderr, "Adding file %d: \"%s/%s\", type=%d (%s), offset = %d, size = %d bytes, checksum = %d/0x%04x.\n",
	      count,
	      dirname,
	      fname,
	      dmap->type, dmap->type==0?"text":(dmap->type==1?"binary":(dmap->type==3?"overlay?":(dmap->type==6?"subdirectory":"unknown"))),
	      dmap->offset_blocks * BLOCKSIZE,
	      dmap->length_blocks * BLOCKSIZE,
	      dmap->checksum, dmap->checksum);
#endif
      count++;
    }
    i += sizeof(struct direntrymap_s);
  }
#ifdef DEBUG
  fprintf(stderr, "Free space on disk: %d bytes.\n", free);
#endif
  return count;
}

int extractFiles(direntry d, unsigned char *filbuf, char *dirname) {
  struct stat dirstats;
  FILE *ofile;
  int count = 0;
  char ofilename[BUFSIZE];
  if (stat(dirname, &dirstats) != 0) {
    perror(dirname);
    return 0;
  }
  if (!(dirstats.st_mode & S_IFDIR)) {
    fprintf(stderr, "%s is not a directory\n", dirname);
    return 0;
  }
  while (d) {
    sprintf(ofilename, "%s/%s", dirname, d->filename);
    if (d->type == 6) {
      mkdir(ofilename, 0777);
      direntry d2 = NULL;
      readDir(&d2, &(filbuf[d->offset]), ofilename);
      extractFiles(d2, &(filbuf[d->offset]), ofilename);
      freeDir(&d2);
    } else {
      if (!(ofile = fopen(ofilename, "w"))) {
	perror(ofilename);
	return 0;
      }
      fwrite(&(filbuf[d->offset]), 1, d->length, ofile);
      fclose(ofile);
    }
    count++;
    d = d->next;
  }
  return count;
}

int main(int argc, char *argv[]) {
  int infile = -1;
  unsigned char *fmap=NULL;
  direntry d = NULL;
  char *dirname = ".";
  struct stat filestats;
  int nfiles, o;
  while ((o=getopt(argc, argv, "f:d:"))!=-1) {
    switch (o)
      {
      case 'f':
        if ((infile=open(optarg, O_RDONLY)) == -1) {
          perror(optarg);
          return -1;
        }
        break;
      case 'd':
	dirname = optarg;
        break;
      default:
        usage(argv[0]);
        return -1;
        break;
      }
  }
  if (optind < argc) {
    usage(argv[0]);
    return -1;
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
  nfiles=readDir(&d, fmap, dirname);
  extractFiles(d, fmap, dirname);
  munmap(fmap, filestats.st_size);
  close(infile);
  freeDir(&d);
}
