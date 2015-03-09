//
//  8BitDiff.cpp
//
//  Created by Carl-Henrik Skårstedt on 2/15/15.
//  Copyright (c) 2015 Carl-Henrik Skårstedt. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 8BDIFF FORMAT
// -------------
// 4 bits: size of offset bit sizes
// 4 bits: size of length bit sizes
// length bit sizes
// offset bit sizes
// 2/4 bytes: number of injected bytes
//	if top bit of first byte is set then 4
// injected bytes
// instructions begin
//  loop until end of inject buffer:
//   bit: 0=inject, 1=source or target
//   if inject and at end of inject buf exit
//   length bit cnt+length bits (stored one less)
//   if source or target:
//    buffer offset bit cnt + buffer offset bits
//	  sign of offset (not instead of negate)
//	  bit: 0=source, 1=target
//  repeat loop
//
// target/source/inject source pointers start
// at the start of each buffer.
// any pointer is set to the end of the run after
// copy and the offset increments/decrements for source
// for a negative offset, not the number, don't negate.

// Some limits
#define E8_SIZE_BITS 3
#define EB_SIZE_BITS_MAX 4
#define E8_MIN_TRG_SRC_LEN 2

// Get index of top bit in value
int GetNumBits(int value)
{
	if (value==0)
		return 0;

	if (value<0)
		value = 1-value;
	
	int ret = 1;
	for (int b=16; b; b>>=1) {
		if (value >= (1<<b)) {
			ret += b;
			value >>= b;
		}
	}
	return ret;
}

// From a list of bit counts, find the lowest that can hold value
int GetBitCountIndex(int value, char *buckets, int numBuckets)
{
	if (value<0)
		value = ~value;
	for (int b=0; b<numBuckets; b++) {
		if (value<(1<<int(buckets[b])))
			return b;
	}
	return -1;
}

// Write a number of bits to a bit stream
unsigned char* PushBits(unsigned char *out, unsigned char &mask, int value, int bits)
{
	unsigned char m = mask;
	if (value<0)
		value = ~value;
	unsigned int f = 1<<(bits-1);
	unsigned char o = *out;
	for (int b=0; b<bits; b++) {
		if (value & f)
			o |= m;
		else
			o &= ~m;
		m>>=1;
		if (!m) {
			m = 0x80;
			*out++ = o;
			o = 0;
		}
		f>>=1;
	}
	*out = o;
	mask = m;
	return out;
}

// Find the best string match starting at match within buffer
// buffer_exp is how much the buffer can grow along with match
// (is of the same buffer as match)
int MatchString(const char *match, size_t match_left,
				const char *buffer, size_t buffer_size, size_t buffer_exp,
				int curr_offset, int &offs, int &size)
{
	int value = -1;
	char first = *match;
	for (size_t src_offs = 0; src_offs<buffer_size; src_offs++) {
		if (buffer[src_offs] == first) {
			size_t src_left = buffer_size + buffer_exp - src_offs;
			const char *chk = buffer + src_offs;
			const char *trg = match;
			int len = 1;
			size_t left = match_left<src_left ? match_left : src_left;
			for (size_t src_chk = left-1; src_chk; --src_chk) {
				if (*++chk!=*++trg)
					break;
				len++;
			}
			int offset = int(src_offs-curr_offset);
			if (len>E8_MIN_TRG_SRC_LEN) {
				// note: weight on offset since we probably need to swap
				// back to this point which might not have been necessary
				int instr_size = 1 + 1 + 1; // instruction src/trg + sign + src/trg
				// estimate bits per size and actual bits of offset plus half cost of returning pointer
				instr_size += E8_SIZE_BITS + 3*GetNumBits(offset)/2;
				// estimate bits per size and actual bits of length
				instr_size += E8_SIZE_BITS + GetNumBits(len-1);
				// bits accounted for minus bits needed for this instruction
				int saving = len*8 - instr_size;
				if (saving>value) {
					value = saving;
					offs = offset;
					size = len;
				}
			}
		}
	}
	return value;
}

// Encoder data
struct Encoder {
	enum EncType {
		LENGTH,
		OFFSET,
		TYPES
	};
	
	enum E8Instr {
		E8I_TRG,
		E8I_SRC,
		E8I_INJ,
		E8I_END
	};
	
	int bitCounts[TYPES][32];
	int count[TYPES];
	int instr[E8I_END];
	int inject_size;
	int bitSizesCount[TYPES]; // how many bits per size lookup
	char besti2b[TYPES][1<<EB_SIZE_BITS_MAX]; // lookup bit size

	char *instructions;
	char *inject;
	int *values;
	
	char *result;
	size_t result_size;
	
	
	Encoder() : instructions(nullptr), inject(nullptr),
				values(nullptr), inject_size(0),
				result(nullptr), result_size(0)
	{
		for (int t=0; t<TYPES; t++) {
			count[t] = 0;
			for (int b=0; b<32; b++)
				bitCounts[t][b] = 0;
		}
		for (int i=0; i<E8I_END; i++)
			instr[i] = 0;
	}
	
	~Encoder() { Reset(); }
	
	void Reset() {
		if (instructions)
			free(instructions);
		instructions = nullptr;
		if (inject)
			free(inject);
		inject = nullptr;
		if (values)
			free(values);
		values = nullptr;
		if (result)
			free(result);
		result = nullptr;
		inject_size = 0;
		result_size = 0;
	}

	void Build(const char *source, size_t source_size, const char *target, size_t target_size);
	void Optimize();
	void Generate();
};

void Encoder::Build(const char *source, size_t source_size, const char *target, size_t target_size)
{
	inject = (char*)malloc(target_size);
	values = (int*)malloc(sizeof(int) * target_size * 3 / 2);
	instructions = (char*)malloc(target_size);
	char *next_inj = inject;
	int *next_offs = values;
	char *next_instr = instructions;
	size_t inject_count = 0;
	size_t cursor = 0;
	int src_offs_prev = 0;
	int trg_offs_prev = 0;
	
	// first find patterns
	while (cursor < target_size) {
		int src_offs, trg_offs;
		int src_size, trg_size;
		int save_src = MatchString(target+cursor, target_size-cursor, source, source_size,
								   0, src_offs_prev, src_offs, src_size);
		int save_trg = MatchString(target+cursor, target_size-cursor, target, cursor,
								   target_size-cursor, trg_offs_prev, trg_offs, trg_size);
		int save = save_src > save_trg ? save_src : save_trg;
		// if no match then push byte to inject buffer
		if (save<=0 || (save<8 && inject_count)) {
			inject_count++;
			cursor++;
		} else {
			// add skipped bytes into injection table
			if (inject_count) {
				const char *read = target+cursor-inject_count;
				for (size_t i=0; i<inject_count; i++)
					*next_inj++ = *read++;
				*next_instr++ = E8I_INJ;
				instr[E8I_INJ]++;
				bitCounts[LENGTH][GetNumBits((int)inject_count)]++;
				count[LENGTH]++;
				*next_offs++ = (int)inject_count;
				inject_count = 0;
			}
			// copy bytes from source or target window
			if (save_src>save_trg) {
				*next_instr++ += E8I_SRC;
				instr[E8I_SRC]++;
				bitCounts[LENGTH][GetNumBits(src_size)]++;
				count[LENGTH]++;
				*next_offs++ = src_size;
				bitCounts[OFFSET][GetNumBits(src_offs)]++;
				count[OFFSET]++;
				*next_offs++ = src_offs;
				cursor += src_size;
				src_offs_prev += src_offs + src_size;
			} else {
				*next_instr++ += E8I_TRG;
				instr[E8I_TRG]++;
				bitCounts[LENGTH][GetNumBits(trg_size)]++;
				count[LENGTH]++;
				*next_offs++ = trg_size;
				bitCounts[OFFSET][GetNumBits(trg_offs)]++;
				count[OFFSET]++;
				*next_offs++ = trg_offs;
				cursor += trg_size;
				trg_offs_prev += trg_offs + trg_size;
			}
		}
	}
	// add trailing injection bytes
	if (inject_count) {
		const char *read = target+cursor-inject_count;
		for (size_t i=0; i<inject_count; i++)
			*next_inj++ = *read++;
		*next_instr++ = E8I_INJ;
		instr[E8I_INJ]++;
		bitCounts[LENGTH][GetNumBits((int)inject_count)]++;
		count[LENGTH]++;
		*next_offs++ = (int)inject_count;
	}
	inject_size = (int)(next_inj - inject);
}

void Encoder::Optimize()
{
	// check stats
	int total[TYPES];
	int top[TYPES];
	
	for (int i=0; i<TYPES; i++) {
		int totes = 0;
		int tops = 0;
		for (int b = 0; b<32; b++) {
			totes += bitCounts[i][b];
			if (bitCounts[i][b])
				tops = b;
		}
		total[i] = totes;
		top[i] = tops;
	}
	
	// find an optimal distribution of bit buckets to represent the sizes
	// 1) bounded by 0 and top
	for (int i=0; i<TYPES; i++) {
		int minCost = 1<<30;
		bitSizesCount[i] = 0;
		// number of bits to represent the size (0 = constant)
		for (int b=0; b<=EB_SIZE_BITS_MAX; b++) {
			char i2b[1<<EB_SIZE_BITS_MAX];
			int last = (1<<b)-1;
			for (int j=0; j<last; j++)
				i2b[j] = j+1; // min valid amount of bits = 1
			i2b[last] = top[i];
			
			bool shuffled = false;
			do {
				// calculate size at current setup
				int s = 0;
				int bits = 0;
				for (int n=0; n<=top[i]; n++) {
					if (n>i2b[s])
						s++;
					bits += bitCounts[i][n] * (i2b[s] + b);
				}
				if (bits < minCost) {
					minCost = bits;
					bitSizesCount[i] = b;
					for (int c=0; c<(1<<3); c++)
						besti2b[i][c] = i2b[c];
				}
				shuffled = false;
				// now attempt to shuffle..
				for (int bi=last-1; bi>=0; --bi) {
					int v = i2b[bi];
					if ((i2b[bi+1]-v)>1) {
						v++;
						i2b[bi] = v;
						shuffled = true;
						for (int b2=bi+1; b2<last; b2++) {
							++v;
							i2b[b2] = v;
						}
						break;
					}
				}
			} while (shuffled);
		}
	}
}

// Build a binary diff buffer
void Encoder::Generate()
{
	int num_instr = instr[E8I_INJ] + instr[E8I_SRC] + instr[E8I_TRG];
	// figure out size of diff
	size_t diff_size = 1;	// 1 byte for bit counts of offs/len tables
	diff_size += (1<<bitSizesCount[LENGTH]) + (1<<bitSizesCount[OFFSET]);
	diff_size += inject_size<(1<<15) ? 2 : 4;
	diff_size += inject_size;
	size_t instruction_bits = 0;
	// go through the instructions and add up the bits

	int *val = values;
	for (int i=0; i<num_instr; i++) {
		instruction_bits += 1; // instructions use at least 1 bit
		char instr = instructions[i];
		// add offset, injection buffer doesn't use offset
		// add length
		int lenIndex = GetBitCountIndex(*val++, besti2b[LENGTH], 1<<bitSizesCount[LENGTH]);
		instruction_bits += bitSizesCount[LENGTH]; // offset bit length
		instruction_bits += besti2b[LENGTH][lenIndex];
		if (instr!=E8I_INJ) { // inject instruction doesn't have an offset
			instruction_bits += bitSizesCount[OFFSET]; // offset bit length
			instruction_bits += besti2b[OFFSET][GetBitCountIndex(*val++,
								besti2b[OFFSET], 1<<bitSizesCount[OFFSET])];
			instruction_bits += 1; // offset buffer requires 1 sign bit
			instruction_bits += 1; // source and target buffers use 1 extra instruction bit
		}
	}
	instruction_bits += 1; // the diff is terminated by an injection that goes beyond the end
	
	diff_size += (instruction_bits+7)/8;
	result_size = diff_size;
	result = (char*)malloc(diff_size);
	
	unsigned char *o = (unsigned char*)result;
	// write # bits per category
	*o++ = (bitSizesCount[OFFSET]<<4) | bitSizesCount[LENGTH];

	// write # bits per bucket
	for (int siz=0; siz<TYPES; siz++) {
		for (int b=0; b<(1<<bitSizesCount[siz]); b++)
			*o++ = besti2b[siz][b];
	}

	// write inject buffer size
	if (inject_size>=0x8000) {
		*o++ = 0x80 | (unsigned char)(inject_size>>24);
		*o++ = (unsigned char)(inject_size>>16);
	}
	*o++ = (unsigned char)(inject_size>>8);
	*o++ = (unsigned char)(inject_size);
	
	// write inject buffer
	for (int i=0; i<inject_size; i++)
		*o++ = inject[i];
	
	// write instructions
	val = values;
	unsigned char mask = 0x80;
	for (int i=0; i<num_instr; i++) {
		char instr = instructions[i];
		// insert first bit of instruction (0=inject, 1=source or target copy)
		o = PushBits(o, mask, instr!=E8I_INJ, 1);
		// add length
		int length = *val++;
		int lenIndex = GetBitCountIndex(length, besti2b[LENGTH], 1<<bitSizesCount[LENGTH]);
		o = PushBits(o, mask, lenIndex, bitSizesCount[LENGTH]);
		o = PushBits(o, mask, length, besti2b[LENGTH][lenIndex]);
		// add offset, injection buffer doesn't use offset
		if (instr!=E8I_INJ) {
			int offset = *val++;
			int offIndex = GetBitCountIndex(offset, besti2b[OFFSET], 1<<bitSizesCount[OFFSET]);
			o = PushBits(o, mask, offIndex, bitSizesCount[OFFSET]);
			o = PushBits(o, mask, offset, besti2b[OFFSET][offIndex]);
			o = PushBits(o, mask, offset<0, 1);
			o = PushBits(o, mask, instr==E8I_TRG, 1);
		}
	}
	o = PushBits(o, mask, 0, 1); // terminate the file!
	if (mask!=0x80)
		o++;
}

// Read a number of bits from the bit stream into a value
int DecodeBits(const unsigned char **read, unsigned char &mask, int bits)
{
	const unsigned char *r = *read;
	char c = *r;
	unsigned char m = mask;
	int value = 0;
	for (int b=0; b<bits; b++) {
		value <<= 1;
		if (c&m)
			value |= 1;
		m >>= 1;
		if (!m) {
			m = 0x80;
			c = *++r;
		}
	}
	mask = m;
	*read = r;
	return value;
}

// Read a single bit from a bit stream and return it
int DecodeBit(const unsigned char **read, unsigned char &mask)
{
	const unsigned char *r = *read;
	char c = *r;
	unsigned char m = mask;

	int ret = (c&m) ? 1 : 0;
	m>>=1;
	if (!m) {
		mask = 0x80;
		*read = r+1;
		return ret;
	}
	mask = m;
	return ret;
}

// Decode a bit stream
size_t Decode(char *out, const char *source, const char *diff)
{
	int bitSizeCnt[2];
	const unsigned char *bitSize[2];
	const char *buf[3];
	const char *end;
	const char *start = out;
	const unsigned char *du = (const unsigned char*)diff;
	
	bitSizeCnt[0] = *du & 0xf;
	bitSizeCnt[1] = (*du++>>4) &0xf;
	bitSize[0] = du;
	du += 1<<bitSizeCnt[0];
	bitSize[1] = du;
	du += 1<<bitSizeCnt[1];
	unsigned int inject_size = 0;
	if (*du & 0x80) {
		inject_size = ((int(du[0]&0x7f)<<8) | int(du[1]))<<16;
		du += 2;
	}
	inject_size |= ((unsigned char)(du[0])<<8) | (unsigned char)du[1];
	du += 2;
	buf[0] = (const char*)du;
	buf[1] = source;
	buf[2] = out;
	du += inject_size;
	end = (const char*)du;
	unsigned char mask = 0x80;
	for (;;) {
		int buffer = DecodeBit(&du, mask);
		if (!buffer && buf[0]>=end)
			break;
		int lbits = DecodeBits(&du, mask, bitSizeCnt[0]);
		int length = DecodeBits(&du, mask, bitSize[0][lbits]);
		if (buffer) {
			int obits = DecodeBits(&du, mask, bitSizeCnt[1]);
			int offset = DecodeBits(&du, mask, bitSize[1][obits]);
			if (DecodeBit(&du, mask))
				offset = ~offset;
			if (DecodeBit(&du, mask))
				buffer = 2;
			buf[buffer] += offset;
		}
		const char *read = buf[buffer];
		for (int move=length; move; --move)
			*out++ = *read++;
		buf[buffer] = read;
	}
	return out-start;
}

// Get size of a bit stream without the source
size_t GetLength(const char *diff, size_t diff_size)
{
	int bitSizeCnt[2];
	const unsigned char *bitSize[2];
	const unsigned char *inject;
	const unsigned char *end;
	const unsigned char *du = (const unsigned char*)diff;
	
	if (diff_size<6)
		return 0;
	
	bitSizeCnt[0] = *du & 0xf;
	bitSizeCnt[1] = (*du++>>4) & 0xf;
	bitSize[0] = du;
	du += 1<<bitSizeCnt[0];
	bitSize[1] = du;
	du += 1<<bitSizeCnt[1];
	unsigned int inject_size = 0;
	if (*du & 0x80) {
		inject_size = ((int(du[0]&0x7f)<<8) | int(du[1]))<<16;
		du += 2;
	}
	inject_size |= ((unsigned char)(du[0])<<8) | (unsigned char)du[1];
	du += 2;
	inject = du;
	du += inject_size;
	end = du;
	
	if ((du-(unsigned char*)diff) >= diff_size)
		return 0;
	
	size_t target_size = 0;
	
	unsigned char mask = 0x80;
	//	int num = 0;
	for (;;) {
		int buffer = DecodeBit(&du, mask);
		if (!buffer && inject>=end)
			break;
		int len = DecodeBits(&du, mask, bitSize[0][DecodeBits(&du, mask, bitSizeCnt[0])]);
		if (buffer)
			DecodeBits(&du, mask, bitSize[1][DecodeBits(&du, mask, bitSizeCnt[1])]+2);
		else
			inject += len;
		target_size += len;
	}
	return target_size;
}

// Names of buffers for creating a csv report
const char *aBufferNames[] = {
	"Inject",
	"Source",
	"Target"
};

// Create a spreadsheet of instructions from a bit stream
bool GetStats(const char *filename, const char *source, size_t source_size, const char *diff, size_t diff_size)
{
	size_t out_size = GetLength(diff, diff_size);
	if (!out_size)
		return false;

	if (FILE *f = fopen(filename, "w")) {
		int bitSizeCnt[2];
		char *start = (char*)malloc(diff_size);
		char *out = start;
		const unsigned char *bitSize[2];
		const char *buf[3], *orig[3];
		const char *end;
		const unsigned char *du = (const unsigned char*)diff;
		
		fprintf(f, "name,target,offset,length,data\n");
		bitSizeCnt[0] = *du & 0xf;
		bitSizeCnt[1] = (*du++>>4) &0xf;
		bitSize[0] = du;
		du += 1<<bitSizeCnt[0];
		bitSize[1] = du;
		du += 1<<bitSizeCnt[1];
		unsigned int inject_size = 0;
		if (*du & 0x80) {
			inject_size = ((int(du[0]&0x7f)<<8) | int(du[1]))<<16;
			du += 2;
		}
		inject_size |= ((unsigned char)(du[0])<<8) | (unsigned char)du[1];
		du += 2;
		orig[0] = buf[0] = (const char*)du;
		orig[1] = buf[1] = source;
		orig[2] = buf[2] = out;
		du += inject_size;
		end = (const char*)du;
		unsigned char mask = 0x80;
		for (;;) {
			int buffer = DecodeBit(&du, mask);
			if (!buffer && buf[0]>=end)
				break;
			int lbits =DecodeBits(&du, mask, bitSizeCnt[0]);
			int length = DecodeBits(&du, mask, bitSize[0][lbits]);
			int offs = -1;
			if (buffer) {
				int obits = DecodeBits(&du, mask, bitSizeCnt[1]);
				offs = DecodeBits(&du, mask, bitSize[1][obits]);
				if (DecodeBit(&du, mask))
					offs = ~offs;
				if (DecodeBit(&du, mask))
					buffer = 2;
				buf[buffer] += offs;
			}
			const char *data = out, *bufptr = buf[buffer];
			if (source) {
				if (buffer==1 && (buf[1]+length-source)>source_size) {
					printf("Source file is not valid (not large enough)\n");
					source = nullptr;
				} else {
					const char *read = buf[buffer];
					for (int move=length; move; --move)
						*out++ = *read++;
					buf[buffer] = read;
				}
			} else {
				out += length;
				buf[buffer] += length;
			}
			char info[17], bufOffs[17];
			if (source) {
				int il = length<16 ? length : 16;
				for (int i=0; i<il; i++)
					info[i] = data[i]<=' ' ? '.' : data[i];
				info[il] = 0;
			} else
				info[0] = 0;
			if (buffer)
				snprintf(bufOffs, sizeof(bufOffs), "0x%x", (int)(bufptr-orig[buffer]));
			else
				bufOffs[0] = 0;
			fprintf(f, "%s,0x%x,%s,0x%x,\"%s\"\n", aBufferNames[buffer], (int)(out-start), bufOffs, length, info);
		}
		// clean up
		free(start);
		fclose(f);
		return true;
	}
	return false;
}


// Read a binary file
const char* LoadFile(const char *name, size_t &size)
{
	if (FILE *f = fopen(name, "rb")) {
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (void *data = malloc(size)) {
			fread(data, size, 1, f);
			fclose(f);
			return (const char*)data;
		}
		fclose(f);
	}
	return nullptr;
		
}

// command line options
const char *aCmdLineOpt[] = {
	"encode",
	"decode",
	"stats",
	nullptr
};

enum CMD_OPT {
	CMD_ENCODE,
	CMD_DECODE,
	CMD_STATS,
	
	CMD_NUM
};

// Command line references

enum CMD_REF {
	REF_SOURCE,
	REF_TARGET,
	REF_DIFF,
	REF_STATS,
	
	REF_COUNT
};

// If string is a file, return extension
const char *GetExt(const char *str)
{
	if (str) {
		const char *end = str + strlen(str);
		while (end>str && *end!='.') --end;
		if (*end=='.')
			return end;
	}
	return "";
}

// Entrypoint
int main(int argc, const char * argv[]) {
	const char *aFiles[REF_COUNT] = { nullptr };
	
	CMD_OPT cmd = CMD_NUM;
	for (int i=1; i<argc; i++) {
		const char *arg = argv[i];
		if (*arg=='-') {
			for (int c=0; c<CMD_NUM && cmd==CMD_NUM; c++) {
				if (strcasecmp(aCmdLineOpt[c], arg+1)==0)
					cmd = (CMD_OPT)c;
			}
		} else {
			const char *ext = GetExt(arg);
			if (strcasecmp(ext, ".csv")==0)
				aFiles[REF_STATS] = arg;
			else if (strcasecmp(ext, ".8bd")==0)
				aFiles[REF_DIFF] = arg;
			else if (!aFiles[REF_SOURCE])
				aFiles[REF_SOURCE] = arg;
			else if (!aFiles[REF_TARGET])
				aFiles[REF_TARGET] = arg;
			else if (!aFiles[REF_DIFF])
				aFiles[REF_DIFF] = arg;
		}
	}
	
	if (cmd==CMD_NUM ||
		(cmd==CMD_ENCODE && !aFiles[REF_TARGET]) ||
		(cmd==CMD_DECODE && (!aFiles[REF_SOURCE] || !aFiles[REF_DIFF])) ||
		(cmd==CMD_STATS && !aFiles[REF_DIFF])) {
		printf("Create a binary patch in a format sensible for 8 bit decoding\n"
			   "Usage: (arguments in brackets are optional)\n"
			   "%s -%s <source> <target> [<result.8bd>] [<stats.csv>]\n"
			   "%s -%s <source> <target> <result.8bd>\n"
			   "%s -%s [<source>] <result.8bd> <stats.csv>\n",
			   argv[0], aCmdLineOpt[CMD_ENCODE],
			   argv[0], aCmdLineOpt[CMD_DECODE],
			   argv[0], aCmdLineOpt[CMD_STATS]);
		return 0;
	}

	size_t source_size = 0;
	const char *source = aFiles[REF_SOURCE] ? LoadFile(aFiles[REF_SOURCE], source_size) : nullptr;
	if (!source && aFiles[REF_SOURCE]) {
		printf("Could not open \"%s\"\n", aFiles[0]);
		return 1;
	}

	size_t target_size = 0;
	const char *target = (cmd!=CMD_DECODE && cmd!=CMD_STATS && aFiles[REF_TARGET]) ?
							LoadFile(aFiles[REF_TARGET], target_size) : nullptr;
	if (!target && (cmd!=CMD_STATS && cmd!=CMD_DECODE && aFiles[REF_TARGET])) {
		free((void*)source);
		printf("Could not open \"%s\"\n", aFiles[1]);
		return 1;
	}

	if (cmd==CMD_ENCODE) {
		Encoder encode;
		encode.Build(source, source_size, target, target_size);
		encode.Optimize();
		encode.Generate();
		
		// check result!
		char *buf = (char*)malloc(target_size);
		size_t decode_size = Decode(buf, source, encode.result);
		
		int compare = memcmp(target, buf, target_size);
		if (compare) {
			printf("You have encountered a bug in the program.\n"
				   "memcmp(target, decode, target_size) = %d (%d / %d)\n",
				   compare, (int)target_size, (int)decode_size);
			for (size_t o=0; o<target_size; o++) {
				unsigned char t = (unsigned char)target[o];
				unsigned char b = (unsigned char)buf[o];
				if (t!=b)
					printf("Byte 0x%x differs (0x%02x != 0x%02x)\n", (int)o, (unsigned char)target[o], (unsigned char)buf[o]);
			}
		} else if (aFiles[REF_DIFF]) {
			if (FILE *f = fopen(aFiles[REF_DIFF], "wb")) {
				fwrite(encode.result, encode.result_size, 1, f);
				fclose(f);
			}
		}
		if (aFiles[REF_STATS]) {
			if (!GetStats(aFiles[REF_STATS], source, source_size, encode.result, encode.result_size))
				printf("Could not generate stats from diff\n");
		}
		free(buf);
		encode.Reset();
	} else if (cmd==CMD_DECODE) {
		size_t diff_size = 0;
		if (const char *diff = LoadFile(aFiles[REF_DIFF], diff_size)) {
			if (size_t target_size = GetLength(diff, diff_size)) {
				target = (const char*)malloc(target_size);
				Decode((char*)target, source, diff);
				if (aFiles[REF_TARGET]) {
					if (FILE *f = fopen(aFiles[REF_TARGET], "wb")) {
						fwrite(target, target_size, 1, f);
						fclose(f);
					}
				}
				free((void*)diff);
			} else
				printf("Could not decode diff file %s\n", aFiles[REF_DIFF]);
		} else
			printf("Could not open diff file %s\n", aFiles[REF_DIFF]);
	} else if (cmd==CMD_STATS) {
		size_t diff_size = 0;
		if (const char *diff = LoadFile(aFiles[REF_DIFF], diff_size)) {
			if (!GetStats(aFiles[REF_STATS], source, source_size, diff, diff_size))
				printf("Could not generate stats from diff\n");
			free((void*)diff);
		}
	}
	
	if (source)
		free((void*)source);
	
	if (target)
		free((void*)target);
	
	
    return 0;
}
