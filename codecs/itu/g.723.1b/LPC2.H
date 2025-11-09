/*
**
** File:        "lpc2.h"
**
** Description:     Function prototypes for "lpc2.c"
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

void   Comp_Lpc(FLOAT *UnqLpc, FLOAT *PrevDat, FLOAT *DataBuff);
FLOAT  Durbin(FLOAT *Lpc, FLOAT *Corr, FLOAT Err, FLOAT *Pk2);
void   Wght_Lpc(FLOAT *PerLpc, FLOAT *UnqLpc);
void   Error_Wght(FLOAT *Dpnt, FLOAT *PerLpc);
void   Comp_Ir(FLOAT *ImpResp, FLOAT *QntLpc, FLOAT *PerLpc, PWDEF Pw);
void   Sub_Ring(FLOAT *Dpnt, FLOAT *QntLpc, FLOAT *PerLpc, FLOAT *PrevErr, PWDEF Pw);
void   Upd_Ring(FLOAT *Dpnt, FLOAT *QntLpc, FLOAT *PerLpc, FLOAT *PrevErr);
void   Synt(FLOAT *Dpnt, FLOAT *Lpc);
FLOAT  Spf(FLOAT *Tv, FLOAT *Lpc);

