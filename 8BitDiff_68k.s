;
; 68K Decoder for 8BitDiff
;
;	Copyright 2015 Carl-Henrik Skårstedt. All rights reserved.
;			https://github.com/sakrac/8BitDiff/
;
; Important note:
;	This implementation is not tested, it is just provided
;	as a first attempt at a 68000 implementation of a decoder
;	for 8BitDiff patches. I have probably missed a large number
;	of potential improvements which could make this more stable,
;	smaller or faster.
;
;	Suggestions, improvements and tests are appreciated.
;
;	8BDIFF FORMAT
;	-------------
;	4 bits: size of offset bit sizes
;	4 bits: size of length bit sizes
;	1 byte length bit sizes
;	1 byte offset bit sizes
;	2-4 bytes size of injected bytes (first bit means 4 bytes)
;	injected bytes
;	loop until end of inject buffer:
;	 bit: 0=inject, 1=source or target
;	 length bit cnt+length bits
;	 if source or target:
;	  buffer offset bit cnt + buffer offset bits
;	 sign of offset (not instead of negate)
;	 bit: 0=source, 1=target
;	repeat loop

; Use the 8BDIFF tool (https://github.com/sakrac/8BitDiff)
; to generate a patch between two files (diff)
; INPUT:
;	a0 = diff
;	a1 = source
;	a2 = dest
;
; OUTPUT:
;	a2 = end of patched data
;

Patch_8BDiff:
	move.l a2, a3 	; target read
	move.b (a0)+, d0
	move.b d0, d1
	and.l #$f, d0 	; bits per length
	lsr #4, d1
	and.l #$f, d1	; bits per offset
	move.l a0, a4	; "length" bit lengths
	moveq #1, d2
	lsl d0, d2		; skip 1<<"length" bytes
	add.l d2, a0
	move.l a0, a5	; offset bit lengths
	moveq #1,d2
	lsl d1, d2		; skip 1<<"offset" bytes
	add.l d2, a0
	move.b (a0)+, d2 ; guaranteed odd address
	lsl #8, d2
	move.b (a0)+, d2
	tst.w d2
	bpl .Inject16b
	and #$7fff, d2
	swap d2
	move.b (a0)+, d2
	lsl #8, d2
	move.b (a0)+, d2
.Inject16b
	move.l a0, a6	; inject buffer
	add.l d2, a0
	move.l a0, d6

	move.b #$80, d2	; bit accumulator
.PatchLoop
	bsr .ReadBit
	bcs .SrcOrTrg
	cmp a0, d6
	blt .NotComplete
	rts
.NotComplete
	move.l d0, d3
	bsr .ReadBits
	move.b (a4, d4), d3
	bsr .ReadBits
.CopyInject
	move.b (a6)+, (a2)+
	dbne d4, .CopyInject
	beq .PatchLoop

.SrcOrTrg
	move.l d0, d3
	bsr .ReadBits
	move.b (a4, d4), d3
	bsr .ReadBits
	move.l d4, d5 ; d5=length
	move.l d1, d3
	bsr .ReadBits
	move.b (a4, d4), d3
	bsr .ReadBits ; return d4=offset
	bsr .ReadBit
	bcc .Positive
	not d4
.Positive
	bsr .ReadBit
	bcs .Trg
	add d4, a1
.CopySource
	move.b (a1)+, (a2)+
	dbne d5, .CopySource
	beq .PatchLoop

.Trg
	add d4, a3
	move.b (a3)+, (a2)+
	dbne d5, .CopySource
	beq .PatchLoop
	



.ReadBit:
	rol.b #1, d2
	bne .GotBit
	move.b (a0)+, d2
	rol.b #1, d2
.GotBit
	rts

.ReadBits: 	; read n bits (in d3), return in d4
	moveq #0, d4
.Bit
	rol.b #1, d2
	bne .GotBits
	move.b (a0)+, d2
	rol.b #1, d2
.GotBits
	rol.l d4
	dbne d3, .Bit
	rts;
