/*
**
** File:        "decod2.h"
**
** Description:     Function prototypes and external declarations
**          for "decod2.c"
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

void    Init_Decod(void);
Flag    Decod(FLOAT *DataBuff, char *Vinp, Word16 Crc);
extern  DECSTATDEF DecStat;
