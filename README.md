# 8BitDiff
A binary patching format suitable for decoding on 8 bit cpus

# Background

I have previously created a VCDIFF decoder in c++ (https://tools.ietf.org/html/rfc3284), which is a curious format. so I thought I would try to implement a version in 6502 assembler.
VCDIFF already requires a code table of 1536 bytes, plus by default 768 bytes of "same cache" and 4 bytes of "near cache" and when the code reached 512 bytes of reading out setup for decoding I decided to give up on VCDIFF and try a simpler patching format.
Based on files I've patched the patch size is smaller or comparable to VCDIFF. This is just a hobby project and not much effort has been put into testing or other validation. It should be possible to encode with a 0 size source file.
