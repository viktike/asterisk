/*
**
** File:        "tab2.h"
**
** Description:  This file contains extern declarations tha tables used by
**              the SG15 LBC Coder for 6.3/5.3 kbps.
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

extern FLOAT   HammingWindowTable[LpcFrame];
extern FLOAT   BinomialWindowTable[LpcOrder];
extern FLOAT   BandExpTable[LpcOrder];
extern FLOAT   CosineTable[CosineTableSize];
extern FLOAT   LspDcTable[LpcOrder];
extern int     BandInfoTable[LspQntBands][2];
extern FLOAT   Band0Tb8[LspCbSize*3];
extern FLOAT   Band1Tb8[LspCbSize*3];
extern FLOAT   Band2Tb8[LspCbSize*4];
extern FLOAT  *BandQntTable[LspQntBands];
extern FLOAT   PerFiltZeroTable[LpcOrder];
extern FLOAT   PerFiltPoleTable[LpcOrder];
extern FLOAT   PostFiltZeroTable[LpcOrder];
extern FLOAT   PostFiltPoleTable[LpcOrder];
extern int     Nb_puls[4];
extern FLOAT   FcbkGainTable[NumOfGainLev];
extern Word32  MaxPosTable[4];
extern Word32  CombinatorialTable[MaxPulseNum][SubFrLen/Sgrid];
extern FLOAT   AcbkGainTable085[85*20];
extern FLOAT   AcbkGainTable170[170*20];
extern FLOAT   *AcbkGainTablePtr[2];
extern FLOAT   LpfConstTable[2];
extern int     epsi170[170];
extern FLOAT   gain170[170];
extern FLOAT   tabgain170[170];
extern FLOAT   tabgain85[85];
extern FLOAT   fact[4];
extern FLOAT   bseg[3];
extern FLOAT   base[3];
