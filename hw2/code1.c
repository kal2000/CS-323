// code1.c                                        Stan Eisenstat (09/12/12)
//
// Stage 1 implementation of putBits/getBits described in code.h


#include <stdio.h>
#include "code.h"
//#include "/c/cs323/Hwk2/code.h"


// Write code (#bits = nBits) to standard output
void putBits (int nBits, int code)
{
    if (code < (1 << nBits))
	printf ("%d\n", code);                  // code representable
    else {
	fprintf (stderr, "nBits = %d, maxCode = %d, code = %d\n",
			  nBits,      (1<<nBits)-1, code        );
    }
}


// Flush remaining bits to standard output
void flushBits (void)
{
}


// Return next code (#bits = nBits) from standard input (EOF on end-of-file).
// Treat #bits < nBits as end-of-file indicator so that flushBits() can be
// implemented as putBits(nBits-1,0).  Print error message and return EOF if
// #bits is not as expected.

int getBits (int nBits)
{
    int code;

    if (scanf ("%d\n", &code) < 1)              // Demand code or assume no more
	return EOF;
    else if (code < (1 << nBits))
	return code;                            // #bits as expected
    else {
	fprintf (stderr, "nBits = %d, code = %d\n",
			  nBits,      code        );
	return EOF;
    }
}
