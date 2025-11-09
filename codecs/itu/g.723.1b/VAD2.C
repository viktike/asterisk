
/*
**
** File:        "vad2.c"
**
** Description:     Voice Activity Detection
**
** Functions:       Init_Vad()
**                  Vad()
**
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "typedef2.h"
#include "cst2.h"
#include "tab2.h"
#include "lsp2.h"
#include "vad2.h"
#include "coder2.h"
#include "lbccode2.h"

VADSTATDEF  VadStat ;

void    Init_Vad(void)
{
    int i ;

    VadStat.Hcnt = 3 ;
    VadStat.Vcnt = 0 ;
    VadStat.Penr = (FLOAT) 1024.0;
    VadStat.Nlev = (FLOAT) 1024.0;

    VadStat.Aen = 0;

    VadStat.Polp[0] = 1;
    VadStat.Polp[1] = 1;
    VadStat.Polp[2] = SubFrLen;
    VadStat.Polp[3] = SubFrLen;

    for (i=0; i < LpcOrder; i++)
        VadStat.NLpc[i] = (FLOAT)0.0;

}

Flag Comp_Vad(FLOAT *Dpnt)
{
    int     i, j, bexp;

    FLOAT   Acc0, Frac, Thresh, Enr;
    FLOAT   Temp;
    Word32  Tm1, Tm2;
    Word16  Minp;

    Flag    VadState = 1 ;

    static  FLOAT  ScfTab[11] = {
        (FLOAT)  9170.0,
        (FLOAT)  9170.0,
        (FLOAT)  9170.0,
        (FLOAT)  9170.0,
        (FLOAT) 10289.0,
        (FLOAT) 11544.0,
        (FLOAT) 12953.0,
        (FLOAT) 14533.0,
        (FLOAT) 16306.0,
        (FLOAT) 18296.0,
        (FLOAT) 20529.0
    };

    if ( !UseVx )
        return VadState ;

    /* Find Minimum pitch period */
    Minp = (Word16) PitchMax ;
    for ( i = 0 ; i < 4 ; i ++ ) {
        if ( Minp > VadStat.Polp[i] )
            Minp = VadStat.Polp[i] ;
    }

    /* Check that all are multiplies of the minimum */
    Tm2 = 0 ;
    for ( i = 0 ; i < 4 ; i ++ ) {
        Tm1 = Minp ;
        for ( j = 0 ; j < 8 ; j ++ ) {
            if (abs((int)(Tm1 - VadStat.Polp[i])) <= 3)
                Tm2++ ;
            Tm1 += Minp;
        }
    }

    /* Update adaptation enable counter if not periodic and not sine */
    if ( (Tm2 == 4) || (CodStat.SinDet < 0) )
        VadStat.Aen += 2 ;
    else
        VadStat.Aen -- ;

    /* Clip it */
    if ( VadStat.Aen > 6 )
        VadStat.Aen = 6 ;
    if ( VadStat.Aen < 0 )
        VadStat.Aen = 0 ;

    /* Inverse filter the data */
    Enr = (FLOAT) 0.0;
    for ( i = SubFrLen ; i < Frame ; i ++ ) {
        Acc0 = Dpnt[i];
        for ( j = 0 ; j < LpcOrder ; j ++ )
            Acc0 -= Dpnt[i-j-1] * VadStat.NLpc[j];
        Enr += Acc0 * Acc0;
    }

    /* Scale the rezidual energy.
     * Multiplication by 0.5 yields a value approximately equal
     * to the corresponding computation in the fixed point code.
     * Thus the same bounds, computations and thresholds as in the
     * fixed point code can be used below.
     */
    Enr = (FLOAT)0.5 * (Enr / (FLOAT)180.0);

    /* Clip noise level in any case */
    if ( VadStat.Nlev > VadStat.Penr ) {
        VadStat.Nlev = (FLOAT)0.25 * VadStat.Nlev + (FLOAT)0.75 * VadStat.Penr;
    }

    /* Update the noise level, if adaptation is enabled */
    if ( !VadStat.Aen ) {
        VadStat.Nlev *= (FLOAT)33.0 / (FLOAT)32.0;
    }
    /* Decay Nlev by small amount */
    else {
        VadStat.Nlev *= (FLOAT)2047.0 / (FLOAT)2048.0;
    }

    /* Update previous energy */
    VadStat.Penr = Enr;

    /* CLip Noise Level */
    if (VadStat.Nlev < (FLOAT) 128.0)
        VadStat.Nlev = (FLOAT) 128.0;
    if (VadStat.Nlev > (FLOAT) 131071.0)
        VadStat.Nlev = (FLOAT) 131071.0;

    /* Compute the threshold */
    Frac = (FLOAT) frexp(VadStat.Nlev, &bexp);
    /* VadStat.Nlev = Frac x 2**bexp, where Frac = 0.1bbbbbb...., binary.
     * Isolate the 6 fractional bits 'b', Temp = 0.bbbbbb.
     */
    Temp = ((FLOAT)floor(Frac * (FLOAT)128.0) / (FLOAT)64.0) - (FLOAT)1.0;
    Temp = ((FLOAT)1.0 - Temp) * ScfTab[18 - bexp] + Temp * ScfTab[17 - bexp];
    Thresh = (Temp * VadStat.Nlev) / (FLOAT)4096.0;

    /* Compare with the threshold */
    if (Thresh > Enr)
        VadState = 0 ;

    /* Do the various counters */
    if ( VadState ) {
        VadStat.Vcnt ++ ;
        VadStat.Hcnt ++ ;
    }
    else {
        VadStat.Vcnt -- ;
        if ( VadStat.Vcnt < 0 )
            VadStat.Vcnt = 0 ;
    }

    if ( VadStat.Vcnt >= 2 ) {
        VadStat.Hcnt = 6 ;
        if (VadStat.Vcnt >= 3)
            VadStat.Vcnt = 3;
    }

    if ( VadStat.Hcnt ) {
        VadState = 1 ;
        if ( VadStat.Vcnt == 0 )
            VadStat.Hcnt -- ;
    }

    /* Update Periodicy detector */
    VadStat.Polp[0] = VadStat.Polp[2] ;
    VadStat.Polp[1] = VadStat.Polp[3] ;

    return VadState ;
}
