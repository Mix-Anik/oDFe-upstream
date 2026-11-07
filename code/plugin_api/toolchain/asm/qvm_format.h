/* The header of a .qvm file (the same as vmHeader_t of qfiles.h in the engine), so that q3asm does not need the headers of
   the engine. */
#ifndef QVM_FORMAT_H
#define QVM_FORMAT_H

#define	VM_MAGIC		0x12721444
#define	VM_MAGIC_VER2	0x12721445

typedef struct {
	int		vmMagic;

	int		instructionCount;

	int		codeOffset;
	int		codeLength;

	int		dataOffset;
	int		dataLength;
	int		litLength;			// ( dataLength - litLength ) should be byteswapped on load
	int		bssLength;			// zero filled memory appended to datalength

	//!!! below here is VM_MAGIC_VER2 !!!
	int		jtrgLength;			// number of jump table targets
} vmHeader_t;

#endif
