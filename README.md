# bigfile-diff-compare
A simple text file comparison utility for extremely large text files that fail under diff

  I was moving large numbers of files from drive to drive and due to boring
  problems not worth repeating here, a trivial rsync was not cutting it, so at
  some point I had to bite the bullet and get a file listing of the from and
  to directories, and compare the listings with 'diff'.  Turned out that linux 'diff' was failing
  silently due to memory issues, and none of the alternatives that I read
  about at stackexchange etc were working that well either.  So... I wrote
  my own file comparison program.  The input text files are memory mapped,
  and the internal data is a pointer per line which is taken off the heap
  in two large calloc calls.  It works and it's fast enough.  The comparison
  logic isn't super clever but it's good enough for all the real-world
  problems I've used it for so far.  I expect this to be used when there
  are relatively small differences in extremely large files.

  Perhaps its biggest infelicity is that it will synchronise on a blank line.
  If that just isn't acceptable to you I think the fix would be around the
  call to "Match".  I added a new program, 'smallcompare', later, which uses
  a different algorithm to produce better differences, but it is too slow for
  using on really large files.  I just include it here in case it's useful
  but honestly you might as well use 'diff' for anything that smallcompare
  would be suitable for.

  The code is a bit crude and it's sort of based on my recollection of
  Hamish Dewar's far more elegant "compare" program from Edinburgh University
  back in the 60's.

  Graham Toal  202308290145
