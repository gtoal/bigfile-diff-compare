/*
    I was moving large numbers of files from drive to drive and due to boring
  problems not worth repeating here, a trivial rsync was not cutting it, so at
  some point I had to bite the bullet and get a file listing of the from and
  to directories, and compare them.  Turned out that linux 'diff' was failing
  silently due to mempry issues, and none of the alternatives that I read
  about at stackexchange etc were working that well either.  So... I wrote
  my own file comparison program.  The input text files are memory mapped,
  and the internal data is a pointer per line which is taken off the heap
  in two large calloc calls.  It works and it's fast enough.  The comparison
  logic isn't super clever but it's good enough for all the real-world
  problems I've used it for so far.  I expect this to be used when there
  are relatively small differences in extremely large files.

  Perhaps its biggest infelicity is that it will synchronise on a blank line.
  If that just isn't acceptable to you I think the fix would be around the
  call to "Match"

  The code is a bit crude and it's sort of based on my recollection of
  Hamish Dewar's far more elegant "compare" program from Edinburgh University
  back in the 60's.

  Graham Toal  202308290145
 
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

static char *Afile;
static char *Bfile;
static char *File[2];
static char *p;

static char **AA, **BB;

static int Base[2];
static int Acount;
static int Bcount;
static int Anum;
static int Bnum;
static int Same;
static int Afd, Bfd;
static char *Aconad, *Bconad;
static int maxgap; /* intention was that an insertion longer than this would fail.  Not yet done. */
static int flag;
static int Alines, Blines, Aline, Bline;  
static int LastLine, LastFlag;

static off_t Aflen, Bflen;

#define A 0
#define B 1
#define Maxlines 65536
#define Maxlinewidth 512

static char *connect(char *filename, int *fd, off_t *flen, int *eflag) {
  char *conad;
  /* If file is not mappable we could read it into memory instead. */
  *eflag = 0;
  *fd = open(filename, O_RDONLY, 0);
  if (*fd == -1) {
    fprintf(stderr, "failed to open input file \"%s\" - %s\n", filename, strerror(errno)); exit(EXIT_FAILURE);
  }
  *flen = lseek(*fd, (off_t)0, SEEK_END); lseek(*fd, (off_t)0, SEEK_SET);
  conad = mmap(NULL, *flen, PROT_READ, MAP_PRIVATE, *fd, (off_t)0);
  if ((int)conad == -1) {
    fprintf(stderr, "failed to map input file \"%s\" - %s\n", filename, strerror(errno)); exit(EXIT_FAILURE);
  }
  return conad;
}

static void disconnect(char *filename, char *conad, int fd, off_t flen, int *eflag) {
  int rc;
  *eflag = 0;
  rc = munmap(conad, flen);
  if (rc) {
    fprintf(stderr, "failed to unmap input file \"%s\" of length %ld at %p - %s\n", filename, (long)flen, conad, strerror(errno)); exit(EXIT_FAILURE);
  } else {
    //flen = lseek(fd, (off_t)0, SEEK_END);
    close(fd);
  }
}

static int Match(int Aline, int Bline) {
  if (AA[Aline+1]-AA[Aline] != BB[Bline+1]-BB[Bline]) return FALSE;
  return strncmp(AA[Aline],BB[Bline],AA[Aline+1]-AA[Aline])==0;
}
  
static void Print(char *AB[], int Low, int High, int Flag) {
  int I = Low;
  do fprintf(stdout, "\"%s\", %4d: %.*s\n", File[Flag], I+1, AB[I+1]-AB[I]-1, AB[I]); while (++I <= High);
}

/* Differences are mostly output one line at at time.  This hack merges
   consecutive lines into a block.  If anyone wanted to change the output
   to be compatible with linux 'diff', this hack would not be practical. */
static void Separator(int Line, int Flag) {
  if ((Flag == LastFlag) && (Line == LastLine+1)) {
    /* Suppress boundary after printing, decide to output it before printing the next set. */
  } else {
    fprintf(stdout, "--------------\n");
  }
  LastLine = Line; LastFlag = Flag;
}

int main(int argc, char **argv) {
  
  maxgap = 1000;
  LastLine = -1; LastFlag = -1;

  Anum = Maxlines;
  Bnum = Maxlines;
  Base[A] = 0;
  Base[B] = 0;
  Acount = 0;
  Bcount = 0;

  if ((argc > 1 && strcmp(argv[1], "-h") == 0) || argc != 3) {
    fprintf(stderr, "Syntax: compare oldfile newfile\n");
    exit(EXIT_SUCCESS);
  }

  // Sorry, no clever options suported.
  Afile = argv[1]; Bfile = argv[2];
  File[A] = strdup(Afile); File[B] = strdup(Bfile);

  /* lazy hack! */
  while (strlen(File[A]) < strlen(File[B])) {File[A] = realloc(File[A], strlen(File[A])+2); strcat(File[A], " ");}
  while (strlen(File[B]) < strlen(File[A])) {File[B] = realloc(File[B], strlen(File[B])+2); strcat(File[B], " ");}
  
  Aconad = connect(Afile, &Afd, &Aflen, &flag);
  Bconad = connect(Bfile, &Bfd, &Bflen, &flag);

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
  for (p = Aconad; p < Aconad+Aflen; p++) {
    if (*p == '\n') AA[Aline++] = p+1;
  }
  AA[Aline] = Aconad+Aflen;
  Alines = Aline-1;

  Bline = 0;
  BB[Bline++] = Bconad;
  for (p = Bconad; p < Bconad+Bflen; p++) {
    if (*p == '\n') BB[Bline++] = p+1;
  }
  BB[Bline] = Bconad+Bflen;
  Blines = Bline-1;

  Base[A] = Aline = 0; Base[B] = Bline = 0; /* Base[] (exclusive) represents the matches/diffs already output */
  Same = TRUE;

  /* MAIN LOOP */
  
  for (;;) {
    if ((Base[A] >= Alines) && (Base[B] >= Blines)) break;
    if (Base[A] >= Alines) {
      /* Output remaining B's */
      Separator(Blines-1, B);
      Print(BB, Base[B], Blines-1, B);
      Same = FALSE;
      Base[B] = Blines;
      continue;
    }
    if (Base[B] >= Blines) {
      /* Output remaining A's */
      Separator(Alines-1, A);
      Print(AA, Base[A], Alines-1, A);
      Same = FALSE;
      Base[A] = Alines;
      continue;
    }
    if (Match(Base[A], Base[B])) {
      Base[A]++; Base[B]++; continue;
    } else Same = FALSE;

    /* Now we have a mismatch, test A and B for an insertion */
    /* (if no insertion, we need to resynch and output the changed block) */

    Aline = Base[A]; Bline = Base[B];
    int noMatchForA = FALSE, noMatchForB = FALSE;
    /* See which resynchs first */
    for (;;) {
      Bline = Bline+1;
      if (Bline >= Blines) {
        noMatchForA = TRUE;
        break; /* No match found */
      }
      if (Match(Aline, Bline)) break;
    }
    Bcount = Bline - Base[B];
    Bline = Base[B];
    for (;;) {
      Aline = Aline+1;
      if (Aline >= Alines) {
        noMatchForB = TRUE;
        break; /* No match found */
      }
      if (Match(Aline, Bline)) break;
    }
    Acount = Aline-Base[A];

    if (noMatchForA && noMatchForB) {
      Separator(-1,-1);
      Print(AA, Base[A], Base[A], A);
      Print(BB, Base[B], Base[B], B);   
      Base[A]++; Base[B]++;
      Same = FALSE;
      continue;
    } else if (noMatchForA) {
      Separator(Base[A], A);
      Print(AA, Base[A], Base[A], A);
      Base[A]++;
      Same = FALSE;
      continue;
    } else if (noMatchForB) {
      Separator(Base[B], B);
      Print(BB, Base[B], Base[B], B);   
      Base[B]++;
      Same = FALSE;
      continue;
    }
    
    if (Acount < Bcount) {
      Separator(Base[A]+Acount-1, A);
      Print(AA, Base[A], Base[A]+Acount-1, A);
      Base[A] += Acount;
      Same = FALSE;
    } else if (Bcount < Acount) {
      Separator(Base[B]+Bcount-1, B);
      Print(BB, Base[B], Base[B]+Bcount-1, B);
      Base[B] += Bcount;
      Same = FALSE;
    } else {
      Separator(-1,-1);
      Print(AA, Base[A], Base[A], A);
      Print(BB, Base[B], Base[B], B);
      Base[A]++; Base[B]++;
      Same = FALSE;
    }
    
  }
  
  fflush(stdout);
  if (Same) {
    fprintf(stderr, "Files are identical\n");
  } else {
    Separator(-1,-1);
  }
  
  disconnect(Afile, Aconad, Afd, Aflen, &flag);
  disconnect(Bfile, Bconad, Bfd, Bflen, &flag);

  exit(EXIT_SUCCESS);
  return (1);
}
