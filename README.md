# 8BitDiff
A binary patching format suitable for decoding on 8 bit cpus

## Background

I have previously created a VCDIFF decoder in c++ (https://tools.ietf.org/html/rfc3284), which is a curious format.
so I thought I would try to implement a version in 6502 assembler.
   VCDIFF already requires a code table of 1536 bytes, plus by default 768 bytes of "same cache" and 4
bytes of "near cache" and when the code reached 512 bytes of reading out setup for decoding I decided
to give up on VCDIFF and try a simpler patching format.
   Based on files I've patched the patch size is smaller or comparable to VCDIFF.
   This is just a hobby project and not much effort has been put into testing or other validation.
   It should be possible to encode with a 0 size source file.

## What about VCDIFF?
VCDIFF is great for it's purpose, but with multiples of kb of tables it doesn't make any sense for
CPUs with a 16 bit address bus. 8BitDiff was optimized around the idea of applying the patch on
a CPU with only 3 registers with a small memory footprint. Interestingly 8BitDiff seems to produce
smaller patch files than VCDIFF. Currently only up to 4 GB files can be patched.