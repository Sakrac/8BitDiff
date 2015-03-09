# 8BitDiff
A binary patching format suitable for decoding on 8 bit cpus

# Background

I have previously created a VCDIFF (https://tools.ietf.org/html/rfc3284) ahich is a curious format so I thought I would try to implement a version in 6502 assembler. After the decode was over 2kb including tables and caches I decided to roll my own. Based on files I've patched the patch size is smaller or comparable to VCDIFF. This is just a hobby project and not much effort has been put into testing or other validation. It should be possible to encode with a 0 size source file.
