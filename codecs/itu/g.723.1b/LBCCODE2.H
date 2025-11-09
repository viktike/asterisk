/*
**
** File:        "lbccode2.h"
**
** Description:     Function prototypes and external declarations
**          for "lbccode2.c"
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

long   Process_Files(FILE **Ifp, FILE **Ofp, FILE **Fep, FILE **Ratp,
                     int Argc, char *Argv[]);
extern Flag  UseHp;
extern Flag  UsePf;
extern Flag  UseVx;
extern enum  Wmode    WrkMode;
extern enum  Crate    WrkRate;
