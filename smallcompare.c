/*
    I was moving large numbers of files from drive to drive, and, due to tedious
  problems not worth repeating here, a trivial rsync was not working, so at
  some point I had to bite the bullet and compare directory listings of the
  partially copied source and destination directories.  I tried using linux
  'diff', but it failed silently due to memory issues, and none of the
  alternatives that I read about at stackexchange etc worked all that well either.
  So... I wrote my own file comparison program.  The input text files are memory
  mapped, and the internal data is a pointer per line which is taken off the heap
  in two large calloc calls; both of those design decisions help greatly in
  handling extremely large input files.

  The first version I posted used a simple sequential/incremental comparison
  algorithm that is fast when used with very large files, and outputs reasonable
  differences as long as there are only a few relatively short insertions or
  deletions, but it has some shortcomings that cause it to output unnecessary
  changes for some inputs.  This version uses a different algorithm which should
  produce better differences (similar to the output of unix 'diff') but only for
  relatively normal file sizes - it is unacceptably slow for really large files,
  which unfortunately were the impetus for writing this in the first place :-(

  This program was inspired by Hamish Dewar's far more elegant "compare" program
  written at Edinburgh University back in the 60's (which predates Unix 'diff').

  Graham Toal  202308302247
 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifndef FALSE
#define FALSE (0!=0)
#endif

#ifndef TRUE
#define TRUE (0==0)
#endif

#define A 0
#define B 1
#define Maxlines 65536
#define Maxlinewidth 512

static char *connect(char *filename, int *fd, off_t *flen, int *eflag) {
  char *conad;
  /* If file is not mappable we could read it into memory instead.  Maybe later. */
  *eflag = 0;
  *fd = open(filename, O_RDONLY, 0);
  if (*fd == -1) { fprintf(stderr, "failed to open input file \"%s\" - %s\n", filename, strerror(errno)); exit(EXIT_FAILURE); }
  *flen = lseek(*fd, (off_t)0, SEEK_END); lseek(*fd, (off_t)0, SEEK_SET);
  conad = mmap(NULL, *flen, PROT_READ, MAP_PRIVATE, *fd, (off_t)0);
  if ((int)conad == -1) { fprintf(stderr, "failed to map input file \"%s\" - %s\n", filename, strerror(errno)); exit(EXIT_FAILURE); }
  return conad;
}

static void disconnect(char *filename, char *conad, int fd, off_t flen, int *eflag) {
  int rc;
  *eflag = 0;
  rc = munmap(conad, flen);
  if (rc) {
    fprintf(stderr, "failed to unmap input file \"%s\" of length %ld at %p - %s\n", filename, (long)flen, conad, strerror(errno)); exit(EXIT_FAILURE);
  } else {
    // flen = lseek(fd, (off_t)0, SEEK_END);
    close(fd);
  }
}

static int Match(char **AA, int Aline, char **BB, int Bline) {
  if (AA[Aline+1]-AA[Aline] != BB[Bline+1]-BB[Bline]) return FALSE; // length test.
  return strncmp(AA[Aline],BB[Bline],AA[Aline+1]-AA[Aline])==0;
}
  
static void Print(char *File[2],
                  char *AB[],
                  int Base,
                  int Low /* inclusive */, int High /* exclusive */,
                  int Flag) {
  int I = Low;
  if (Low==High) return;
  do fprintf(stdout, "\"%s\", %4d: %.*s\n", File[Flag], Base+I+1, AB[I+1]-AB[I]-1, AB[I]); while (++I < High);
}

static int Matches(char **AA, char **BB, int maxlen) {
  int line;
  for (line = 0; line < maxlen; line++) if (!Match(AA,line, BB,line)) return FALSE;
  return TRUE;
}

static void COMPARE(char *File[2],
                    char **AA, int Alines, int Abase,
                    char **BB, int Blines, int Bbase,
                    int *Same) {
  // find the largest common intersection, recurse on areas before and after.
  // (Different options might want to print the common text inbetween if required.)
  // Ideally, to optimise the recursion, if there is more than one longest match,
  // we should take the one nearest the middle, rather than the first one as we're
  // doing now.  But the overhead of continuing the scan to see if there are any more
  // matches of the given length undoubtedly will outweigh the benefit from the more
  // balanced recursion.

  // Abase and Bbase are *only* relevant to printing the results.  As far as this
  // procedure is concerned, the array starts at element 0.
  
  int Astart, Bstart, maxlines = Alines;

  if (Alines <= 0 && Blines <= 0) {       // DONE.
    return;
  } else if (Alines <= 0) {               // Output all B as an insertion into A
    Print(File, BB, Bbase, 0, Blines, B); fprintf(stdout, "--------------\n");
    *Same = FALSE;
    return;
  } else if (Blines <= 0) {               // Output all A as a deletion from A
    Print(File, AA, Abase, 0, Alines, A); fprintf(stdout, "--------------\n");
    *Same = FALSE;
    return;
  }

  if (Blines < maxlines) maxlines = Blines; // common text cannot be longer than 'maxlines' lines.

  Astart = 0; Bstart = 0;
  while (maxlines > 0) {                    // find the longest match possible. Quit as soon as a match is found.
    int Ap = Astart, Bp = Bstart;
    do {                                    // slide the window down the A file
      if (Matches(&AA[Ap], &BB[Bp], maxlines)) {
        COMPARE(File, AA, Ap, Abase, BB, Bp, Bbase, Same);
        // common MIDDLE. don't print.
        COMPARE(File, &AA[Ap+maxlines], Alines-Ap-maxlines, Abase+Ap+maxlines, &BB[Bp+maxlines], Blines-Bp-maxlines, Bbase+Bp+maxlines, Same);
        return;
      }
      Ap += 1;
    } while (Ap <= Alines);
    Ap = Astart; Bp = Bstart;
    do {                                    // slide the window down the B file
      if (Matches(&AA[Ap], &BB[Bp], maxlines)) {
        COMPARE(File, AA, Ap, Abase, BB, Bp, Bbase, Same);
        // common MIDDLE. don't print.
        COMPARE(File, &AA[Ap+maxlines], Alines-Ap-maxlines, Abase+Astart+maxlines, &BB[Bp+maxlines], Blines-Bp-maxlines, Bbase+Bstart+maxlines, Same);
        return;
      }
      Bp += 1;
    } while (Bp <= Blines);
    maxlines -= 1;
  }

  // No matches. Treat as a 1-line delete/replace.
  // TO DO: would be nice if these two lines were merged with similar following lines.
  Print(File, AA, Abase, 0, 1, A);
  Print(File, BB, Bbase, 0, 1, B);
  fprintf(stdout, "--------------\n");
  *Same = FALSE;
  COMPARE(File, &AA[1], Alines-1, Abase+1, &BB[1], Blines-1, Bbase+1, Same);
}

int main(int argc, char **argv) {
static char **AA, **BB;
static char *File[2], *p;
static int Same, flag;
static int Afd, Bfd;
static char *Aconad, *Bconad;
static int Alines, Blines, Aline, Bline;  
static off_t Aflen, Bflen;

  if ((argc > 1 && strcmp(argv[1], "-h") == 0) || argc != 3) {
    fprintf(stderr, "Syntax: compare oldfile newfile\n");
    exit(EXIT_SUCCESS);
  }

  File[A] = strdup(argv[1]); File[B] = strdup(argv[2]);  // Sorry, no clever options supported.

  Aconad = connect(File[A], &Afd, &Aflen, &flag);
  Bconad = connect(File[B], &Bfd, &Bflen, &flag);
  
  /* lazy hack! */
  while (strlen(File[A]) < strlen(File[B])) {File[A] = realloc(File[A], strlen(File[A])+2); strcat(File[A], " ");}
  while (strlen(File[B]) < strlen(File[A])) {File[B] = realloc(File[B], strlen(File[B])+2); strcat(File[B], " ");}
  
  Alines = 0; Blines = 0;
  for (p = Aconad; p < Aconad+Aflen; p++) if (*p == '\n') Alines++;
  for (p = Bconad; p < Bconad+Bflen; p++) if (*p == '\n') Blines++;
  AA = calloc(Alines+1, sizeof(char *));
  BB = calloc(Blines+1, sizeof(char *));
  if (AA == NULL || BB == NULL) {
    fprintf(stderr, "* Internal error: insufficient RAM available to store pointers to every line.\n");
    exit(EXIT_FAILURE);
  }

  Aline = 0;
  AA[Aline++] = Aconad;
  for (p = Aconad; p < Aconad+Aflen; p++) if (*p == '\n') AA[Aline++] = p+1;
  AA[Aline] = Aconad+Aflen;
  Alines = Aline-1;

  Bline = 0;
  BB[Bline++] = Bconad;
  for (p = Bconad; p < Bconad+Bflen; p++) if (*p == '\n') BB[Bline++] = p+1;
  BB[Bline] = Bconad+Bflen;
  Blines = Bline-1;

  Same = TRUE; COMPARE(File, AA, Alines, 0, BB, Blines, 0, &Same); fflush(stdout);
  if (Same) fprintf(stderr, "Files are identical\n");
  
  disconnect(File[A], Aconad, Afd, Aflen, &flag);
  disconnect(File[B], Bconad, Bfd, Bflen, &flag);

  exit(EXIT_SUCCESS);
  return (EXIT_FAILURE);
}
