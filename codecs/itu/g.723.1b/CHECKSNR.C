/* Compute the SNR between two files and emit PASS or FAIL
   depending on whether the SNR exceeds a threshold.
   Invocation syntax is

       checksnr file1 file2 -snrmin<threshold>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int
main(int argc, char *argv[])
{
  int    i;
  FILE  *fp1, *fp2;
  char   *fname1 = NULL, *fname2 = NULL;
  short  sample1, sample2;
  double energy = 0.0, err = 0.0, snr_threshold, tmp;

  snr_threshold = 1000.0;

  if (sizeof(short) != 2) {
    (void) fprintf(stderr, "ERROR: sizeof(short) is not 2 on this machine\n");
    exit(1);
  }

  for (i = 1; i < argc; i++)
  {
    if (!strncmp(argv[i], "-snrmin", 7))
      snr_threshold = strtod(argv[i]+7, NULL);
    else if (fname1 == NULL)
      fname1 = argv[i];
    else if (fname2 == NULL)
      fname2 = argv[i];
    else {
      (void) fprintf(stderr, "ERROR: SNR requires exactly two input files\n");
      exit(1);
    }
  }

  if (fname2 == NULL) {
    (void) fprintf(stderr, "ERROR: less than two input files were specified\n");
    exit(1);
  }

  if ((fp1 = fopen(fname1, "rb")) == NULL) {
    (void) fprintf(stderr, "ERROR:  could not open file %s\n", fname1);
    exit(1);
  }
  if ((fp2 = fopen(fname2, "rb")) == NULL) {
    (void) fprintf(stderr, "ERROR:  could not open file %s\n", fname2);
    exit(1);
  }

  while (fread(&sample1, sizeof(short), 1, fp1) == 1)
  {
    if (fread(&sample2, sizeof(short), 1, fp2) != 1) {
      (void) fprintf(stderr,"ERROR:  file %s is shorter than %s\n", fname2, fname1);
      exit(1);
    }

    energy += (double)sample1 * (double)sample1;
    tmp = sample1 - sample2;
    err += tmp*tmp;
  }

  if (fread(&sample2, sizeof(short), 1, fp2) == 1) {
    (void) fprintf(stderr,"ERROR:  file %s is larger than %s\n", fname2, fname1);
    exit(1);
  }

  if (err == 0.0)
    (void) fprintf(stdout, "SNR PASSED(%s): infinity >= %7.2f\n", fname2, snr_threshold);
  else if (energy == 0.0)
      (void) fprintf(stdout, "SNR FAILED(%s): -infinity < %7.2f\n", fname2, snr_threshold);
  else {
    double SNR = 10.0 * log10(energy/err);
    if (SNR >= snr_threshold)
      (void) fprintf(stdout, "SNR PASSED(%s): %7.2f >= %7.2f\n", fname2, SNR, snr_threshold);
    else
      (void) fprintf(stdout, "SNR FAILED(%s): %7.2f < %7.2f\n", fname2, SNR, snr_threshold);
  }

  (void) fclose(fp1);
  (void) fclose(fp2);

  return 0;
}
