;
; Z80 Decoder for 8BitDiff
;
;	Copyright 2015 Carl-Henrik Skårstedt. All rights reserved.
;			https://github.com/sakrac/8BitDiff/
;
; Important note:
;	Although this looks like valid Z80 assembler
;	I have not actually tested it. It is also my
;	first original Z80 programming, I have just
;	modified / debugged it on Sega Genesis and
;	in the Shark emulator ages ago. I even made a
;	full Z80 console debugger in Shark :)
;
;	Basically I am posting this so that someone
;	can help me figure out the following:
;	1) How to code Z80 assembler (better)
;	2) How to test Z80 code (ideally in an emulator with
;		a built-in monitor or proper debugger)
;
;	Having said that I'd be perfectly happy if someone
;	could simply correct this code and test it and let me know
;	where to get a fixed version.
;
;	8BDIFF FORMAT
;	-------------
;	4 bits: size of offset bit sizes
;	4 bits: size of length bit sizes
;	1 byte length bit sizes
;	1 byte offset bit sizes
;	2 bytes size of injected bytes (8 bit specific)
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
;	BC = source
;	HL = diff
;	DE = dest
;	IX = 14 bytes of work mem
;
; RETURN:
;	DE = end of patched data
;

Patch_8BDiff:
	ld (IX+2), B	; store away source read pointer
	ld (IX+3), C
	ld (IX+4), D	; store away target read pointer
	ld (IX+5), E
	ld A, 15
	and A, (HL)
	ld (IX+6), A	; lower 4 bits are number bits in lengths
	ld A, (HL)
	sra A
	sra A
	sra A
	sra A
	ld (IX+7), A	; upper 4 bits are number bits in offsets
	inc HL
	ld (IX+8), H	; length of bit lengths
	ld (IX+9), L
	ld A, (IX+6)
	call P8BD_Shift
	adc HL, BC
	ld (IX+10), H	; length of bit offsets
	ld (IX+11), L
	ld A, (IX+7)
	call P8BD_Shift
	adc HL, BC
	ld B, (HL)		; number of inject bytes
	inc HL
	ld C, (HL)		; number of inject bytes
	inc HL
	ld (IX), H		; inject buffer
	ld (IX+1), L
	scf
	ccf				; clear carry = set and invert carry flag
	adc HL, BC
	ld (IX+12), H	; end of inject buffer
	ld (IX+13), L

	ld A, 128		; last bit
P8BP_PatchLoop:
	sla A
	jr NZ, P8BP_GotBit1
	ld A, (HL)
	inc HL
	scf				; set carry flag to set empty bit
	rr A
P8BP_GotBit1:
	jr C, P8BP_SrcOrTrg
P8BP_Inject:
	push AF			; need to use accumulator for finish test
	ld A, (IX)		; if end of inject buffer then we're done
	cp (IX+12)
	jr C, P8BP_More
	ld A, (IX+1)
	cp (IX+13)
	jr C, P8BP_More
	pop AF
	ret				; finished with patching
P8BP_More:

	push DE			; using D for temp register
	ld D, (IX+6)	; get length descriptor bits
	call P8BD_ReadBits
	push HL
	ld H, (IX+8)
	ld L, (IX+9)
	add HL, BC
	ld D, (HL)		; get number of length bits
	pop HL
	call P8BD_ReadBits
	pop DE
	push HL
	ld H, (IX)		; read from inject buffer
	ld L, (IX+1)
	ldir
	ld (IX), H		; store current position of inject buffer
	ld (IX+1), L
	pop HL
	jr P8BP_PatchLoop

P8BP_SrcOrTrg:

	push DE			; using D for temp register
	ld D, (IX+6)	; get length descriptor bits
	call P8BD_ReadBits
	push HL
	ld H, (IX+8)
	ld L, (IX+9)
	add HL, BC
	ld D, (HL)		; get number of length bits
	pop HL
	call P8BD_ReadBits
	push BC			; push length

	ld D, (IX+7)	; get offset descriptor bits
	call P8BD_ReadBits
	push HL
	ld H, (IX+8)
	ld L, (IX+9)
	add HL, BC
	ld D, (HL)		; get number of offset bits
	pop HL
	call P8BD_ReadBits

	sla A			; read one bit for negative offset
	jr NZ, P8BP_GotBit3
	ld A, (HL)
	inc HL
	scf				; set carry flag to set empty bit
	rr A
P8BP_GotBit3:
	jr NC, P8BP_PosOffs
	push AF			; xor ffff to get negative address
	ld A, 255
	xor B
	ld B, A
	ld A, 255
	xor C
	ld C, A
	pop AF
P8BP_PosOffs:

	sla A			; read one bit for 0: Source or 1: Target copy
	jr NZ, P8BP_GotBit4
	ld A, (HL)
	inc HL
	scf				; set carry flag to set empty bit
	rr A
P8BP_GotBit4:
	jr C, P8BP_Target

	pop IY			; copy from Source pointer
	pop DE
	push HL
	push IY
	ld H, (IX+2)
	ld L, (IX+3)
	add HL, BC
	pop BC
	ldir
	ld (IX+2), H
	ld (IX+3), L
	jp P8BP_PatchLoop

P8BP_Target:
	pop IY			; copy from Target pointer
	pop DE
	push HL
	push IY
	ld H, (IX+4)
	ld L, (IX+5)
	add HL, BC
	pop BC
	ldir
	ld (IX+4), H
	ld (IX+5), L
	jp P8BP_PatchLoop


	; bit count in D
	; remainder bits in A
	; bit byte stream in HL
	; return bits in BC
P8BD_ReadBits:
	ld C, 0
	ld B, C
P8BD_ReadBitLoop:
	sla A
	jr NZ, P8BP_GotBit2
	ld A, (HL)
	inc HL
	scf				; set carry flag to set empty bit
	rr A
P8BP_GotBit2:
	rr C
	rr B
	dec D
	jp NZ, P8BD_ReadBitLoop
	ret

	; BC = 1<<A
P8BD_Shift:
	ld C,0
	ld B,C
	scf				; set carry for first bit to be shifted in
P8BD_ShiftLoop:
	rr C
	dec A
	jp NZ,P8BD_ShiftLoop
	ret




