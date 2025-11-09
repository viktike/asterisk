/*
**
** File:        "lsp2.h"
**
** Description:     Function prototypes for "lsp2.c"
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

void   AtoLsp(FLOAT *LspVect, FLOAT *Lpc, FLOAT *PrevLsp);
Word32 Lsp_Qnt(FLOAT *CurrLsp, FLOAT *PrevLsp);
Word32 Lsp_Svq(FLOAT *Lsp, FLOAT *Wvect);
void   Lsp_Inq(FLOAT *Lsp, FLOAT *PrevLsp, Word32 LspId, Word16 Crc);
void   Lsp_Int(FLOAT *QntLpc, FLOAT *CurrLsp, FLOAT *PrevLsp);
void   LsptoA(FLOAT *Lsp);
