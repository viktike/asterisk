/*
**
** File:    lpc2.c
**
** Description: Functions that implement linear predictive coding
**      (LPC) operations.
**
** Functions:
**
**  Computing LPC coefficients:
**
**      Comp_Lpc()
**      Durbin()
**
**  Perceptual noise weighting:
**
**      Wght_Lpc()
**      Error_Wght()
**
**  Computing combined impulse response:
**
**      Comp_Ir()
**
**  Computing ringing response:
**
**      Sub_Ring()
**      Upd_Ring()
**
**  Synthesizing speech:
**
**      Synt()
**      Spf()
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

#include <stdio.h>
#include <math.h>
#include <float.h>

#include "typedef2.h"
#include "cst2.h"
#include "tab2.h"
#include "lbccode2.h"
#include "coder2.h"
#include "decod2.h"
#include "util2.h"
#include "lpc2.h"
#include "codcng2.h"

/*
**
** Function:        Comp_Lpc()
**
** Description:     Computes the tenth-order LPC filters for an
**          entire frame.  For each subframe, a
**          Hamming-windowed block of 180 samples,
**          centered around the subframe, is used to
**          compute eleven autocorrelation coefficients.
**          The Levinson-Durbin algorithm then generates
**          the LPC coefficients.  This function requires
**          a look-ahead of one subframe, and hence
**          introduces a 7.5 ms encoding delay.
**
** Links to text:   Section 2.4
**
** Arguments:
**
**  FLOAT  *UnqLpc      Empty Buffer
**  FLOAT  PrevDat[]    Previous 2 subframes of samples (120 words)
**  FLOAT  DataBuff[]   Current frame of samples (240 words)
**
** Outputs:
**
**
**  FLOAT  UnqLpc[]     LPC coefficients for entire frame (40 words)
**
** Return value:    None
**
*/
void  Comp_Lpc(FLOAT *UnqLpc, FLOAT *PrevDat, FLOAT *DataBuff)
{
    int   i,j,k;

    FLOAT  Dpnt[Frame+LpcFrame-SubFrLen];
    FLOAT  Vect[LpcFrame];
    FLOAT  Acf_sf[LpcOrderP1*SubFrames];
    FLOAT  *curAcf;
    FLOAT  Pk2;

    /*
     * Generate a buffer of 360 samples.  This consists of 120 samples
     * from the previous frame and 240 samples from the current frame.
     */

    for (i=0; i < LpcFrame-SubFrLen; i++)
        Dpnt[i] = PrevDat[i];
    for (i=0; i < Frame; i++)
        Dpnt[i+LpcFrame-SubFrLen] = DataBuff[i];

    /*
     * Repeat for all subframes
     */

    curAcf = Acf_sf;
    for (k=0; k < SubFrames; k++)
    {
        /*  Apply the Hamming window */

        for (i = 0; i < LpcFrame; i++)
            Vect[i] = Dpnt[k*SubFrLen+i]*HammingWindowTable[i];

        /*  Compute the autocorrelation coefficients  */

        curAcf[0] = DotProd(Vect, Vect, LpcFrame)/(LpcFrame*LpcFrame);

        /*  Do Ridge regression  */

        curAcf[0] *= ((FLOAT)1025.0/(FLOAT)1024.0);
        if (curAcf[0] == (FLOAT)0.0) {
            for (i = 1; i <= LpcOrder; i++)
                curAcf[i] = (FLOAT) 0.0;
        }
        else {
            for (i = 1; i <= LpcOrder; i++)
                curAcf[i] = DotProd(Vect, &Vect[i], LpcFrame-i)
                            / (LpcFrame*LpcFrame) * BinomialWindowTable[i-1];
        }

        /*
         * Apply the Levinson-Durbin algorithm to generate the LPC
         * coefficients
         */

        Durbin(&UnqLpc[k*LpcOrder], &curAcf[1], curAcf[0], &Pk2);
        CodStat.SinDet <<= 1;
        if (Pk2 > (FLOAT) 0.95) {
            CodStat.SinDet++;
        }
        curAcf += LpcOrderP1;
    }

    /* Update sine detector */
    CodStat.SinDet &= 0x7fff ;

    j = CodStat.SinDet ;
    k = 0 ;
    for ( i = 0 ; i < 15 ; i ++ ) {
        k += j & 1 ;
        j >>= 1 ;
    }
    if ( k >= 14 )
        CodStat.SinDet |= 0x8000 ;

    Update_Acf(Acf_sf);
}


/*
**
** Function:        Durbin()
**
** Description:     Implements the Levinson-Durbin algorithm for a
**          subframe.  The Levinson-Durbin algorithm
**          recursively computes the minimum mean-squared
**          error (MMSE) linear prediction filter based on the
**          estimated autocorrelation coefficients.
**
** Links to text:   Section 2.4
**
** Arguments:
**
**  FLOAT *Lpc Empty buffer
**  FLOAT Corr[]   First- through tenth-order autocorrelations (10 words)
**  FLOAT Err  Zeroth-order autocorrelation, or energy
**
** Outputs:
**
**  FLOAT Lpc[]    LPC coefficients (10 words)
**
** Return value:    None
**
*/
FLOAT  Durbin(FLOAT *Lpc, FLOAT *Corr, FLOAT Err, FLOAT *Pk2)
{
    int    i,j;
    FLOAT  Temp[LpcOrder];
    FLOAT  Pk,Tmp0;

    /*  Initialize the LPC vector  */

    for (i=0; i < LpcOrder; i++)
        Lpc[i] = (FLOAT)0.0;

    /*  Compute the partial correlation (parcor) coefficient  */

    for (i=0; i < LpcOrder; i++)
    {
        Tmp0 = Corr[i];
        for (j=0; j<i; j++)
            Tmp0 -= Lpc[j]*Corr[i-j-1];

        if (fabs(Tmp0) >= Err) {
            *Pk2 = (FLOAT)0.99;
            break;
        }

        Lpc[i] = Pk = Tmp0/Err;
        Err -= Tmp0*Pk;

        /*
         * Sine detector
         */
        if ( i == 1 )
            *Pk2 = -Pk;

        for (j=0; j < i; j++)
            Temp[j] = Lpc[j];

        for (j=0; j < i; j++)
            Lpc[j] = Lpc[j] - Pk*Temp[i-j-1];
    }
    return Err;
}


/*
**
** Function:        Wght_Lpc()
**
** Description:     Computes the formant perceptual weighting
**          filter coefficients for a frame.  These
**          coefficients are geometrically scaled versions
**          of the unquantized LPC coefficients.
**
** Links to text:   Section 2.8
**
** Arguments:
**
**  FLOAT  *PerLpc      Empty Buffer
**  FLOAT  UnqLpc[]     Unquantized LPC coefficients (40 words)
**
** Outputs:
**
**
**  FLOAT  PerLpc[]     Perceptual weighting filter coefficients
**              (80 words)
**
** Return value:    None
**
*/
void  Wght_Lpc(FLOAT *PerLpc, FLOAT *UnqLpc)
{
    int  i,j;

    for (i=0; i < SubFrames; i++)
    {
        /*
         * Compute the jth FIR coefficient by multiplying the jth LPC
         * coefficient by (0.9)^j. Compute the jth IIR coefficient by
         * multiplying the jth LPC coefficient by (0.5)^j.
         */

        for (j=0; j < LpcOrder; j++)
        {
            PerLpc[j]          = UnqLpc[j]*PerFiltZeroTable[j];
            PerLpc[j+LpcOrder] = UnqLpc[j]*PerFiltPoleTable[j];
        }
        PerLpc += 2*LpcOrder;
        UnqLpc += LpcOrder;
    }
}

/*
**
** Function:        Error_Wght()
**
** Description:     Implements the formant perceptual weighting
**          filter for a frame. This filter effectively
**          deemphasizes the formant frequencies in the
**          error signal.
**
** Links to text:   Section 2.8
**
** Arguments:
**
**  FLOAT  Dpnt[]       Highpass filtered speech x[n] (240 words)
**  FLOAT  PerLpc[]     Filter coefficients (80 words)
**
** Inputs:
**
**  CodStat.WghtFirDl[] FIR filter memory from previous frame (10 words)
**  CodStat.WghtIirDl[] IIR filter memory from previous frame (10 words)
**
**
** Outputs:
**
**  FLOAT  Dpnt[]       Weighted speech f[n] (240 words)
**
** Return value:    None
**
*/
void  Error_Wght(FLOAT *Dpnt, FLOAT *PerLpc)
{
    int  i,j,k;

    FLOAT Acc0;


    for (k=0; k < SubFrames; k++)
    {
        for (i=0; i < SubFrLen; i++)
        {
            /*  FIR part  */

            Acc0 = *Dpnt - DotProd(PerLpc,CodStat.WghtFirDl,LpcOrder);

            for (j=LpcOrder-1; j > 0; j --)
                CodStat.WghtFirDl[j] = CodStat.WghtFirDl[j-1];

            CodStat.WghtFirDl[0] = *Dpnt;

            /*  IIR part  */

            Acc0 += DotProd(&PerLpc[LpcOrder],CodStat.WghtIirDl,LpcOrder);

            for (j = LpcOrder-1; j > 0; j --)
                CodStat.WghtIirDl[j] = CodStat.WghtIirDl[j-1];

            *Dpnt++ = CodStat.WghtIirDl[0] = Acc0;

        }
        PerLpc += 2*LpcOrder;
    }
}


/*
**
** Function:        Comp_Ir()
**
** Description:     Computes the combined impulse response of the
**          formant perceptual weighting filter, harmonic
**          noise shaping filter, and synthesis filter for
**          a subframe.
**
** Links to text:   Section 2.12
**
** Arguments:
**
**  FLOAT  *ImpResp     Empty Buffer
**  FLOAT  QntLpc[]     Quantized LPC coefficients (10 words)
**  FLOAT  PerLpc[]     Perceptual filter coefficients (20 words)
**  PWDEF  Pw           Harmonic noise shaping filter parameters
**
** Outputs:
**
**  FLOAT  ImpResp[]    Combined impulse response (60 words)
**
** Return value:    None
**
*/
void  Comp_Ir(FLOAT *ImpResp, FLOAT *QntLpc, FLOAT *PerLpc, PWDEF Pw)
{
    int    i,j;

    FLOAT  FirDl[LpcOrder];
    FLOAT  IirDl[LpcOrder];
    FLOAT  Temp[PitchMax+SubFrLen];
    FLOAT  Acc0,Acc1;

    /*
     * Clear all memory.  Impulse response calculation requires
     * an all-zero initial state.
     */

    /* Perceptual weighting filter */

    for (i=0; i < LpcOrder; i++)
        FirDl[i] = IirDl[i] = (FLOAT)0.0;

    /* Harmonic noise shaping filter */

    for (i=0; i < PitchMax+SubFrLen; i++)
        Temp[i] = (FLOAT)0.0;

    /*  Input a single impulse  */

    Acc0 = (FLOAT)1.0;

    /*  Do for all elements in a subframe  */

    for (i=0; i < SubFrLen; i++)
    {
        /*  Synthesis filter  */

        Acc1 = Acc0 = Acc0 + DotProd(QntLpc,FirDl,LpcOrder);

        /*  Perceptual weighting filter  */

        /*  FIR part */

        Acc0 -= DotProd(PerLpc,FirDl,LpcOrder);
        for (j=LpcOrder-1; j > 0; j--)
            FirDl[j] = FirDl[j-1];

        FirDl[0] = Acc1;

        /*  IIR part */

        Acc0 += DotProd(&PerLpc[LpcOrder],IirDl,LpcOrder);
        for (j=LpcOrder-1; j > 0; j--)
            IirDl[j] = IirDl[j-1];

        Temp[PitchMax+i] = IirDl[0] = Acc0;

        /*  Harmonic noise shaping filter  */

        ImpResp[i] = Acc0 - Pw.Gain*Temp[PitchMax-Pw.Indx+i];

        Acc0 = (FLOAT)0.0;
    }
}


/*
**
** Function:        Sub_Ring()
**
** Description:     Computes the zero-input response of the
**          combined formant perceptual weighting filter,
**          harmonic noise shaping filter, and synthesis
**          filter for a subframe.  Subtracts the
**          zero-input response from the harmonic noise
**          weighted speech vector to produce the target
**          speech vector.
**
** Links to text:   Section 2.13
**
** Arguments:
**
**  FLOAT Dpnt[]       Harmonic noise weighted vector w[n] (60 words)
**  FLOAT QntLpc[]     Quantized LPC coefficients (10 words)
**  FLOAT PerLpc[]     Perceptual filter coefficients (20 words)
**  FLOAT PrevErr[]    Harmonic noise shaping filter memory (145 words)
**  PWDEF Pw        Harmonic noise shaping filter parameters
**
** Inputs:
**
**  CodStat.RingFirDl[] Perceptual weighting filter FIR memory from
**               previous subframe (10 words)
**  CodStat.RingIirDl[] Perceptual weighting filter IIR memory from
**               previous subframe (10 words)
**
** Outputs:
**
**  FLOAT Dpnt[]       Target vector t[n] (60 words)
**
** Return value:    None
**
*/
void  Sub_Ring(FLOAT *Dpnt, FLOAT *QntLpc, FLOAT *PerLpc, FLOAT *PrevErr,
               PWDEF Pw)
{
    int    i,j;
    FLOAT  Acc0,Acc1;

    FLOAT  FirDl[LpcOrder];
    FLOAT  IirDl[LpcOrder];
    FLOAT  Temp[PitchMax+SubFrLen];

    /*  Initialize the memory  */

    for (i=0; i < PitchMax; i++)
        Temp[i] = PrevErr[i];

    for (i=0; i < LpcOrder; i++)
    {
        FirDl[i] = CodStat.RingFirDl[i];
        IirDl[i] = CodStat.RingIirDl[i];
    }

    /*  Do for all elements in a subframe  */

    for (i=0; i < SubFrLen; i++)
    {
        /*  Synthesis filter  */

        Acc1 = Acc0 = DotProd(QntLpc,FirDl,LpcOrder);

        /*  Perceptual weighting filter  */

        /*  FIR part */

        Acc0 -= DotProd(PerLpc,FirDl,LpcOrder);

        for (j=LpcOrder-1; j > 0; j--)
            FirDl[j] = FirDl[j-1];

        FirDl[0] = Acc1;

        /*  IIR part */

        Acc0 += DotProd(&PerLpc[LpcOrder],IirDl,LpcOrder);

        for (j=LpcOrder-1; j > 0; j--)
            IirDl[j] = IirDl[j-1];

        Temp[PitchMax+i] = IirDl[0] = Acc0;
        /*
         * Do the harmonic noise shaping filter and subtract the result
         * from the harmonic noise weighted vector.
         */
        Dpnt[i] -= Acc0 - Pw.Gain*Temp[PitchMax-Pw.Indx+i];
    }
}


/*
**
** Function:        Upd_Ring()
**
** Description:     Updates the memory of the combined formant
**          perceptual weighting filter, harmonic noise
**          shaping filter, and synthesis filter for a
**          subframe.  The update is done by passing the
**          current subframe's excitation through the
**          combined filter.
**
** Links to text:   Section 2.19
**
** Arguments:
**
**  FLOAT  Dpnt[]       Decoded excitation for the current subframe e[n]
**               (60 words)
**  FLOAT  QntLpc[]     Quantized LPC coefficients (10 words)
**  FLOAT  PerLpc[]     Perceptual filter coefficients (20 words)
**  FLOAT  PrevErr[]    Harmonic noise shaping filter memory (145 words)
**
** Inputs:
**
**  CodStat.RingFirDl[] Perceptual weighting filter FIR memory from
**               previous subframe (10 words)
**  CodStat.RingIirDl[] Perceptual weighting filter IIR memory from
**               previous subframe (10 words)
**
** Outputs:
**
**  FLOAT  PrevErr[]    Updated harmonic noise shaping filter memory
**  CodStat.RingFirDl[] Updated perceptual weighting filter FIR memory
**  CodStat.RingIirDl[] Updated perceptual weighting filter IIR memory
**
** Return value:    None
**
*/
void  Upd_Ring(FLOAT *Dpnt, FLOAT *QntLpc, FLOAT *PerLpc, FLOAT *PrevErr)
{
    int    i,j;
    FLOAT  Acc0,Acc1;

    /*  Shift the harmonic noise shaping filter memory  */

    for (i=SubFrLen; i < PitchMax; i++)
        PrevErr[i-SubFrLen] = PrevErr[i];

    /*  Do for all elements in the subframe  */

    for (i=0; i < SubFrLen; i++)
    {
        /*  Synthesis filter  */

        Acc0 = Dpnt[i] + DotProd(QntLpc,CodStat.RingFirDl,LpcOrder);

        Dpnt[i] = Acc0;
        Acc1 = Acc0;

        /*  Perceptual weighting filter  */
        /*  FIR part */

        Acc0 -= DotProd(PerLpc,CodStat.RingFirDl,LpcOrder);

        for (j=LpcOrder-1; j > 0; j--)
            CodStat.RingFirDl[j] = CodStat.RingFirDl[j-1];
        CodStat.RingFirDl[0] = Acc1;

        /*  IIR part */

        Acc0 += DotProd(&PerLpc[LpcOrder],CodStat.RingIirDl,LpcOrder);

        /* Update IIR memory */
        for (j=LpcOrder-1; j > 0; j--)
            CodStat.RingIirDl[j] = CodStat.RingIirDl[j-1];

        CodStat.RingIirDl[0] = Acc0;
        PrevErr[PitchMax-SubFrLen+i] = CodStat.RingIirDl[0];
    }
}


/*
**
** Function:        Synt()
**
** Description:     Implements the decoder synthesis filter for a
**          subframe.  This is a tenth-order IIR filter.
**
** Links to text:   Section 3.7
**
** Arguments:
**
**  FLOAT  Dpnt[]       Pitch-postfiltered excitation for the current
**               subframe ppf[n] (60 words)
**  FLOAT  Lpc[]        Quantized LPC coefficients (10 words)
**
** Inputs:
**
**  DecStat.SyntIirDl[] Synthesis filter memory from previous
**               subframe (10 words)
**
** Outputs:
**
**  FLOAT  Dpnt[]       Synthesized speech vector sy[n]
**  DecStat.SyntIirDl[] Updated synthesis filter memory
**
** Return value:    None
**
*/
void Synt(FLOAT *Dpnt, FLOAT *Lpc)
{
    int     i,j;
    FLOAT   Acc0;

    for (i=0 ; i < SubFrLen ; i++)
    {
        Acc0 = Dpnt[i] + DotProd(Lpc,DecStat.SyntIirDl,LpcOrder);

        for (j=LpcOrder-1 ; j > 0 ; j--)
            DecStat.SyntIirDl[j] = DecStat.SyntIirDl[j-1];

        Dpnt[i] = DecStat.SyntIirDl[0] = Acc0;
    }
}


/*
**
** Function:        Spf()
**
** Description:     Implements the formant postfilter for a
**          subframe.  The formant postfilter is a
**          10-pole, 10-zero ARMA filter followed by a
**          single-tap tilt compensation filter.
**
** Links to text:   Section 3.8
**
** Arguments:
**
**  FLOAT Tv[]     Synthesized speech vector sy[n] (60 words)
**  FLOAT Lpc[]        Quantized LPC coefficients (10 words)
**  FLOAT Sen      Input speech vector energy
**
** Inputs:
**
**  DecStat.PostIirDl[] Postfilter IIR memory from previous subframe (10 words)
**  DecStat.PostFirDl[] Postfilter FIR memory from previous subframe (10 words)
**  DecStat.Park        Previous value of compensation filter parameter
**
** Outputs:
**
**  FLOAT Tv[]     Postfiltered speech vector pf[n] (60 words)
**  DecStat.PostIirDl[] Updated postfilter IIR memory
**  DecStat.PostFirDl[] Updated postfilter FIR memory
**  DecStat.Park        Updated compensation filter parameter
**
** Return value:    None
**
*/
FLOAT  Spf(FLOAT *Tv, FLOAT *Lpc)
{
    int    i,j;

    FLOAT  Acc0, Acc1;
    FLOAT  Sen;
    FLOAT  Tmp;
    FLOAT  FirCoef[LpcOrder];
    FLOAT  IirCoef[LpcOrder];

    /*
     * Compute ARMA coefficients.  Compute the jth FIR coefficient by
     * multiplying the jth quantized LPC coefficient by (0.65)^j.
     * Compute the jth IIR coefficient by multiplying the jth quantized
     * LPC coefficient by (0.75)^j.  This emphasizes the formants in
     * the frequency response.
     */

    for (i=0; i < LpcOrder; i++)
    {
        FirCoef[i] = Lpc[i]*PostFiltZeroTable[i];
        IirCoef[i] = Lpc[i]*PostFiltPoleTable[i];
    }

    /* Compute the first two autocorrelation coefficients R[0] and R[1] */

    Acc0 = DotProd(Tv,&Tv[1],SubFrLen-1);
    Acc1 = DotProd(Tv,Tv,SubFrLen);

    /* energy */
    Sen = Acc1;

    /*
     * Compute the first-order partial correlation coefficient of the
     * input speech vector.
     */

    if (Acc1 > (FLOAT) FLT_MIN)
        Tmp = Acc0/Acc1;
    else
        Tmp = (FLOAT)0.0;

    /* Update the parkor memory */

    DecStat.Park = (((FLOAT)0.75)*DecStat.Park + ((FLOAT)0.25)*Tmp);
    Tmp = DecStat.Park*PreCoef;

    /* Do the formant post filter */

    for (i=0; i<SubFrLen; i++)
    {
        /* FIR Filter */

        Acc0 = Tv[i] - DotProd(FirCoef,DecStat.PostFirDl,LpcOrder);

        for (j=LpcOrder-1; j > 0; j--)
            DecStat.PostFirDl[j] = DecStat.PostFirDl[j-1];

        DecStat.PostFirDl[0] = Tv[i];

        /* IIR Filter */

        Acc0 += DotProd(IirCoef,DecStat.PostIirDl,LpcOrder);

        for (j=LpcOrder-1; j > 0; j--)
            DecStat.PostIirDl[j] = DecStat.PostIirDl[j-1];

        DecStat.PostIirDl[0] = Acc0;

        /* Preemphasis */

        Tv[i] = Acc0 + DecStat.PostIirDl[1] * Tmp;

    }
    return Sen;
}
