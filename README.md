# 8BitDiff
A binary patching format suitable for decoding on 8 bit cpus

![8bitdiff](/media/8BitDiff.png)
Distribution of injected bytes, source file bytes and target duplicates.

## Format

8BDIFF FORMAT
-------------
- 4 bits: size of offset bit sizes
- 4 bits: size of length bit sizes
- 1 byte length bit sizes
- 1 byte offset bit sizes
- 2-4 bytes size of injected bytes (4 bytes if first byte msb set, only 2 bytes supported on 8 bit CPUs)
- injected bytes
- loop until end of inject buffer:
-  bit: 0=inject, 1=source or target
-  length bit cnt+length bits
-  if source or target:
-   buffer offset bit cnt + buffer offset bits
- 	 sign of offset (not instead of negate)
- 	 bit: 0=source, 1=target
- repeat loop

Target/source/inject source pointers start at the start of each buffer.
Any pointer is set to the end of the run after copy and the offset increments/decrements for source for a negative offset, invert the number, don't negate.

USAGE (6502)
------------

- Use the 8BDIFF tool (https://github.com/sakrac/8BitDiff/tools/8BitDiff.cpp) to generate a patch between two files. Load the original file and the patch file and assign them as parameters:
- z8BDiff = Address of patch (ZP)
- z8BSrc = Address of original data (ZP)
- z8BDst = Address to decode updated data (ZP)
- jsr Patch_8BDiff
- Address of the first byte after the patched data is now in z8BDst (ZP)

USAGE (Z80)
------------

- Use the 8BDIFF tool (https://github.com/sakrac/8BitDiff/tools/8BitDiff.cpp) to generate a patch between two files. Load the original file and the patch file and assign them as parameters:
- BC = Pointer to original data
- DE = Pointer to where to write patched data
- HL = Pointer to diff data (from the tool)
- call Patch_8BDiff
- Pointer to the first byte after the patched data is returned in DE  
Note that the Z80 decoder implementation is untested and very likely not working at all.  

USAGE (68000)
------------

- Use the 8BDIFF tool (https://github.com/sakrac/8BitDiff/tools/8BitDiff.cpp) to generate a patch between two files. Load the original file and the patch file and assign them as parameters:
- a0 = Pointer to diff data (from the tool)
- a1 = Pointer to original data
- a2 = Pointer to where to write patched data
- jsr Patch_8BDiff
- Pointer to the first byte after the patched data is returned in a2  
Note that the 68000 decoder implementation is untested and very likely not working at all.  

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
