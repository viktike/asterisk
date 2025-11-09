/*
**
** File:    exc2.c
**
** Description: Functions that implement adaptive and fixed codebook
**       operations.
**
** Functions:
**
**  Computing Open loop Pitch lag:
**
**      Estim_Pitch()
**
**  Harmonic noise weighting:
**
**      Comp_Pw()
**      Filt_Pw()
**
**  Fixed Cobebook computation:
**
**      Find_Fcbk()
**      Gen_Trn()
**      Find_Best()
**      Fcbk_Pack()
**      Fcbk_Unpk()
**      ACELP_LBC_code()
**      Cor_h()
**      Cor_h_X()
**      reset_max_time()
**      D4i64_LBC()
**      G_code()
**      search_T0()
**
**  Adaptive Cobebook computation:
**
**      Find_Acbk()
**      Get_Rez()
**      Decod_Acbk()
**
**  Pitch postfilter:
**      Comp_Lpf()
**      Find_B()
**      Find_F()
**      Filt_Lpf()
**
**  Residual interpolation:
**
**      Comp_Info()
**      Regen()
**
** Functions used to avoid possible explosion of the decoder
** excitation in case of series of long term unstable filters
** and when the encoder and the decoder are de-synchronized
**
**      Update_Err()
**      Test_Err()
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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

#include "typedef2.h"
#include "cst2.h"
#include "tab2.h"
#include "lbccode2.h"
#include "coder2.h"
#include "util2.h"
#include "exc2.h"
#include "utilcng2.h"

/*
**
** Function:        Estim_Pitch()
**
** Description: Open loop pitch estimation made twice per frame (one for
**              the first two subframes and one for the last two).
**              The method is based on the maximization of the
**              crosscorrelation of the speech.
**
** Links to text:   Section 2.9
**
** Arguments:
**
**  FLOAT *Dpnt    Perceptually weighted speech
**  int   Start    Starting index defining the subframes under study
**
** Outputs:
**
** Return value:
**
**  int      Open loop pitch period
**
*/
int Estim_Pitch(FLOAT *Dpnt, int Start)
{
    int     i;

    int     Pr,Indx = PitchMin;
    FLOAT   MaxE = (FLOAT)1.0;
    FLOAT   MaxC = (FLOAT)0.0;
    FLOAT   E,C,C2,Diff;

    Pr = Start - PitchMin + 1;

    /* Init the energy estimate */

    E = DotProd(&Dpnt[Pr],&Dpnt[Pr],2*SubFrLen);

    /* Main Open loop pitch search loop */

    for (i=PitchMin; i <= PitchMax-3; i++)
    {
        Pr--;

        /* Update energy, compute cross */

        E = E - Dpnt[Pr+2*SubFrLen]*Dpnt[Pr+2*SubFrLen] + Dpnt[Pr]*Dpnt[Pr];
        C = DotProd(&Dpnt[Start],&Dpnt[Pr],2*SubFrLen);
        C2 = C*C;

        /* Check for new maximum */

        Diff = C2*MaxE - E*MaxC;
        if (E > (FLOAT)0.0 && C > (FLOAT)0.0)
        {
            if ((Diff > (FLOAT)0.0 && ((i - Indx) < PitchMin))
                 || (Diff > (FLOAT)0.25*C2*MaxE))
            {
                Indx = i;
                MaxE = E;
                MaxC = C2;
            }
        }
    }
    return Indx;
}

/*
**
** Function:        Comp_Pw()
**
** Description:     Computes harmonic noise filter coefficients.
**                  For each subframe, the optimal lag is searched around the
**                  open loop pitch lag based on only positive correlation
**                  maximization.
**
** Links to text:   Section 2.11
**
** Arguments:
**
**  FLOAT *Dpnt    Formant perceptually weighted speech
**  int   Start
**  int   Olp      Open loop pitch lag
**
** Outputs:         None
**
** Return value:
**
**  PWDEF   Word16  Indx  lag of the harmonic noise shaping filter
**          FLOAT   Gain  gain of the harmonic noise shaping filter
**
*/
PWDEF Comp_Pw(FLOAT *Dpnt, int Start, int Olp)
{

    int     i, k;
    FLOAT   Energy,C,E,C2,MaxE,MaxC2,MaxC,Gopt;
    PWDEF   Pw;

    /* Compute target energy */

    Energy = DotProd(&Dpnt[Start],&Dpnt[Start],SubFrLen);

    /* Find position with maximum C2/E value */

    MaxE = (FLOAT)1.0;
    MaxC = (FLOAT)0.0;
    MaxC2 = (FLOAT)0.0;
    Pw.Indx = -1;
    Pw.Gain = (FLOAT)0.0;
    k = Start - (Olp-PwRange);

    for (i=0; i <= 2*PwRange; i++)
    {
        C = DotProd(&Dpnt[Start],&Dpnt[k],SubFrLen);
        E = DotProd(&Dpnt[k],&Dpnt[k],SubFrLen);
        k--;

        if (E > (FLOAT)0.0 && C > (FLOAT)0.0)
        {
            C2 = C*C;
            if (C2*MaxE > E*MaxC2)
            {
                Pw.Indx = i;
                MaxE = E;
                MaxC = C;
                MaxC2 = C2;
            }
        }
    }

    if ( Pw.Indx == -1 )
    {
        Pw.Indx = Olp ;
        return Pw ;
    }

    Pw.Gain = (FLOAT)0.0;
    if (MaxC2 > MaxE*Energy*(FLOAT)0.375)
    {
        if (MaxC > MaxE || MaxE == (FLOAT)0.0)
            Gopt = (FLOAT)1.0;
        else
            Gopt = MaxC/MaxE;

        Pw.Gain = (FLOAT)0.3125*Gopt;
    }
    Pw.Indx = Olp - PwRange + Pw.Indx;
    return Pw;
}

/*
**
** Function:        Filt_Pw()
**
** Description:     Applies harmonic noise shaping filter.
**                  Lth order FIR filter on each subframe (L: filter lag).
**
** Links to text:   Section 2.11
**
** Arguments:
**
**  FLOAT *DataBuff     Target vector
**  FLOAT *Dpnt         Formant perceptually weighted speech
**  int   Start
**  PWDEF Pw            Parameters of the harmonic noise shaping filter
**
** Outputs:
**
**  FLOAT *DataBuff     Target vector
**
** Return value:        None
**
*/
void  Filt_Pw(FLOAT *DataBuff, FLOAT *Dpnt, int Start, PWDEF Pw)
{
    int i;

    /* Perform the harmonic weighting */

    for (i=0; i < SubFrLen; i++)
        DataBuff[Start+i] = Dpnt[PitchMax+Start+i] -
                            Pw.Gain*Dpnt[PitchMax+Start-Pw.Indx+i];
    return;
}

/*
**
** Function:        Find_Fcbk()
**
** Description:     Fixed codebook excitation computation.
**
**
** Links to text:   Sections 2.15 & 2.16
**
** Arguments:
**
**  FLOAT  *Dpnt    Target vector
**  FLOAT  *ImpResp Impulse response of the synthesis filter
**  LineDef *Line   Excitation parameters for one subframe
**  int    Sfc      Subframe index
**
** Outputs:
**
**  FLOAT  *Dpnt    Excitation vector
**  LINEDEF *Line   Fixed codebook parameters for one subframe
**
** Return value:        None
**
*/
void  Find_Fcbk(FLOAT *Dpnt, FLOAT *ImpResp, LINEDEF *Line, int Sfc)
{
    int      i;
    int      Srate, T0_acelp;
    FLOAT    gain_T0;

    BESTDEF  Best;

    switch(WrkRate)
    {
    case Rate63:
        Srate = Nb_puls[Sfc];
        Best.MaxErr = (FLOAT)(-99999999.9);

        Find_Best(&Best, Dpnt, ImpResp, Srate, SubFrLen);

        if ((*Line).Olp[Sfc>>1] < SubFrLen-2)
            Find_Best(&Best, Dpnt, ImpResp, Srate, (*Line).Olp[Sfc>>1]);

        /* Reconstruct the excitation */

        for (i=0; i <  SubFrLen; i++)
            Dpnt[i] = (FLOAT)0.0;

        for (i=0; i < Srate; i++)
            Dpnt[Best.Ploc[i]] = Best.Pamp[i];

        /* Code the excitation */

        Fcbk_Pack(Dpnt, &((*Line).Sfs[Sfc]), &Best, Srate);

        if (Best.UseTrn == 1)
            Gen_Trn(Dpnt, Dpnt, (*Line).Olp[Sfc>>1]);
        break;

    case Rate53:
        T0_acelp = search_T0 ( (Word16) ((*Line).Olp[Sfc>>1]-1+(*Line).Sfs[Sfc].AcLg),
                            (*Line).Sfs[Sfc].AcGn, &gain_T0);

        (*Line).Sfs[Sfc].Ppos = ACELP_LBC_code(Dpnt, ImpResp, T0_acelp, Dpnt,
                                   &(*Line).Sfs[Sfc].Mamp,
                                   &(*Line).Sfs[Sfc].Grid,
                                   &(*Line).Sfs[Sfc].Pamp, gain_T0);

        (*Line).Sfs[Sfc].Tran = 0;
        break;
    }

    return;
}

/*
**
** Function:        Gen_Trn()
**
** Description:     Generation of a train of Dirac functions with the period
**                  Olp.
**
** Links to text:   Section 2.15
**
** Arguments:
**
**  FLOAT  *Dst     Fixed codebook excitation vector with  train of Dirac
**  FLOAT  *Src     Fixed codebook excitation vector without train of Dirac
**  int    Olp      Closed-loop pitch lag of subframe 0 (for subframes 0 & 1)
**                  Closed-loop pitch lag of subframe 2 (for subframes 2 & 3)
**
** Outputs:
**
**  FLOAT  *Dst     excitation vector
**
** Return value:    None
**
*/
void  Gen_Trn(FLOAT *Dst, FLOAT *Src, int Olp)
{
    int    i;
    int    Tmp0;
    FLOAT  Tmp[SubFrLen];

    Tmp0 = Olp;

    for (i=0; i < SubFrLen; i++) {
        Tmp[i] = Src[i];
        Dst[i] = Src[i];
    }

    while (Tmp0 < SubFrLen) {
        for (i=Tmp0; i < SubFrLen; i++)
            Dst[i] += Tmp[i-Tmp0];

        Tmp0 += Olp;
    }
    return;
}


/*
**
** Function:        Find_Best()
**
** Description:     Fixed codebook search for the high rate encoder.
**                  It performs the quantization of the residual signal.
**                  The excitation made of Np positive or negative pulses
**                  multiplied by a gain and whose positions on the grid are
**                  either all odd or all even, should approximate as best as
**                  possible the residual signal (perceptual criterion).
**
** Links to text:   Section 2.15
**
** Arguments:
**
**  BESTDEF *Best   Parameters of the best excitation model
**  FLOAT  *Tv      Target vector
**  FLOAT  *ImpResp Impulse response of the combined filter
**  int    Np       Number of pulses (6 for even subframes, 5 for odd)
**  int    Olp      Closed-loop pitch lag of subframe 0 (for subframes 0 & 1)
**                  Closed-loop pitch lag of subframe 2 (for subframes 2 & 3)
**
** Outputs:
**
**  BESTDEF *Best
**
** Return value:    None
**
*/
void  Find_Best(BESTDEF *Best, FLOAT *Tv, FLOAT *ImpResp,int Np,int Olp)
{

    int     i,j,k,l;
    BESTDEF  Temp;

    int     MaxAmpId;
    FLOAT   MaxAmp;
    FLOAT   Acc0,Acc1,Acc2;

    FLOAT   Imr[SubFrLen];
    FLOAT   OccPos[SubFrLen];
    FLOAT   ImrCorr[SubFrLen];
    FLOAT   ErrBlk[SubFrLen];
    FLOAT   WrkBlk[SubFrLen];


    /* Update Impulse responce */

    if (Olp < (SubFrLen-2)) {
        Temp.UseTrn = 1;
        Gen_Trn(Imr, ImpResp, Olp);
    }
    else {
        Temp.UseTrn = 0;
        for (i = 0; i < SubFrLen; i++)
            Imr[i] = ImpResp[i];
    }

    /* Copy Imr */

    for (i=0; i < SubFrLen; i++)
        OccPos[i] = Imr[i];

    /* Compute Imr AutoCorr function */

    for (i=0;i<SubFrLen;i++)
        ImrCorr[i] = DotProd(&Imr[i],Imr,SubFrLen-i);

    /* Cross correlation with the signal */

    for (i=0;i<SubFrLen;i++)
        ErrBlk[i] = DotProd(&Tv[i],Imr,SubFrLen-i);

    /* Search for the best sequence */

    for (k=0; k < Sgrid; k++)
    {
        Temp.GridId = k;

        /*Find maximum amplitude */

        Acc1 = (FLOAT)0.0;
        for (i=k; i < SubFrLen; i +=Sgrid)
        {
            Acc0 = (FLOAT) fabs(ErrBlk[i]);
            if (Acc0 >= Acc1)
            {
                Acc1 = Acc0;
                Temp.Ploc[0] = i;
            }
        }

        /* Quantize the maximum amplitude */
        Acc2 = Acc1;
        Acc1 = (FLOAT)32767.0;
        MaxAmpId = (NumOfGainLev - MlqSteps);

        for (i=MaxAmpId; i >= MlqSteps; i--)
        {
            Acc0 = (FLOAT) fabs(FcbkGainTable[i]*ImrCorr[0] - Acc2);
            if (Acc0 < Acc1)
            {
                Acc1 = Acc0;
                MaxAmpId = i;
            }
        }
        MaxAmpId --;

        for (i=1; i <=2*MlqSteps; i++)
        {
            for (j=k; j < SubFrLen; j +=Sgrid)
            {
                WrkBlk[j] = ErrBlk[j];
                OccPos[j] = (FLOAT)0.0;
            }
            Temp.MampId = MaxAmpId - MlqSteps + i;

            MaxAmp = FcbkGainTable[Temp.MampId];

            if (WrkBlk[Temp.Ploc[0]] >= (FLOAT)0.0)
                Temp.Pamp[0] = MaxAmp;
            else
                Temp.Pamp[0] = -MaxAmp;

            OccPos[Temp.Ploc[0]] = (FLOAT)1.0;

            for (j=1; j < Np; j++)
            {
                Acc1 = (FLOAT)-32768.0;

                for (l=k; l < SubFrLen; l +=Sgrid)
                {
                    if (OccPos[l] != (FLOAT)0.0)
                        continue;

                    Acc0 = WrkBlk[l] - Temp.Pamp[j-1]*
                                        ImrCorr[abs(l-Temp.Ploc[j-1])];
                    WrkBlk[l] = Acc0;

                    Acc0 = (FLOAT) fabs(Acc0);
                    if (Acc0 > Acc1)
                    {
                        Acc1 = Acc0;
                        Temp.Ploc[j] = l;
                    }
                }

                if (WrkBlk[Temp.Ploc[j]] >= (FLOAT)0.0)
                    Temp.Pamp[j] = MaxAmp;
                else
                    Temp.Pamp[j] = -MaxAmp;

                OccPos[Temp.Ploc[j]] = (FLOAT)1.0;
            }

            /* Compute error vector */

            for (j=0; j < SubFrLen; j++)
                OccPos[j] = (FLOAT)0.0;

            for (j=0; j < Np; j++)
                OccPos[Temp.Ploc[j]] = Temp.Pamp[j];

            for (l=SubFrLen-1; l >= 0; l--)
            {
                Acc0 = (FLOAT)0.0;
                for (j=0; j <= l; j++)
                    Acc0 += OccPos[j]*Imr[l-j];
                OccPos[l] = Acc0;
            }

            /* Evaluate error */

            Acc2 = ((FLOAT)2.0)*DotProd(Tv,OccPos,SubFrLen)
                   - DotProd(OccPos,OccPos,SubFrLen);

            if (Acc2 > (*Best).MaxErr)
            {
                (*Best).MaxErr = Acc2;
                (*Best).GridId = Temp.GridId;
                (*Best).MampId = Temp.MampId;
                (*Best).UseTrn = Temp.UseTrn;
                for (j = 0; j < Np; j++)
                {
                    (*Best).Pamp[j] = Temp.Pamp[j];
                    (*Best).Ploc[j] = Temp.Ploc[j];
                }
            }
        }
    }
    return;
}

/*
**
** Function:        Fcbk_Pack()
**
** Description:     Encoding of the pulse positions and gains for the high
**                  rate case.
**                  Combinatorial encoding is used to transmit the optimal
**                  combination of pulse locations.
**
** Links to text:   Section 2.15
**
** Arguments:
**
**  FLOAT  *Dpnt    Excitation vector
**  SFSDEF *Sfs     Encoded parameters of the excitation model
**  BESTDEF *Best   Parameters of the best excitation model
**  int    Np       Number of pulses (6 for even subframes; 5 for odd subframes)
**
** Outputs:
**
**  SFSDEF *Sfs     Encoded parameters of the excitation model
**
** Return value:    None
**
*/
void  Fcbk_Pack(FLOAT *Dpnt, SFSDEF *Sfs, BESTDEF *Best, int Np)
{
    int i,j;

    /* Code the amplitudes and positions */

    j = MaxPulseNum - Np;

    (*Sfs).Pamp = 0;
    (*Sfs).Ppos = 0;

    for (i=0; i < SubFrLen/Sgrid; i++)
    {
        if (Dpnt[(*Best).GridId + Sgrid*i] == 0)
            (*Sfs).Ppos = (*Sfs).Ppos + CombinatorialTable[j][i];
        else
        {
            (*Sfs).Pamp = (*Sfs).Pamp << 1;
            if (Dpnt[(*Best).GridId + Sgrid*i] < 0)
                (*Sfs).Pamp++;
            j++;

            /* Check for end  */

            if (j == MaxPulseNum)
                break;
        }
    }

    (*Sfs).Mamp = (*Best).MampId;
    (*Sfs).Grid = (*Best).GridId;
    (*Sfs).Tran = (*Best).UseTrn;
    return;
}


/*
**
** Function:        Fcbk_Unpk()
**
** Description:     Decoding of the fixed codebook excitation for both rates.
**                  Gains, pulse positions, grid position (odd or even), signs
**                  are decoded and used to reconstruct the excitation.
**
** Links to text:   Section 2.17 & 3.5
**
** Arguments:
**
**  FLOAT  *Tv      Decoded excitation vector
**  SFSDEF Sfs      Encoded parameters of the excitation (for one subframe)
**  int    Olp      Closed loop adaptive pitch lag
**  int    Sfc      Subframe index
**
** Outputs:
**
**  FLOAT  *Tv      Decoded excitation vector
**
** Return value:    None
**
*/
void  Fcbk_Unpk(FLOAT *Tv, SFSDEF Sfs, int Olp, int Sfc)
{
    int    i,j,Np;
    FLOAT  Tv_tmp[SubFrLen+4];
    FLOAT  acelp_gain,gain_T0;
    int    acelp_sign, acelp_shift, acelp_pos;
    int    offset, ipos, T0_acelp;
    Word32 Acc0;

    switch(WrkRate)
    {
        case Rate63:
        {
            Np = Nb_puls[Sfc];

            for (i=0; i < SubFrLen; i++)
                Tv[i] = (FLOAT)0.0;

            if (Sfs.Ppos >= MaxPosTable[Sfc])
                return;

            /*  Decode the amplitudes and positions */

            j = MaxPulseNum - Np;
            Acc0 = Sfs.Ppos;

            for (i = 0; i < SubFrLen/Sgrid; i++)
            {
                Acc0 -= CombinatorialTable[j][i];

                if (Acc0 < (Word32) 0)
                {
                    Acc0 += CombinatorialTable[j][i];
                    j++;

                    if ((Sfs.Pamp & (1 << (MaxPulseNum-j))) != 0)
                        Tv[Sfs.Grid + Sgrid*i] = -FcbkGainTable[Sfs.Mamp];
                    else
                        Tv[Sfs.Grid + Sgrid*i] =  FcbkGainTable[Sfs.Mamp];

                    if (j == MaxPulseNum)
                        break;
                }
            }

            if (Sfs.Tran == 1)
                Gen_Trn(Tv, Tv, Olp);
            break;
        }

        case Rate53:
        {
            for (i = 0; i < SubFrLen+4; i++)
                Tv_tmp[i] = (FLOAT)0.0;

            acelp_gain = FcbkGainTable[Sfs.Mamp];
            acelp_shift = Sfs.Grid;
            acelp_sign = Sfs.Pamp;
            acelp_pos = (int)Sfs.Ppos;

            offset  = 0;
            for (i=0; i<4; i++)
            {
                ipos = (acelp_pos & 7);
                ipos = (ipos << 3) + acelp_shift + offset;

                if ((acelp_sign & 1)== 1)
                    Tv_tmp[ipos] = acelp_gain;
                else
                    Tv_tmp[ipos] = -acelp_gain;

                offset += 2;
                acelp_pos = acelp_pos >> 3;
                acelp_sign = acelp_sign >> 1;
            }
            for (i = 0; i < SubFrLen; i++)
                Tv[i] = Tv_tmp[i];

            T0_acelp = search_T0( (Olp-1+Sfs.AcLg), Sfs.AcGn, &gain_T0);
            if (T0_acelp < SubFrLen-2)
            {
                for (i = T0_acelp; i < SubFrLen; i++)
                    Tv[i] += Tv[i-T0_acelp]*gain_T0;
            }
            break;
        }
    }
    return;
}

/*
**
** Function:        ACELP_LBC_code()
**
** Description:     Find Algebraic codebook for low bit rate LBC encoder
**
** Links to text:   Section 2.16
**
** Arguments:
**
**   FLOAT  X[]              Target vector.     (in Q0)
**   FLOAT  h[]              Impulse response.  (in Q12)
**   int    T0               Pitch period.
**   FLOAT  code[]           Innovative vector.        (in Q12)
**   int    gain             Innovative vector gain.   (in Q0)
**   int    sign             Signs of the 4 pulses.
**   int    shift            Shift of the innovative vector
**   FLOAT  gain_T0          Gain for pitch synchronous fiter
**
** Inputs :
**
**   FLOAT  X[]              Target vector.     (in Q0)
**   FLOAT  h[]              Impulse response.  (in Q12)
**   int    T0               Pitch period.
**   FLOAT  gain_T0          Gain for pitch synchronous fiter
**
** Outputs:
**
**   FLOAT  code[]           Innovative vector.        (in Q12)
**   int    gain             Innovative vector gain.   (in Q0)
**   int    sign             Signs of the 4 pulses.
**   int    shift            Shift of the innovative vector.
**
** Return value:
**
**   int    index            Innovative codebook index
**
*/
int ACELP_LBC_code(FLOAT X[], FLOAT h[], int T0, FLOAT code[],
    int *ind_gain, int *shift, int *sign, FLOAT gain_T0)
{
    int i, index;
    FLOAT gain_q;
    FLOAT Dn[SubFrLen2], tmp_code[SubFrLen2];
    FLOAT rr[DIM_RR];

    /*  Include fixed-gain pitch contribution into impulse resp. h[] */

    if (T0 < SubFrLen-2)
        for (i = T0; i < SubFrLen; i++)
            h[i] += gain_T0*h[i-T0];

    /*  Compute correlations of h[] needed for the codebook search */

    Cor_h(h, rr);

    /*  Compute correlation of target vector with impulse response. */

    Cor_h_X(h, X, Dn);

    /*  Find codebook index */

    index = D4i64_LBC(Dn, rr, h, tmp_code, rr, shift, sign);

    /*  Compute innovation vector gain. */
    /*  Include fixed-gain pitch contribution into code[]. */

    *ind_gain = G_code(X, rr, &gain_q);

    for (i=0; i < SubFrLen; i++)
        code[i] = tmp_code[i]*gain_q;

    if (T0 < SubFrLen-2)
        for (i=T0; i < SubFrLen; i++)
            code[i] += code[i-T0]*gain_T0;

    return index;
}


/*
**
** Function:        Cor_h()
**
** Description:     Compute correlations of h[] needed for the codebook search.
**
** Links to text:   Section 2.16
**
** Arguments:
**
**  FLOAT  h[]              Impulse response.
**  FLOAT  rr[]             Correlations.
**
**  Outputs:
**
**  FLOAT  rr[]             Correlations.
**
**  Return value :          None
*/
void Cor_h(FLOAT *H, FLOAT *rr)
{

    /*   Compute  correlations of h[]  needed for the codebook search. */
    /*     h[]              :Impulse response. */
    /*     rr[]             :Correlations. */

    FLOAT *rri0i0, *rri1i1, *rri2i2, *rri3i3;
    FLOAT *rri0i1, *rri0i2, *rri0i3;
    FLOAT *rri1i2, *rri1i3, *rri2i3;

    FLOAT *p0, *p1, *p2, *p3;
    FLOAT cor, *h2;
    int   i, k, m, t;
    FLOAT h[SubFrLen2];

    for (i=0; i<SubFrLen; i++)
        h[i+4] = H[i];

    for (i=0; i<4; i++)
        h[i] = (FLOAT)0.0;

    /*  Init pointers */

    rri0i0 = rr;
    rri1i1 = rri0i0 + NB_POS;
    rri2i2 = rri1i1 + NB_POS;
    rri3i3 = rri2i2 + NB_POS;

    rri0i1 = rri3i3 + NB_POS;
    rri0i2 = rri0i1 + MSIZE;
    rri0i3 = rri0i2 + MSIZE;
    rri1i2 = rri0i3 + MSIZE;
    rri1i3 = rri1i2 + MSIZE;
    rri2i3 = rri1i3 + MSIZE;

    /*  Compute rri0i0[], rri1i1[], rri2i2[] and rri3i3[] */

    cor = (FLOAT)0.0;
    m = 0;
    for (i=NB_POS-1; i>=0; i--)
    {
        cor += h[m+0]*h[m+0] + h[m+1]*h[m+1];   rri3i3[i] = cor;
        cor += h[m+2]*h[m+2] + h[m+3]*h[m+3];   rri2i2[i] = cor;
        cor += h[m+4]*h[m+4] + h[m+5]*h[m+5];   rri1i1[i] = cor;
        cor += h[m+6]*h[m+6] + h[m+7]*h[m+7];   rri0i0[i] = cor;

        m += 8;
    }

    /*  Compute elements of: rri0i1[], rri0i3[], rri1i2[] and rri2i3[] */

    h2 = h+2;
    p3 = rri2i3 + MSIZE-1;
    p2 = rri1i2 + MSIZE-1;
    p1 = rri0i1 + MSIZE-1;
    p0 = rri0i3 + MSIZE-2;

    for (k=0; k<NB_POS; k++)
    {
        cor = (FLOAT)0.0;
        m = 0;
        t = 0;

        for (i=k+1; i<NB_POS; i++)
        {
            cor += h[m+0]*h2[m+0] + h[m+1]*h2[m+1];   p3[t] = cor;
            cor += h[m+2]*h2[m+2] + h[m+3]*h2[m+3];   p2[t] = cor;
            cor += h[m+4]*h2[m+4] + h[m+5]*h2[m+5];   p1[t] = cor;
            cor += h[m+6]*h2[m+6] + h[m+7]*h2[m+7];   p0[t] = cor;

            t -= (NB_POS+1);
            m += 8;
        }
        cor += h[m+0]*h2[m+0] + h[m+1]*h2[m+1];   p3[t] = cor;
        cor += h[m+2]*h2[m+2] + h[m+3]*h2[m+3];   p2[t] = cor;
        cor += h[m+4]*h2[m+4] + h[m+5]*h2[m+5];   p1[t] = cor;

        h2 += STEP;
        p3 -= NB_POS;
        p2 -= NB_POS;
        p1 -= NB_POS;
        p0 -= 1;
    }


    /*  Compute elements of: rri0i2[], rri1i3[]  */

    h2 = h+4;
    p3 = rri1i3 + MSIZE-1;
    p2 = rri0i2 + MSIZE-1;
    p1 = rri1i3 + MSIZE-2;
    p0 = rri0i2 + MSIZE-2;

    for (k=0; k<NB_POS; k++)
    {
        cor = (FLOAT)0.0;
        m = 0;
        t = 0;

        for (i=k+1; i<NB_POS; i++)
        {
            cor += h[m+0]*h2[m+0] + h[m+1]*h2[m+1];   p3[t] = cor;
            cor += h[m+2]*h2[m+2] + h[m+3]*h2[m+3];   p2[t] = cor;
            cor += h[m+4]*h2[m+4] + h[m+5]*h2[m+5];   p1[t] = cor;
            cor += h[m+6]*h2[m+6] + h[m+7]*h2[m+7];   p0[t] = cor;

            t -= (NB_POS+1);
            m += 8;
        }
        cor += h[m+0]*h2[m+0] + h[m+1]*h2[m+1];   p3[t] = cor;
        cor += h[m+2]*h2[m+2] + h[m+3]*h2[m+3];   p2[t] = cor;

        h2 += STEP;
        p3 -= NB_POS;
        p2 -= NB_POS;
        p1 -= 1;
        p0 -= 1;
    }

    /*  Compute elements of: rri0i1[], rri0i3[], rri1i2[] and rri2i3[] */

    h2 = h+6;
    p3 = rri0i3 + MSIZE-1;
    p2 = rri2i3 + MSIZE-2;
    p1 = rri1i2 + MSIZE-2;
    p0 = rri0i1 + MSIZE-2;

    for (k=0; k<NB_POS; k++)
    {
        cor = (FLOAT)0.0;
        m = 0;
        t = 0;

        for (i=k+1; i<NB_POS; i++)
        {
            cor += h[m+0]*h2[m+0] + h[m+1]*h2[m+1];   p3[t] = cor;
            cor += h[m+2]*h2[m+2] + h[m+3]*h2[m+3];   p2[t] = cor;
            cor += h[m+4]*h2[m+4] + h[m+5]*h2[m+5];   p1[t] = cor;
            cor += h[m+6]*h2[m+6] + h[m+7]*h2[m+7];   p0[t] = cor;

            t -= (NB_POS+1);
            m += 8;
        }
        cor += h[m+0]*h2[m+0] + h[m+1]*h2[m+1];   p3[t] = cor;

        h2 += STEP;
        p3 -= NB_POS;
        p2 -= 1;
        p1 -= 1;
        p0 -= 1;
    }

    return;
}

/*
**
**  Function:     Corr_h_X()
**
**  Description:    Compute  correlations of input response h[] with
**                  the target vector X[].
**
**  Links to the text: Section 2.16
**
** Arguments:
**
**      FLOAT  h[]              Impulse response.
**      FLOAT  X[]              Target vector.
**      FLOAT  D[]              Correlations.
**
**  Outputs:
**
**      FLOAT  D[]              Correlations.
**
**  Return value:           None
*/
void Cor_h_X(FLOAT h[],FLOAT X[],FLOAT D[])
{
    int i;

    for (i=0; i < SubFrLen; i++)
        D[i] = DotProd(&X[i],h,(SubFrLen-i));

    return;
}


/*
** Function:            Reset_max_time()
**
**  Description:        This function should be called at the beginnig
**                      of each frame.
**
**  Links to the text:  Section 2.16
**
**  Arguments:          None
**
**  Inputs:             None
**
**  Outputs:
**
**      Word16          extra
**
**  Return value:           None
**
*/
static int extra;
void reset_max_time(void)
{
    extra = 120;
    return;
}

/*
**
**  Function:       D4i64_LBC
**
**  Description:       Algebraic codebook for LBC.
**                     -> 17 bits; 4 pulses in a frame of 60 samples
**
**                     The code length is 60, containing 4 nonzero pulses
**                     i0, i1, i2, i3. Each pulse can have 8 possible
**                     positions (positive or negative):
**
**                     i0 (+-1) : 0, 8,  16, 24, 32, 40, 48, 56
**                     i1 (+-1) : 2, 10, 18, 26, 34, 42, 50, 58
**                     i2 (+-1) : 4, 12, 20, 28, 36, 44, 52, (60)
**                     i3 (+-1) : 6, 14, 22, 30, 38, 46, 54, (62)
**
**                     All the pulse can be shifted by one.
**                     The last position of the last 2 pulses falls outside the
**                     frame and signifies that the pulse is not present.
**                     The threshold controls if a section of the innovative
**                     codebook should be searched or not.
**
**  Links to the text: Section 2.16
**
**  Input arguments:
**
**      FLOAT  Dn[]       Correlation between target vector & impulse resp h[]
**      FLOAT  rr[]       Correlations of impulse response h[]
**      FLOAT  h[]        Impulse response of filters
**
**  Output arguments:
**
**      FLOAT  cod[]      Selected algebraic codeword
**      FLOAT  y[]        Filtered codeword
**      int    code_shift Shift of the codeword
**      int    sign       Signs of the 4 pulses.
**
**  Return value:
**
**      int    Index of selected codevector
**
*/

int D4i64_LBC(FLOAT Dn[], FLOAT rr[], FLOAT h[], FLOAT cod[],
        FLOAT y[], int *code_shift, int *sign)
{
    int  i0, i1, i2, i3, ip0, ip1, ip2, ip3;
    int  i, j, time;
    int  shif, shift;

    FLOAT  ps0, ps1, ps2, ps3;
    FLOAT  alp0, alp1, alp2, alp3;
    FLOAT  ps0a, ps1a, ps2a;
    FLOAT  ps3c, psc, alpha;
    FLOAT  means, max0, max1, max2, thres;

    FLOAT *rri0i0,*rri1i1,*rri2i2,*rri3i3;
    FLOAT *rri0i1,*rri0i2,*rri0i3;
    FLOAT *rri1i2,*rri1i3,*rri2i3;

    FLOAT *ptr_ri0i0,*ptr_ri1i1,*ptr_ri2i2,*ptr_ri3i3;
    FLOAT *ptr_ri0i1,*ptr_ri0i2,*ptr_ri0i3;
    FLOAT *ptr_ri1i2,*ptr_ri1i3,*ptr_ri2i3;

    int  p_sign[SubFrLen2/2],p_sign2[SubFrLen2/2];

    /*  Init pointers  */

    rri0i0 = rr;
    rri1i1 = rri0i0 + NB_POS;
    rri2i2 = rri1i1 + NB_POS;
    rri3i3 = rri2i2 + NB_POS;

    rri0i1 = rri3i3 + NB_POS;
    rri0i2 = rri0i1 + MSIZE;
    rri0i3 = rri0i2 + MSIZE;
    rri1i2 = rri0i3 + MSIZE;
    rri1i3 = rri1i2 + MSIZE;
    rri2i3 = rri1i3 + MSIZE;

    /*  Extend the backward filtered target vector by zeros                 */

    for (i=SubFrLen; i < SubFrLen2; i++)
        Dn[i] = (FLOAT)0.0;

    /*  Chose the sign of the impulse.                                      */

    for (i=0; i<SubFrLen; i+=2)
    {
        if ((Dn[i] + Dn[i+1]) >= (FLOAT)0.0)
        {
            p_sign[i/2] = 1;
            p_sign2[i/2] = 2;
        }
        else
        {
            p_sign[i/2] = -1;
            p_sign2[i/2] = -2;
            Dn[i] = -Dn[i];
            Dn[i+1] = -Dn[i+1];
        }
    }
    p_sign[30] = p_sign[31] = 1;
    p_sign2[30] = p_sign2[31] = 2;

    /*  - Compute the search threshold after three pulses                  */
    /*  odd positions  */
    /*  Find maximum of Dn[i0]+Dn[i1]+Dn[i2] */

    max0 = Dn[0];
    max1 = Dn[2];
    max2 = Dn[4];
    for (i=8; i < SubFrLen; i+=STEP)
    {
        if (Dn[i] > max0)
            max0 = Dn[i];
        if (Dn[i+2] > max1)
            max1 = Dn[i+2];
        if (Dn[i+4] > max2)
            max2 = Dn[i+4];
    }
    max0 = max0 + max1 + max2;

    /*  Find means of Dn[i0]+Dn[i1]+Dn[i] */

    means = (FLOAT)0.0;
    for (i=0; i < SubFrLen; i+=STEP)
        means += Dn[i+4] + Dn[i+2] + Dn[i];

    means *= (FLOAT)0.125;
    thres = means + (max0-means)*(FLOAT)0.5;


    /*  even positions */
    /*  Find maximum of Dn[i0]+Dn[i1]+Dn[i2] */

    max0 = Dn[1];
    max1 = Dn[3];
    max2 = Dn[5];
    for (i=9; i < SubFrLen; i+=STEP)
    {
        if (Dn[i] > max0)
            max0 = Dn[i];
        if (Dn[i+2] > max1)
            max1 = Dn[i+2];
        if (Dn[i+4] > max2)
            max2 = Dn[i+4];
    }
    max0 = max0 + max1 + max2;

    /*  Find means of Dn[i0]+Dn[i1]+Dn[i2]  */

    means = (FLOAT)0.0;
    for (i=1; i < SubFrLen; i+=STEP)
        means += Dn[i+4] + Dn[i+2] + Dn[i];

    means *= (FLOAT)0.125;
    max1 = means + (max0-means)*(FLOAT)0.5;

    /*  Keep maximum threshold between odd and even position  */

    if (max1 > thres)
        thres = max1;

    /*  Modification of rrixiy[] to take signs into account. */

    ptr_ri0i1 = rri0i1;
    ptr_ri0i2 = rri0i2;
    ptr_ri0i3 = rri0i3;

    for (i0=0; i0<SubFrLen/2; i0+=STEP/2)
    {
        for (i1=2/2; i1<SubFrLen/2; i1+=STEP/2)
        {
            *ptr_ri0i1++ *= p_sign[i0] * p_sign2[i1];
            *ptr_ri0i2++ *= p_sign[i0] * p_sign2[i1+1];
            *ptr_ri0i3++ *= p_sign[i0] * p_sign2[i1+2];
        }
    }

    ptr_ri1i2 = rri1i2;
    ptr_ri1i3 = rri1i3;
    for (i1=2/2; i1<SubFrLen/2; i1+=STEP/2)
    {
        for (i2=4/2; i2<SubFrLen2/2; i2+=STEP/2)
        {
            *ptr_ri1i2++ *= p_sign[i1] * p_sign2[i2];
            *ptr_ri1i3++ *= p_sign[i1] * p_sign2[i2+1];
        }
    }

    ptr_ri2i3 = rri2i3;
    for (i2=4/2; i2<SubFrLen2/2; i2+=STEP/2)
    {
        for (i3=6/2; i3<SubFrLen2/2; i3+=STEP/2)
            *ptr_ri2i3++ *= p_sign[i2] * p_sign2[i3];
    }

    /*-------------------------------------------------------------------
     * Search the optimum positions of the four  pulses which maximize
     *     square(correlation) / energy
     * The search is performed in four  nested loops. At each loop, one
     * pulse contribution is added to the correlation and energy.
     *
     * The fourth loop is entered only if the correlation due to the
     *  contribution of the first three pulses exceeds the preset
     *  threshold.
     */

    /*  Default values  */

    ip0    = 0;
    ip1    = 2;
    ip2    = 4;
    ip3    = 6;
    shif   = 0;
    psc    = (FLOAT)0.0;
    alpha  = (FLOAT)1.0;
    time   = max_time + extra;

    /*  Four loops to search innovation code. */
    /*  Init. pointers that depend on first loop  */

    ptr_ri0i0 = rri0i0;
    ptr_ri0i1 = rri0i1;
    ptr_ri0i2 = rri0i2;
    ptr_ri0i3 = rri0i3;

    /*  first pulse loop   */

    for (i0=0; i0 < SubFrLen; i0 +=STEP)
    {
        ps0  = Dn[i0];
        ps0a = Dn[i0+1];
        alp0 = *ptr_ri0i0++;

        /*  Init. pointers that depend on second loop */

        ptr_ri1i1 = rri1i1;
        ptr_ri1i2 = rri1i2;
        ptr_ri1i3 = rri1i3;

        /*  second pulse loop */

        for (i1=2; i1 < SubFrLen; i1 +=STEP)
        {
            ps1  = ps0 + Dn[i1];
            ps1a = ps0a + Dn[i1+1];

            alp1 = alp0 + *ptr_ri1i1++ + *ptr_ri0i1++;

            /*  Init. pointers that depend on third loop */

            ptr_ri2i2 = rri2i2;
            ptr_ri2i3 = rri2i3;

            /*  third pulse loop */

            for (i2 = 4; i2 < SubFrLen2; i2 +=STEP)
            {
                ps2  = ps1 + Dn[i2];
                ps2a = ps1a + Dn[i2+1];

                alp2 = alp1 + *ptr_ri2i2++ + *ptr_ri0i2++ + *ptr_ri1i2++;

                /*  Decide the shift */

                shift = 0;
                if (ps2a > ps2)
                {
                    shift = 1;
                    ps2   = ps2a;
                }

                /*  Test threshold  */

                if (ps2 > thres)
                {

                    /*  Init. pointers that depend on 4th loop */

                    ptr_ri3i3 = rri3i3;

                    /*  4th pulse loop */

                    for (i3 = 6; i3 < SubFrLen2; i3 +=STEP)
                    {
                        ps3 = ps2 + Dn[i3+shift];
                        alp3 = alp2 + *ptr_ri3i3++ +
                        *ptr_ri0i3++ + *ptr_ri1i3++ + *ptr_ri2i3++;

                        ps3c = ps3 * ps3;
                        if ((ps3c * alpha) > (psc * alp3))
                        {
                            psc = ps3c;
                            alpha = alp3;
                            ip0 = i0;
                            ip1 = i1;
                            ip2 = i2;
                            ip3 = i3;
                            shif = shift;
                        }
                    }

                    time--;

                    /*  Maximum time finish  */

                    if (time <= 0)
                        goto end_search;
                    ptr_ri0i3 -= NB_POS;
                    ptr_ri1i3 -= NB_POS;
                }

                else
                    ptr_ri2i3 += NB_POS;
            }

            ptr_ri0i2 -= NB_POS;
            ptr_ri1i3 += NB_POS;
        }

        ptr_ri0i2 += NB_POS;
        ptr_ri0i3 += NB_POS;
    }

end_search:

    extra = time;

    /*  Set the sign of impulses  */

    i0 = p_sign[(ip0 >> 1)];
    i1 = p_sign[(ip1 >> 1)];
    i2 = p_sign[(ip2 >> 1)];
    i3 = p_sign[(ip3 >> 1)];

    /*  Find the codeword corresponding to the selected positions  */

    for (i=0; i<SubFrLen; i++)
        cod[i] = (FLOAT)0.0;

    if (shif > 0)
    {
        ip0++;
        ip1++;
        ip2++;
        ip3++;
    }

    cod[ip0] =  (FLOAT)i0;
    cod[ip1] =  (FLOAT)i1;
    if (ip2<SubFrLen)
        cod[ip2] = (FLOAT)i2;
    if (ip3<SubFrLen)
        cod[ip3] = (FLOAT)i3;

/*  find the filtered codeword  */

    for (i=0; i < SubFrLen; i++)
        y[i] = (FLOAT)0.0;

    if (i0 > 0)
        for (i=ip0, j=0; i<SubFrLen; i++, j++)
            y[i] = y[i] + h[j];
    else
        for (i=ip0, j=0; i<SubFrLen; i++, j++)
            y[i] = y[i] - h[j];

    if (i1 > 0)
        for (i=ip1, j=0; i<SubFrLen; i++, j++)
            y[i] = y[i] + h[j];
    else
        for (i=ip1, j=0; i<SubFrLen; i++, j++)
            y[i] = y[i] - h[j];

    if (ip2<SubFrLen)
    {
        if (i2 > 0)
            for (i=ip2, j=0; i<SubFrLen; i++, j++)
                y[i] = y[i] + h[j];
        else
            for (i=ip2, j=0; i<SubFrLen; i++, j++)
                y[i] = y[i] - h[j];
    }

    if (ip3<SubFrLen)
    {
        if (i3 > 0)
            for (i=ip3, j=0; i<SubFrLen; i++, j++)
                y[i] = y[i] + h[j];
        else
            for (i=ip3, j=0; i<SubFrLen; i++, j++)
                y[i] = y[i] - h[j];
    }

    *code_shift = shif;

    *sign = 0;
    if (i0 > 0)
        *sign += 1;
    if (i1 > 0)
        *sign += 2;
    if (i2 > 0)
        *sign += 4;
    if (i3 > 0)
        *sign += 8;

    i = ((ip3 >> 3) << 9) + ((ip2 >> 3) << 6) + ((ip1 >> 3) << 3) + (ip0 >> 3);

    return i;
}


/*
**
**  Function:  G_code()
**
**  Description: Compute the gain of innovative code.
**
**
**  Links to the text: Section 2.16
**
** Input arguments:
**
**      FLOAT  X[]        Code target.  (in Q0)
**      FLOAT  Y[]        Filtered innovation code. (in Q12)
**
** Output:
**
**      FLOAT  *gain_q    Gain of innovation code.  (in Q0)
**
**  Return value:
**
**      int     index of innovation code gain
**
*/
int G_code(FLOAT X[], FLOAT Y[], FLOAT *gain_q)
{
    int     i;
    FLOAT   xy, yy, gain_nq;
    int     gain;
    FLOAT   dist, dist_min;

    /*  Compute scalar product <X[],Y[]> */

    xy = DotProd(X,Y,SubFrLen);

    if (xy <= 0)
    {
        gain = 0;
        *gain_q =FcbkGainTable[gain];
        return(gain);
    }

    /*  Compute scalar product <Y[],Y[]>  */

    yy = DotProd(Y,Y,SubFrLen);

    if (yy > (FLOAT) FLT_MIN)
        gain_nq = xy/yy;
    else
        gain_nq = (FLOAT)0.0;

    gain = 0;
    dist_min = (FLOAT)fabs(gain_nq - FcbkGainTable[0]);

    for (i=1; i <NumOfGainLev; i++)
    {
        dist = (FLOAT)fabs(gain_nq - FcbkGainTable[i]);
        if (dist < dist_min)
        {
            dist_min = dist;
            gain = i;
        }
    }
    *gain_q = FcbkGainTable[gain];
    return(gain);
}


/*
**
**  Function:       search_T0()
**
**  Description:          Gets parameters of pitch synchronous filter
**
**  Links to the text:    Section 2.16
**
**  Arguments:
**
**      int    T0         Decoded pitch lag
**      int    Gid        Gain vector index in adaptive gain vector codebook
**      FLOAT  *gain_T0   Pitch synchronous gain
**
**  Outputs:
**
**      FLOAT  *gain_T0   Pitch synchronous filter gain
**
**  Return Value:
**
**      int    T0_mod     Pitch synchronous filter lag
*/
int search_T0 (int T0, int Gid, FLOAT *gain_T0)
{
    int T0_mod;

    T0_mod = T0+epsi170[Gid];
    *gain_T0 = gain170[Gid];

    return(T0_mod);
}

/*
**
** Function:    Update_Err()
**
** Description:   Estimation of the excitation error associated
**          to the excitation signal when it is disturbed at
**          the decoder, the disturbing signal being filtered
**          by the long term synthesis filters
**          one value for (SubFrLen/2) samples
**          Updates the table CodStat.Err
**
** Links to text:   Section
**
** Arguments:
**
**  int Olp    Center value for pitch delay
**  int AcLg   Offset value for pitch delay
**  int AcGn   Index of Gain LT filter
**
** Outputs: None
**
** Return value:  None
**
*/
void Update_Err(int Olp, int AcLg, int AcGn)
{
    int     i, iz, temp2;
    int     Lag;
    FLOAT   Worst1, Worst0, wtemp;
    FLOAT   beta,*ptr_tab;

    Lag = Olp - Pstep + AcLg;

    /* Select Quantization tables */
    i = 0 ;
    ptr_tab = tabgain85;
    if ( WrkRate == Rate63 ) {
        if ( Olp >= (SubFrLen-2) )
            ptr_tab = tabgain170;
    }
    else {
        ptr_tab = tabgain170;
    }
    beta = ptr_tab[(int)AcGn];


    if (Lag <= (SubFrLen/2))
    {
        Worst0 = CodStat.Err[0]*beta + Err0;
        Worst1 = Worst0;
    }
    else
    {
        iz = (int)(((Word32)Lag*1092L) >> 15);
        temp2 = 30*(iz+1);

        if (temp2 != Lag)
        {
            if (iz == 1)
            {
                Worst0 = CodStat.Err[0]*beta + Err0;
                Worst1 = CodStat.Err[1]*beta + Err0;

                if (Worst0 > Worst1)
                    Worst1 = Worst0;
                else
                    Worst0 = Worst1;
            }
            else
            {
                wtemp = CodStat.Err[iz-1]*beta + Err0;
                Worst0 = CodStat.Err[iz-2]*beta + Err0;
                if (wtemp > Worst0)
                    Worst0 = wtemp;
                Worst1 = CodStat.Err[iz]*beta + Err0;
                if (wtemp > Worst1)
                    Worst1 = wtemp;
            }
        }
        else
        {
            Worst0 = CodStat.Err[iz-1]*beta + Err0;
            Worst1 = CodStat.Err[iz]*beta + Err0;
        }
    }

    if (Worst0 > MAXV)
        Worst0 = MAXV;
    if (Worst1 > MAXV)
        Worst1 = MAXV;

    for (i=4; i>=2; i--)
        CodStat.Err[i] = CodStat.Err[i-2];

    CodStat.Err[0] = Worst0;
    CodStat.Err[1] = Worst1;

    return;
}

/*
**
** Function:    Test_Err()
**
** Description:   Check the error excitation maximum for
**          the subframe and computes an index iTest used to
**          calculate the maximum nb of filters (in Find_Acbk) :
**          Bound = Min(Nmin + iTest x pas, Nmax) , with
**          AcbkGainTable085 : pas = 2, Nmin = 51, Nmax = 85
**          AcbkGainTable170 : pas = 4, Nmin = 93, Nmax = 170
**          iTest depends on the relative difference between
**          errmax and a fixed threshold
**
** Links to text:   Section
**
** Arguments:
**
**  Word16 Lag1    1st long term Lag of the tested zone
**  Word16 Lag2    2nd long term Lag of the tested zone
**
** Outputs: None
**
** Return value:
**  Word16      index iTest used to compute Acbk number of filters
*/
int Test_Err(int Lag1, int Lag2)
{
    int     i, i1, i2;
    int     zone1, zone2, iTest;
    FLOAT   Err_max;

    i2 = Lag2 + ClPitchOrd/2;
    zone2 = i2/30;

    i1 = - SubFrLen + 1 + Lag1 - ClPitchOrd/2;
    if (i1 <= 0)
        i1 = 1;
    zone1 = i1/30;

    Err_max = (FLOAT)-1.0;
    for (i=zone2; i>=zone1; i--)
    {
        if (CodStat.Err[i] > Err_max)
            Err_max = CodStat.Err[i];
    }
    if ((Err_max > ThreshErr) || (CodStat.SinDet < 0 ) )
    {
        iTest = 0;
    }
    else
    {
        iTest = (Word16)(ThreshErr - Err_max);
    }

    return(iTest);
}

/*
**
** Function:        Find_Acbk()
**
** Description:     Computation of adaptive codebook contribution in
**                  closed-loop around open-loop pitch lag (subframes 0 & 2)
**                  around the previous subframe closed-loop pitch lag
**                  (subframes 1 & 3).  For subframes 0 & 2, the pitch lag is
**                  encoded whereas for subframes 1 & 3, only the difference
**                  with the previous value is encoded (-1, 0, +1 or +2).
**                  The pitch predictor gains are quantized using one of two
**                  codebooks (85 entries or 170 entries) depending on the
**                  rate and on the pitch lag value.
**                  Finally, the contribution of the pitch predictor is decoded
**                  and subtracted to obtain the residual signal.
**
** Links to text:   Section 2.14
**
** Arguments:
**
**  FLOAT  *Tv      Target vector
**  FLOAT  *ImpResp Impulse response of the combined filter
**  FLOAT  *PrevExc Previous excitation vector
**  LINEDEF *Line   Contains pitch parameters (open/closed loop lag, gain)
**  int    Sfc      Subframe index
**
** Outputs:
**
**  FLOAT  *Tv     Residual vector
**  LINEDEF *Line  Contains pitch related parameters (closed loop lag, gain)
**
** Return value:    None
**
*/
void  Find_Acbk(FLOAT *Tv, FLOAT *ImpResp, FLOAT *PrevExc,
                LINEDEF *Line, int Sfc)
{
    int i,j,k,l;

    FLOAT Acc0,Max;

    FLOAT RezBuf[SubFrLen+ClPitchOrd-1];
    FLOAT FltBuf[ClPitchOrd][SubFrLen];
    FLOAT CorVct[4*(2*ClPitchOrd + ClPitchOrd*(ClPitchOrd-1)/2)];
    FLOAT *lPnt;
    FLOAT *sPnt;

    int   Olp,Lid,Gid,Hb;
    int   Bound[2];
    int   Lag1, Lag2;
    int   off_filt;

    Olp = (*Line).Olp[Sfc>>1];
    Lid = Pstep;
    Gid = 0;
    Hb  = 3 + (Sfc & 1);

    /*  For even frames only */

    if ((Sfc & 1) == 0)
    {
        if (Olp == PitchMin)
            Olp++;
        if (Olp > (PitchMax-5))
            Olp = PitchMax-5;
    }

    lPnt = CorVct;
    for (k=0; k < Hb; k++)
    {

        /*  Get residual from the exitation buffer */

        Get_Rez(RezBuf, PrevExc, Olp-Pstep+k);

        /*  Filter the last one (ClPitchOrd-1) using the impulse responce */

        for (i=0; i < SubFrLen; i++)
        {
            Acc0 = (FLOAT)0.0;
            for (j=0; j <= i; j++)
                Acc0 += RezBuf[ClPitchOrd-1+j]*ImpResp[i-j];

            FltBuf[ClPitchOrd-1][i] = Acc0;
        }

        /*  Update the others (ClPitchOrd-2 down to 0) */

        for (i=ClPitchOrd-2; i >= 0; i --)
        {
            FltBuf[i][0] = RezBuf[i];
            for (j = 1; j < SubFrLen; j++)
                FltBuf[i][j] = RezBuf[i]*ImpResp[j] + FltBuf[i+1][j-1];
        }

        /*  Compute the cross products with the signal */

        for (i=0; i < ClPitchOrd; i++)
            *lPnt++ = DotProd(Tv, FltBuf[i], SubFrLen);

        /*  Compute the energies */

        for (i=0; i < ClPitchOrd; i++)
            *lPnt++ = ((FLOAT)0.5)*DotProd(FltBuf[i], FltBuf[i], SubFrLen);

        /*  Compute the between crosses */

        for (i=1; i < ClPitchOrd; i++)
            for (j = 0; j < i; j++)
                *lPnt++ = DotProd(FltBuf[i], FltBuf[j], SubFrLen);
    }

    /* Test potential error */
    Lag1 = Olp - Pstep;
    Lag2 = Olp - Pstep + Hb - 1;

    off_filt = Test_Err(Lag1, Lag2);

    Bound[0] =  NbFilt085_min + (off_filt << 2);
    if (Bound[0] > NbFilt085)
        Bound[0] = NbFilt085;
    Bound[1] =  NbFilt170_min + (off_filt << 3);
    if (Bound[1] > NbFilt170)
        Bound[1] = NbFilt170;

    Max = (FLOAT)0.0;
    for (k=0; k < Hb; k++)
    {

        /*  Select Quantization table */
        l = 0;
        if (WrkRate == Rate63)
        {
            if ((Sfc & 1) == 0)
            {
                if (Olp-Pstep+k >= SubFrLen-2)
                    l = 1;
            }
            else
            {
                if (Olp >= SubFrLen-2)
                    l = 1;
            }
        }
        else
            l = 1;

        /*  Search for maximum */

        sPnt = AcbkGainTablePtr[l];

        for (i=0; i < Bound[l]; i++)
        {
            Acc0 = DotProd(&CorVct[k*20],sPnt,20);
            sPnt += 20;

            if (Acc0 > Max)
            {
                Max = Acc0;
                Gid = i;
                Lid = k;
            }
        }
    }

    /*  Modify Olp for even sub frames */

    if ((Sfc & 1) == 0)
    {
        Olp = Olp - Pstep + Lid;
        Lid = Pstep;
    }


    /*  Save Lag, Gain and Olp */

    (*Line).Sfs[Sfc].AcLg = Lid;
    (*Line).Sfs[Sfc].AcGn = Gid;
    (*Line).Olp[Sfc>>1] = Olp;

    /*  Decode the Acbk contribution and subtract it */

    Decod_Acbk(RezBuf, PrevExc, Olp, Lid, Gid);

    for (i=0; i < SubFrLen; i++)
    {
        Acc0 = Tv[i];

        for (j=0; j <= i; j++)
            Acc0 -= RezBuf[j]*ImpResp[i-j];

        Tv[i] = Acc0;
    }
}


/*
**
** Function:        Get_Rez()
**
** Description:     Gets delayed contribution from the previous excitation
**                  vector.
**
** Links to text:   Sections 2.14, 2.18 & 3.4
**
** Arguments:
**
**  FLOAT  *Tv      delayed excitation
**  FLOAT  *PrevExc Previous excitation vector
**  int    Lag      Closed loop pitch lag
**
** Outputs:
**
**  FLOAT  *Tv      delayed excitation
**
** Return value:    None
**
*/
void  Get_Rez(FLOAT *Tv, FLOAT *PrevExc, int Lag)
{
    int i;

    for (i=0; i < ClPitchOrd/2; i++)
        Tv[i] = PrevExc[PitchMax - Lag - ClPitchOrd/2 + i];

    for (i=0; i < SubFrLen+ClPitchOrd/2; i++)
        Tv[ClPitchOrd/2+i] = PrevExc[PitchMax - Lag + i%Lag];
}


/*
**
** Function:        Decod_Acbk()
**
** Description:     Computes the adaptive codebook contribution from the previous
**                  excitation vector.
**                  With the gain index, the closed loop pitch lag, the jitter
**                  which when added to this pitch lag gives the actual closed
**                  loop value, and after having selected the proper codebook,
**                  the pitch contribution is reconstructed using the previous
**                  excitation buffer.
**
** Links to text:   Sections 2.14, 2.18 & 3.4
**
** Arguments:
**
**  FLOAT  *Tv      Reconstructed excitation vector
**  FLOAT  *PrevExc Previous excitation vector
**  int    Olp      closed-loop pitch period
**  int    Lid      Jitter around pitch period
**  int    Gid      Gain vector index in 5- dimensional
**                      adaptive gain vector codebook
**
** Outputs:
**
**  FLOAT  *Tv      Reconstructed excitation vector
**
** Return value:    None
**
*/
void  Decod_Acbk(FLOAT *Tv, FLOAT *PrevExc, int Olp, int Lid, int Gid)
{
    int      i;
    FLOAT    RezBuf[SubFrLen+ClPitchOrd-1];
    FLOAT   *sPnt;

    Get_Rez(RezBuf, PrevExc, (Olp + Lid) - Pstep);


    i = 0;
    if (WrkRate == Rate63)
    {
        if (Olp >= (SubFrLen-2))
            i++;
    }
    else
        i=1;

    sPnt = AcbkGainTablePtr[i] + Gid*20;

    /*  Compute output vector */

    for (i=0; i < SubFrLen; i++)
        Tv[i] = DotProd(&RezBuf[i], sPnt, ClPitchOrd);
}

/*
**
** Function:        Comp_Lpf()
**
** Description:     Computes pitch postfilter parameters.
**                  The pitch postfilter lag is first derived (Find_B
**                  and Find_F). Then, the one that gives the largest
**                  contribution is used to calculate the gains (Get_Ind).
**
**
** Links to text:   Section 3.6
**
** Arguments:
**
**  FLOAT  *Buff    decoded excitation
**  int    Olp      Decoded pitch lag
**  int    Sfc      Subframe index
**
** Outputs:
**
**
** Return value:
**
**  PFDEF       Pitch postfilter parameters: PF.Gain    Pitch Postfilter gain
**                                           PF.ScGn    Pitch Postfilter scaling gain
**                                           PF.Indx    Pitch postfilter lag
*/
PFDEF Comp_Lpf(FLOAT *Buff, int Olp, int Sfc)
{
    PFDEF  Pf;
    FLOAT  Lcr[5];
    int    Bindx, Findx;
    FLOAT  Acc0,Acc1;

    /*  Initialize  */

    Pf.Indx = 0;
    Pf.Gain = (FLOAT)0.0;
    Pf.ScGn = (FLOAT)1.0;

    /*  Find both indices */

    Bindx = Find_B(Buff, Olp, Sfc);
    Findx = Find_F(Buff, Olp, Sfc);

    /*  Combine the results */

    if ((Bindx==0) && (Findx==0))
        return Pf;

    /*  Compute target energy  */

    Lcr[0] = DotProd(&Buff[PitchMax+Sfc*SubFrLen],
                    &Buff[PitchMax+Sfc*SubFrLen],SubFrLen);

    if (Bindx != 0)
    {
        Lcr[1] = DotProd(&Buff[PitchMax+Sfc*SubFrLen],
                        &Buff[PitchMax+Sfc*SubFrLen+Bindx],SubFrLen);
        Lcr[2] = DotProd(&Buff[PitchMax+Sfc*SubFrLen+Bindx],
                        &Buff[PitchMax+Sfc*SubFrLen+Bindx],SubFrLen);
    }

    if (Findx != 0)
    {
        Lcr[3] = DotProd(&Buff[PitchMax+Sfc*SubFrLen],
                        &Buff[PitchMax+Sfc*SubFrLen+Findx],SubFrLen);
        Lcr[4] = DotProd(&Buff[PitchMax+Sfc*SubFrLen+Findx],
                        &Buff[PitchMax+Sfc*SubFrLen+Findx],SubFrLen);
    }

    /*  Select the best pair  */

    if ((Bindx != 0) && (Findx == 0))
        Pf = Get_Ind(Bindx, Lcr[0], Lcr[1], Lcr[2]);

    if ((Bindx == 0) && (Findx != 0))
        Pf = Get_Ind(Findx, Lcr[0], Lcr[3], Lcr[4]);

    if ((Bindx != 0) && (Findx != 0))
    {
        Acc0 = Lcr[4] * Lcr[1] * Lcr[1];
        Acc1 = Lcr[2] * Lcr[3] * Lcr[3];
        if (Acc0 > Acc1)
            Pf = Get_Ind(Bindx, Lcr[0], Lcr[1], Lcr[2]);
        else
            Pf = Get_Ind(Findx, Lcr[0], Lcr[3], Lcr[4]);
    }

    return Pf;
}

/*
**
** Function:        Find_B()
**
** Description:     Computes best pitch postfilter backward lag by
**                  backward cross correlation maximization around the
**                  decoded pitch lag
**                  of the subframe 0 (for subrames 0 & 1)
**                  of the subframe 2 (for subrames 2 & 3)
**
** Links to text:   Section 3.6
**
** Arguments:
**
**  FLOAT  *Buff    decoded excitation
**  int    Olp      Decoded pitch lag
**  int    Sfc      Subframe index
**
** Outputs:     None
**
** Return value:
**
**  Word16   Pitch postfilter backward lag
*/
int  Find_B(FLOAT *Buff, int Olp, int Sfc)
{
    int  i;

    int  Indx = 0;

    FLOAT  Acc0,Acc1;

    if (Olp > (PitchMax-3))
        Olp = (PitchMax-3);

    Acc1 = (FLOAT)0.0;

    for (i=Olp-3; i<=Olp+3; i++)
    {
        Acc0 = DotProd(&Buff[PitchMax+Sfc*SubFrLen],
                    &Buff[PitchMax+Sfc*SubFrLen-i],SubFrLen);

        /* return index of largest cross correlation */

        if (Acc0 > Acc1)
        {
            Acc1 = Acc0;
            Indx = i;
        }
    }
    return -Indx;
}


/*
**
** Function:        Find_F()
**
** Description:     Computes best pitch postfilter forward lag by
**                  forward cross correlation maximization around the
**                  decoded pitch lag
**                  of the subframe 0 (for subrames 0 & 1)
**                  of the subframe 2 (for subrames 2 & 3)
**
** Links to text:   Section 3.6
**
** Arguments:
**
**  FLOAT  *Buff    decoded excitation
**  int    Olp      Decoded pitch lag
**  int    Sfc      Subframe index
**
** Outputs:     None
**
** Return value:
**
**  int       Pitch postfilter forward lag
*/
int  Find_F(FLOAT *Buff, int Olp, int Sfc)
{
    int    i;
    int    Indx = 0;
    FLOAT  Acc0,Acc1;

    if (Olp > (PitchMax-3))
        Olp = (PitchMax-3);

    Acc1 = (FLOAT)0.0;

    for (i=Olp-3; i<=Olp+3; i++)
    {
        if (!((Sfc*SubFrLen+SubFrLen+i) > Frame))
        {
            Acc0 = DotProd(&Buff[PitchMax+Sfc*SubFrLen],
                            &Buff[PitchMax+Sfc*SubFrLen+i],SubFrLen);

            /* return index of largest cross correlation */

            if (Acc0 > Acc1)
            {
                Acc1 = Acc0;
                Indx = i;
            }
        }
    }

    return Indx;
}

/*
**
** Function:        Filt_Lpf()
**
** Description:     Applies the pitch postfilter for each subframe.
**
** Links to text:   Section 3.6
**
** Arguments:
**
**  FLOAT  *Tv      Pitch postfiltered excitation
**  FLOAT  *Buff    decoded excitation
**  PFDEF Pf        Pitch postfilter parameters
**  int    Sfc      Subframe index
**
** Outputs:
**
**  FLOAT  *Tv      Pitch postfiltered excitation
**
** Return value: None
**
*/
void  Filt_Lpf(FLOAT *Tv, FLOAT *Buff, PFDEF Pf, int Sfc)
{
    int  i;

    for (i = 0; i < SubFrLen; i++)
        Tv[Sfc*SubFrLen+i]= Buff[PitchMax+Sfc*SubFrLen+i]*Pf.ScGn +
                            Buff[PitchMax+Sfc*SubFrLen+Pf.Indx+i]*Pf.Gain;
}


/*
**
** Function:        Get_Ind()
**
** Description:     Computes gains of the pitch postfilter.
**                  The gains are calculated using the cross correlation
**                  (forward or backward, the one with the greatest contribution)
**                  and the energy of the signal. Also, a test is performed on
**                  the prediction gain to see whether the pitch postfilter
**                  should be used or not.
**
**
**
** Links to text:   Section 3.6
**
** Arguments:
**
**  int    Ind      Pitch postfilter lag
**  FLOAT  Ten      energy of the current subframe excitation vector
**  FLOAT  Ccr      Crosscorrelation of the excitation
**  FLOAT  Enr      Energy of the (backward or forward) "delayed" excitation
**
** Outputs:     None
**
** Return value:
**
**  PFDEF
**         int      Indx    Pitch postfilter lag
**         FLOAT    Gain    Pitch postfilter gain
**         FLOAT    ScGn    Pitch postfilter scaling gain
**
*/
PFDEF Get_Ind(int Ind, FLOAT Ten, FLOAT Ccr, FLOAT Enr)
{
    FLOAT  Acc0,Acc1;
    FLOAT  Exp;

    PFDEF Pf;


    Pf.Indx = Ind;

    /*  Check valid gain > 2db */

    Acc0 = (Ten * Enr)/(FLOAT)4.0;
    Acc1 = Ccr * Ccr;

    if (Acc1 > Acc0)
    {
        if (Ccr >= Enr)
            Pf.Gain = LpfConstTable[WrkRate];
        else
            Pf.Gain = (Ccr/Enr) * LpfConstTable[WrkRate];

        /*  Compute scaling gain  */

        Exp  = Ten + 2*Ccr*Pf.Gain + Pf.Gain*Pf.Gain*Enr;

        if (fabs(Exp) < (FLOAT) FLT_MIN)
            Pf.ScGn = (FLOAT)0.0;
        else
            Pf.ScGn = (FLOAT)sqrt(Ten/Exp);
    }
    else
    {
        Pf.Gain = (FLOAT)0.0;
        Pf.ScGn = (FLOAT)1.0;
    }

    Pf.Gain = Pf.Gain * Pf.ScGn;

    return Pf;
}

/*
**
** Function:        Comp_Info()
**
** Description:     Voiced/unvoiced classifier.
**                  It is based on a cross correlation maximization over the
**                  last 120 samples of the frame and with an index varying
**                  around the decoded pitch lag (from L-3 to L+3). Then the
**                  prediction gain is tested to declare the frame voiced or
**                  unvoiced.
**
** Links to text:   Section 3.10.2
**
** Arguments:
**
**  FLOAT  *Buff  decoded excitation
**  int    Olp    Decoded pitch lag
**  FLOAT  *Gain
**
** Outputs: None
**
** Return value:
**
**      Word16    Estimated pitch value
*/
Word16  Comp_Info(FLOAT *Buff, int Olp, FLOAT *Gain)
{
    int    i;
    FLOAT  Acc0;
    FLOAT  Tenr;
    FLOAT  Ccr,Enr;
    int    Indx;

    if (Olp > (PitchMax-3))
        Olp = (PitchMax-3);

    Indx = Olp;
    Ccr =  (FLOAT)0.0;

    for (i=Olp-3; i <= Olp+3; i++)
    {
        Acc0 = DotProd(&Buff[PitchMax+Frame-2*SubFrLen],
                        &Buff[PitchMax+Frame-2*SubFrLen-i],2*SubFrLen);

        if (Acc0 > Ccr)
        {
            Ccr = Acc0;
            Indx = i;
        }
    }

    /*  Compute target energy  */

    Tenr = DotProd(&Buff[PitchMax+Frame-2*SubFrLen],
                    &Buff[PitchMax+Frame-2*SubFrLen],2*SubFrLen);
    *Gain = Tenr;

    /*  Compute best energy */

    Enr = DotProd(&Buff[PitchMax+Frame-2*SubFrLen-Indx],
                    &Buff[PitchMax+Frame-2*SubFrLen-Indx],2*SubFrLen);

    if (Ccr <= (FLOAT)0.0)
        return 0;

    if (((((FLOAT)0.125)*Enr*Tenr) - (Ccr*Ccr)) < (FLOAT)0.0)
        return (Word16) Indx;

    return 0;
}


/*
**
** Function:        Regen()
**
** Description:     Performs residual interpolation depending of the frame
**                  classification.
**                  If the frame is previously declared unvoiced, the excitation
**                  is regenerated using a random number generator. Otherwise
**                  a periodic excitation is generated with the period previously
**                  found.
**
** Links to text:   Section 3.10.2
**
** Arguments:
**
**  FLOAT  *DataBuff current subframe decoded excitation
**  FLOAT  *Buff     past decoded excitation
**  Word16 Lag       Decoded pitch lag from previous frame
**  FLOAT  Gain      Interpolated gain from previous frames
**  int    Ecount    Number of erased frames
**  Word16 *Sd       Random number used in unvoiced cases
**
** Outputs:
**
**  Word16 *DataBuff current subframe decoded excitation
**  Word16 *Buff     updated past excitation
**
** Return value:    None
**
*/
void    Regen(FLOAT *DataBuff, FLOAT *Buff, Word16 Lag, FLOAT Gain,
              int Ecount, Word16 *Sd)
{
    int  i;

    /*  Test for clearing */

    if (Ecount >= ErrMaxNum)
    {
        for (i = 0; i < Frame; i++)
            DataBuff[i] = (FLOAT)0.0;
        for (i = 0; i < Frame+PitchMax; i++)
            Buff[i] = (FLOAT)0.0;
    }
    else
    {

        /*  Interpolate accordingly to the voicing estimation */

        if (Lag != 0)
        {
            /*  Voiced case */
            for (i = 0; i < Frame; i++)
                Buff[PitchMax+i] = Buff[PitchMax-Lag+i];
            for (i = 0; i < Frame; i++)
                DataBuff[i] = Buff[PitchMax+i] = Buff[PitchMax+i] * (FLOAT)0.75;
        }
        else
        {

            /* Unvoiced case */

            for (i = 0; i < Frame; i++)
                DataBuff[i] = Gain*(FLOAT)Rand_lbc(Sd)*((FLOAT)1.0/(FLOAT)32768.0);

            /* Clear buffer to reset memory */

            for (i = 0; i < Frame+PitchMax; i++)
                Buff[i] = (FLOAT)0.0;
        }
    }
}
