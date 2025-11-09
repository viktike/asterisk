/*
**
** File:        "utilcng2.c"
**
** Description:     General Comfort Noise Generation functions
**
**
** Functions:       Calc_Exc_Rand() Computes random excitation
**                                  used both by coder & decoder
**                  Qua_SidGain()   Quantization of SID gain
**                                  used by coder
**                  Dec_SidGain()   Decoding of SID gain
**                                  used both by coder & decoder
**
** Local functions :
**                  random_number()

*/
/*
	ITU-T G.723.1 Software Package Release 2 (June 2006)

    ITU-T G.723.1 Floating Point Speech Coder ANSI C Source Code.  Version 5.2F
    copyright (c) 1995, AudioCodes, DSP Group, France Telecom,
    Universite de Sherbrooke.  All rights reserved.
	
    Floating-point code copyright (c) 1996,
    Intel Corporation and France Telecom (CNET).
    All rights reserved.

    Last modified : March 2006
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "typedef2.h"
#include "cst2.h"
#include "tab2.h"
#include "util2.h"
#include "exc2.h"
#include "utilcng2.h"

/* Declaration of local functions */
static Word16 random_number(Word16 np1, Word16 *nRandom);

/*
**
** Function:           Calc_Exc_Rand()
**
** Description:        Computation of random excitation for inactive frames:
**                     Adaptive codebook entry selected randomly
**                     Higher rate innovation pattern selected randomly
**                     Computes innovation gain to match curGain
**
** Links to text:
**
** Arguments:
**
**  FLOAT   curGain    current average gain to match
**  FLOAT   *PrevExc   previous/current excitation (updated)
**  FLOAT   *DataExc   current frame excitation
**  Word16  *nRandom   random generator status (input/output)
**  LINEDEF *Line
**
** Outputs:
**
**  FLOAT   *PrevExc
**  FLOAT   *DataExc
**  Word16  *nRandom
**  LINEDEF *Line
**
** Return value:       None
**
*/
void Calc_Exc_Rand(FLOAT curGain, FLOAT *PrevExc, FLOAT *DataExc,
                                      Word16 *nRandom, LINEDEF *Line)
{
    int     i, i_subfr, iblk;
    Word16  temp16;
    Word16  j;
    Word16  TabPos[2*NbPulsBlk], *ptr_TabPos;
    FLOAT   TabSign[2*NbPulsBlk], *ptr_TabSign;
    Word16  *ptr1;
    FLOAT   *curExc;
    FLOAT   x1, x2, ener_ltp, inter_exc, delta, b0, c;
    Word16  tmp[SubFrLen/Sgrid];
    Word16  offset[SubFrames];

    /*
     * generate LTP codes
     */
    Line->Olp[0] = random_number(21, nRandom) + 123;
    Line->Olp[1] = random_number(19, nRandom) + 123;	/* G723.1 Maintenance April 2006 */
    													/* Before : Line->Olp[1] = random_number(21, nRandom) + 123; */
    for (i_subfr=0; i_subfr<SubFrames; i_subfr++) {  /* in [1, NbFilt] */
        Line->Sfs[i_subfr].AcGn = random_number(NbFilt, nRandom) + (Word16)1;
    }
    Line->Sfs[0].AcLg = 1;
    Line->Sfs[1].AcLg = 0;
    Line->Sfs[2].AcLg = 1;
    Line->Sfs[3].AcLg = 3;


    /*
     * Random innovation :
     * Selection of the grids, signs and pulse positions
     */

    /* Signs and Grids */
    ptr_TabSign = TabSign;
    ptr1 = offset;
    for (iblk=0; iblk<SubFrames/2; iblk++) {
        temp16  = random_number((Word16) (1 << (NbPulsBlk+2)), nRandom);
        *ptr1++ = (Word16) (temp16 & 0x0001);
        temp16  >>= 1;
        *ptr1++ = (Word16) (SubFrLen + (temp16 & 0x0001));
        for (i=0; i<NbPulsBlk; i++) {
            temp16 >>= 1;
            *ptr_TabSign++= (FLOAT) 2. * (FLOAT)(temp16 & 0x0001) - (FLOAT) 1.;
        }
    }

    /* Positions */
    ptr_TabPos  = TabPos;
    for (i_subfr=0; i_subfr<SubFrames; i_subfr++) {

        for (i=0; i<(SubFrLen/Sgrid); i++)
            tmp[i] = (Word16) i;
        temp16 = (SubFrLen/Sgrid);
        for (i=0; i<Nb_puls[i_subfr]; i++) {
            j = random_number(temp16, nRandom);
            *ptr_TabPos++ = (Word16) (2 * tmp[(int)j] + offset[i_subfr]);
            temp16--;
            tmp[(int)j] = tmp[(int)temp16];
        }
    }

    /*
     * Compute fixed codebook gains
     */

    ptr_TabPos = TabPos;
    ptr_TabSign = TabSign;
    curExc = DataExc;
    i_subfr = 0;
    for (iblk=0; iblk<SubFrames/2; iblk++) {

        /* decode LTP only */
        Decod_Acbk(curExc, &PrevExc[0], Line->Olp[iblk],
                    Line->Sfs[i_subfr].AcLg, Line->Sfs[i_subfr].AcGn);
        Decod_Acbk(&curExc[SubFrLen], &PrevExc[SubFrLen], Line->Olp[iblk],
            Line->Sfs[i_subfr+1].AcLg, Line->Sfs[i_subfr+1].AcGn);

        ener_ltp = DotProd(curExc, curExc, SubFrLenD);

        inter_exc = (FLOAT) 0.;
        for (i=0; i<NbPulsBlk; i++) {
            inter_exc += curExc[(int)ptr_TabPos[i]] * ptr_TabSign[i];
        }

        c = (ener_ltp - curGain * curGain * (FLOAT)SubFrLenD) * InvNbPulsBlk;

        /*
         * Solve EQ(X) = X**2 + 2 b0 X + c
         */

        b0 = inter_exc * InvNbPulsBlk;
        delta = b0 * b0 - c;

        /* Case delta <= 0 */
        if (delta <= (FLOAT) 0.) {
            x1 = - b0;
        }

        /* Case delta > 0 */
        else {
            delta = (FLOAT) sqrt(delta);
            x1 = - b0 + delta;
            x2 = b0 + delta;
            if (fabs(x2) < fabs(x1))
                x1 = - x2;
        }

        /* Update DataExc */
        if (x1 > Gexc_Max)
            x1 = Gexc_Max;
        if (x1 < -Gexc_Max)
            x1 = -Gexc_Max;

        for (i=0; i<NbPulsBlk; i++) {
            j = *ptr_TabPos++;
            curExc[(int)j] += (x1 * (*ptr_TabSign++));
        }

        for (i=0; i<SubFrLenD; i++) {
            if (curExc[i] > (FLOAT) 32766.5)
                curExc[i] = (FLOAT) 32767.0;
            else if (curExc[i] < (FLOAT) -32767.5)
                curExc[i] = (FLOAT) -32768.0;
        }

        /* update PrevExc */
        for (i=SubFrLenD; i<PitchMax; i++)
            PrevExc[i-SubFrLenD] = PrevExc[i];
        for (i=0; i<SubFrLenD; i++)
            PrevExc[i+PitchMax-SubFrLenD] = curExc[i];

        curExc += SubFrLenD;
        i_subfr += 2;

    } /* end of loop on LTP blocks */

    return;
}

/*
**
** Function:           random_number()
**
** Description:        returns a number randomly taken in [0, n]
**                     with np1 = n+1 at input
**
** Links to text:
**
** Arguments:
**
**  Word16 np1
**  Word16 *nRandom    random generator status (input/output)
**
** Outputs:
**
**  Word16 *nRandom
**
** Return value:       random number in [0, (np1-1)]
**
*/
static Word16 random_number(Word16 np1, Word16 *nRandom)
{
    Word16 temp;

    temp = (Word16) (Rand_lbc(nRandom) & 0x7FFF);
    temp = (Word16) (((Word32)temp * (Word32)np1) >> 15);
    return(temp);
}

/*
**
** Function:           Qua_SidGain()
**
** Description:        Quantization of Sid gain
**                     Pseudo-log quantizer in 3 segments
**                     1st segment : length = 16, resolution = 2
**                     2nd segment : length = 16, resolution = 4
**                     3rd segment : length = 32, resolution = 8
**                     quantizes a sum of energies
**
** Links to text:
**
** Arguments:
**
**  FLOAT  *Ener        table of the energies
**  Word16 nq           if nq >= 1 : quantization of nq energies
**                      for SID gain calculation in function Cod_Cng()
**                      if nq = 0 : in function Comp_Info(),
**                      quantization of saved estimated excitation energy
**
** Outputs:             None
**
**
** Return value:        index of quantized energy
**
*/
Word16 Qua_SidGain(FLOAT *Ener, Word16 nq)
{
    Word16  temp16, iseg, iseg_p1;
    Word32  j, j2, k, exp;
    FLOAT   temp, x, y, z;
    int     i;

    if (nq == 0) {
        /* Quantize energy saved for frame erasure case                */
        /* x = fact[0] * Ener[0] with fact[0] = 1/(2*SubFrLen)         */
        x = fact[0] * Ener[0];
    }

    else {

        /*
         * Compute weighted average of energies
         * x = fact[nq] x SUM(i=0->nq-1) Ener[i]
         * with fact[nq] =  fact_mul x fact_mul / nq x Frame
         */
        for (i=0, x=(FLOAT)0.; i<nq; i++)
            x += Ener[i];
        x *= fact[nq];
    }

    /* Quantize x */
    if (x >= bseg[2])
        return(63);

    /* Compute segment number iseg */
    if (x >= bseg[1]) {
        iseg = 2;
        exp = 4;
    }
    else {
        exp  = 3;
        if (x >= bseg[0])
            iseg = 1;
        else
            iseg = 0;
    }

    iseg_p1 = (Word16) (iseg + 1);
    j = 1 << exp;
    k = j >> 1;

    /* Binary search in segment iseg */
    for (i=0; i<exp; i++) {
        temp = base[iseg] + (FLOAT) (j << iseg_p1);
        y = temp * temp;
        if (x >= y)
            j += k;
        else
            j -= k;
        k = k >> 1;
    }

    temp = base[iseg] + (FLOAT) (j << iseg_p1);
    y =  (temp * temp) - x;
    if (y <= (FLOAT)0.0) {
        j2 = j + 1;
        temp = base[iseg] + (FLOAT) (j2 << iseg_p1);
        z = x - (temp * temp);
        if (y > z)
            temp16 = (Word16) ((iseg<<4) + j);
        else
            temp16 = (Word16) ((iseg<<4) + j2);
    }
    else {
        j2 = j - 1;
        temp = base[iseg] + (FLOAT) (j2 << iseg_p1);
        z = x - (temp * temp);
        if (y < z)
            temp16 = (Word16) ((iseg<<4) + j);
        else
            temp16 = (Word16) ((iseg<<4) + j2);
    }
    return(temp16);
}

/*
**
** Function:           Dec_SidGain()
**
** Description:        Decoding of quantized Sid gain
**                     (corresponding to sqrt of average energy)
**
** Links to text:
**
** Arguments:
**
**  Word16 iGain        index of quantized Sid Gain
**
** Outputs:             None
**
** Return value:        decoded gain value << 5
**
*/
FLOAT Dec_SidGain(Word16 iGain)
{
    Word16 i, iseg;
    FLOAT  temp;

    iseg = (Word16) (iGain >> 4);
    if (iseg == 3)
        iseg = 2;
    i = (Word16) (iGain - (iseg << 4));
    temp = base[iseg] + (FLOAT) (i << (iseg + 1));
    return(temp);
}
