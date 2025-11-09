
/* "cmpcode" is a program that compares two G.723.1 encoded bit streams.
   See the usage() function for invocation syntax.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int  sequence_length = 10;    /* number of frames per sequence */
int  verbose = 0;

char* help_table[] = {
  "Usage:  cmpcode [-v] [Rnum] file1 file2",
  "",
  "  cmpcode compares every frame sequence in file1 and file2, and displays",
  "  the percentage of identical sequences.  A frame sequence consists of",
  "  10 frames, unless overridden by -R.",
  "",
  "  If -v is given, the sequence number of each nonmatching sequence",
  "  is also displayed.",
  "",
  NULL
};

void
usage()
{
  char **txtpp;

  for (txtpp = &help_table[0]; *txtpp; txtpp++)
    (void) fprintf(stderr, "%s\n", *txtpp);
}

/* Read 'num_frames' encoded frames into 'buf'
   Return the number of bytes read.
 */

int
read_frame_sequence(unsigned char *buf, int num_frames, FILE *fp)
{
  int i, count, FrType, tot = 0;

  for (i = 0; i < num_frames; i++)
  {
    if (fread(buf, 1, 1, fp) != 1) {
      if (i > 0) {
        (void) fprintf(stderr,"WARNING: file size is not a multiple of %d frames\n", num_frames);
      }
      break;
    }
    FrType = buf[0] & 0x3;
    tot++;
    buf++;

    if (FrType == 2)
      count = 3;
    else if (FrType == 0)
      count = 23;
    else if (FrType == 1)
      count = 19;
    else
      count = 0;
    if (count > 0)
    {
      if ((int)fread(buf, 1, count, fp) != count) {
        (void) fprintf(stderr,"ERROR: corrupt frame encountered at end of file\n");
        usage(); exit(1);
      }
    }
    buf += count;
    tot += count;
  }

  return tot;
}

void
compare_streams(FILE *if1, FILE *if2, FILE *ofp)
{
  unsigned char *buf1, *buf2;
  int seqnum, len1, len2;
  int matches = 0;

  buf1 = (unsigned char*) malloc(24 * sequence_length);
  buf2 = (unsigned char*) malloc(24 * sequence_length);
  if (buf1 == NULL || buf2 == NULL) {
    (void) fprintf(stderr,"ERROR: out of memory allocating buffers\n");
    usage(); exit(1);
  }

  seqnum = 0;

  for (; (len1 = read_frame_sequence(buf1,sequence_length,if1)) > 0; seqnum++)
  {
    if ((len2 = read_frame_sequence(buf2, sequence_length, if2)) == 0) {
      (void) fprintf(stderr,"ERROR: second file is shorter than first\n");
      usage(); exit(1);
    }

    if (len1 != len2 || memcmp(buf1, buf2, len1)) {
      if (verbose)
        (void) fprintf(ofp, "sequence %d does not match\n", seqnum);
    }
    else
      matches++;
  }

  if ((len2 = read_frame_sequence(buf2, sequence_length, if2)) > 0) {
    (void) fprintf(stderr,"ERROR: first file is shorter than second\n");
    usage(); exit(1);
  }

  if (seqnum == 0)
    (void) fprintf(ofp,"  0.00%%\n");
  else
    (void) fprintf(ofp,"%5.2f%%\n", ((double)100.0 * matches)/(double)seqnum);

  (void) free(buf1);
  (void) free(buf2);
}

int
main(int argc, char *argv[])
{
  int   i, f1_idx = 0, f2_idx = 0;
  FILE  *ifp1, *ifp2;

  for (i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "-v"))
      verbose = 1;
    else if (!strncmp(argv[i], "-R", 2))
    {
      sequence_length = strtol(argv[i]+2, NULL, 10);
      if (sequence_length <= 0) {
        (void) fprintf(stderr, "ERROR:  invalid -R value %s\n", argv[i]+2);
        exit(1);
      }
    }
    else if (f1_idx == 0)
      f1_idx = i;
    else if (f2_idx == 0)
      f2_idx = i;
    else {
      (void) fprintf(stderr, "ERROR: Too many input files specified\n");
      usage(); exit(1);
    }
  }

  if (f1_idx == 0 || f2_idx == 0) {
    (void) fprintf(stderr, "ERROR: Too few input files were specified\n");
    usage(); exit(1);
  }

  if ((ifp1 = fopen(argv[f1_idx], "rb")) == NULL) {
    (void) fprintf(stderr, "ERROR:  could not open file %s\n", argv[f1_idx]);
    usage(); exit(1);
  }
  if ((ifp2 = fopen(argv[f2_idx], "rb")) == NULL) {
    (void) fprintf(stderr, "ERROR:  could not open file %s\n", argv[f2_idx]);
    usage(); exit(1);
  }

  compare_streams(ifp1, ifp2, stdout);

  (void) fclose(ifp1);
  (void) fclose(ifp2);

  return 0;
}
