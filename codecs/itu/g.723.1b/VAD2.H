/*
**
** File:        "vad2.h"
**
** Description:     Function prototypes for "vad2.c"
**
*/

/*
	ITU-T G.723.1 Software Package Release 2 (June 2006)

    ITU-T G.723.1 Floating Point Speech Coder ANSI C Source Code.  Version 5.1F
    copyright (c) 1995, AudioCodes, DSP Group, France Telecom,
    Universite de Sherbrooke.  All rights reserved.

    Floating-point code copyright (c) 1996,
    Intel Corporation and France Telecom (CNET).
    All rights reserved.
*/

void    Init_Vad(void);
Flag    Comp_Vad(FLOAT *Dpnt);

extern  VADSTATDEF  VadStat;
