/*
**
** File:        "util2.h"
**
** Description:     Function prototypes for "util2.c"
**
*/

/*
	ITU-T G.723.1 Software Package Release 2 (June 2006)

    ITU-T G.723.1 Floating Point Speech Coder ANSI C Source Code.  Version 5.1F

    Original fixed-point code copyright (c) 1995,
    AudioCodes, DSP Group, France Telecom, Universite de Sherbrooke.
    All rights reserved.

    Floating-point code copyright (c) 1995,
    Intel Corporation and France Telecom (CNET).
    All rights reserved.
*/

void     Read_lbc(FLOAT *Dpnt, int Len, FILE *Fp);
void     Write_lbc(FLOAT *Dpnt, int Len, FILE *Fp);
void     Rem_Dc(FLOAT *Dpnt);
void     Mem_Shift(FLOAT *PrevDat, FLOAT *DataBuff);
Word16   Rand_lbc(Word16 *p);
void     Line_Pack(LINEDEF *Line, char *Vout, Word16 Ftyp);
LINEDEF  Line_Unpk(char *Vinp, Word16 *Ftyp, Word16 Crc);
Word16*  Par2Ser(Word32 Inp, Word16 *Pnt, int BitNum);
Word32   Ser2Par(Word16 **Pnt, int Count);
void     Scale(FLOAT *Tv,FLOAT Sen);
FLOAT    DotProd(FLOAT *in1, FLOAT *in2, int len);

void     Line_Wr(char *, FILE *);
int      Line_Rd(char *, FILE *);
