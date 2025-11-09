/*
**
** File:            "codcng2.c"
**
** Description:     Comfort noise generation
**                  performed at the encoder part
**
** Functions:       Init_Cod_Cng()
**                  Cod_Cng()
**                  Update_Acf()
**
** Local functions:
**                  ComputePastAvFilter()
**                  CalcRC()
**                  LpcDiff()
**
**
*/
/*
	ITU-T G.723.1 Software Package Release 2 (June 2006)

    ITU-T G.723.1 Floating Point Speech Coder ANSI C Source Code.  Version 5.2F
    copyright (c) 1995, AudioCodes, DSP Group, France Telecom,
    Universite de Sherbrooke.  All rights reserved.
	

    Floating-point code copyright (c) 1996,
    Intel Corporation and France Telecom (CNET).
    All rights reserved.

    Last mofified : March 2006
*/

#include <stdio.h>
#include <stdlib.h>

#include "typedef2.h"
#include "cst2.h"
#include "tab2.h"
#include "util2.h"
#include "lsp2.h"
#include "lpc2.h"
#include "utilcng2.h"
#include "codcng2.h"
#include "coder2.h"
#include "vad2.h"

/* Declaration of local functions */
static void ComputePastAvFilter(FLOAT *Coeff);
static void CalcRC(FLOAT *Coeff, FLOAT *RC);
static Flag LpcDiff(FLOAT *RC, FLOAT *ptrAcf, FLOAT alpha);

/* Global Variables */
CODCNGDEF CodCng;

/*
**
** Function:        Init_Cod_Cng()
**
** Description:     Initialize Cod_Cng static variables
**
** Links to text:
**
** Arguments:       None
**
** Outputs:         None
**
** Return value:    None
**
*/
void Init_Cod_Cng(void)
{
    int i;

    CodCng.CurGain = (FLOAT)0.0;

    for (i=0; i< SizAcf; i++)
        CodCng.Acf[i] = (FLOAT)0.0;

    for (i=0; i < LpcOrder; i++)
        CodCng.SidLpc[i] = (FLOAT)0.0;

    CodCng.PastFtyp = 1;

    CodCng.RandSeed = 12345;

    return;
}


/*
**
** Function:           Cod_Cng()
**
** Description:        Computes Ftyp for inactive frames
**                              0  :  for untransmitted frames
**                              2  :  for SID frames
**                     Computes current frame excitation
**                     Computes current frame LSPs
**                     Computes the coded parameters of SID frames
**
** Links to text:
**
** Arguments:
**
**  FLOAT   *DataExc   Current frame synthetic excitation
**  Word16  *Ftyp      Characterizes the frame type for CNG
**  LINEDEF *Line      Quantized parameters (used for SID frames)
**  FLOAT   *QntLpc    Interpolated frame LPC coefficients
**
** Outputs:
**
**  FLOAT   *DataExc
**  Word16  *Ftyp
**  LINEDEF *Line
**  FLOAT   *QntLpc
**
** Return value:       None
**
*/
void Cod_Cng(FLOAT *DataExc, Word16 *Ftyp, LINEDEF *Line, FLOAT *QntLpc)
{
    FLOAT   curCoeff[LpcOrder];
    Word16  curQGain;
    FLOAT   temp;
    int     i;

    /*
     * Update Ener
     */
    for (i=NbAvGain-1; i>=1; i--) {
        CodCng.Ener[i] = CodCng.Ener[i-1];
    }

    /*
     * Compute LPC filter of present frame
     */
    CodCng.Ener[0] = Durbin(curCoeff, &CodCng.Acf[1], CodCng.Acf[0], &temp);

    /*
     * if first frame of silence => SID frame
     */
    if (CodCng.PastFtyp == 1) {
        *Ftyp = 2;
        CodCng.NbEner = 1;
        curQGain = Qua_SidGain(CodCng.Ener, CodCng.NbEner);
    }

    else {
        CodCng.NbEner++;
        if (CodCng.NbEner > NbAvGain)
            CodCng.NbEner = NbAvGain;
        curQGain = Qua_SidGain(CodCng.Ener, CodCng.NbEner);

        /*
         * Compute stationarity of current filter
         * versus reference filter
         */
        if (LpcDiff(CodCng.RC, CodCng.Acf, *CodCng.Ener) == 0) {
            *Ftyp = 2;  /* transmit SID frame */
        }
        else {
            i = abs((int)(curQGain - CodCng.IRef));
            if (i > ThreshGain) {
                *Ftyp = 2;
            }
            else {
                /* no transmission */
                *Ftyp = 0;
            }
        }
    }

    /*
     * If SID frame : Compute SID filter
     */
    if (*Ftyp == 2) {

        /*
         * Evaluates local stationnarity :
         * Computes difference between current filter and past average filter
         * if signal not locally stationary SID filter = current filter
         * else SID filter = past average filter
         */

        /* Compute past average filter */
        ComputePastAvFilter(CodCng.SidLpc) ;

        /* If adaptation enabled, fill noise filter */
        if ( !VadStat.Aen ) {
            for (i = 0; i < LpcOrder; i++)
                VadStat.NLpc[i] = CodCng.SidLpc[i];
        }

        /* Compute autocorr. of past average filter coefficients */
        CalcRC(CodCng.SidLpc , CodCng.RC);

        if (LpcDiff(CodCng.RC, CodCng.Acf, *CodCng.Ener) == 0) {
            for (i=0; i<LpcOrder; i++) {
                CodCng.SidLpc[i] = curCoeff[i];
            }
            CalcRC(curCoeff, CodCng.RC);
        }

        /*
         * Compute SID frame codes
         */

        /* Compute LspSid */
        AtoLsp(CodCng.LspSid, CodCng.SidLpc, CodStat.PrevLsp);
        Line->LspId = Lsp_Qnt(CodCng.LspSid, CodStat.PrevLsp);
        Lsp_Inq(CodCng.LspSid, CodStat.PrevLsp, Line->LspId, 0);

        Line->Sfs[0].Mamp = curQGain;
        CodCng.IRef = curQGain;
        CodCng.SidGain = Dec_SidGain(CodCng.IRef);

    } /* end of Ftyp=2 case (SID frame) */

    /*
     * Compute new excitation
     */
    if (CodCng.PastFtyp == 1) {
        CodCng.CurGain = CodCng.SidGain;
    }
    else {
        CodCng.CurGain = (FLOAT)0.875 * CodCng.CurGain + (FLOAT)0.125 * CodCng.SidGain;
    }
    Calc_Exc_Rand(CodCng.CurGain, CodStat.PrevExc, DataExc,
                                                &CodCng.RandSeed, Line);

    /*
     * Interpolate LSPs and update PrevLsp
     */
    Lsp_Int(QntLpc, CodCng.LspSid, CodStat.PrevLsp);
    for (i=0; i < LpcOrder ; i++) {
        CodStat.PrevLsp[i] = CodCng.LspSid[i];
    }

    /*
     * Output & save frame type info
     */
    CodCng.PastFtyp = *Ftyp;
    return;
}

/*
**
** Function:           Update_Acf()
**
** Description:        Computes & Stores sums of subframe-acfs
**
** Links to text:
**
** Arguments:
**
**  FLOAT  *Acf_sf     sets of subframes Acfs of current frame
**
** Output :            None
**
** Return value:       None
**
*/
void Update_Acf(FLOAT *Acf_sf)
{
    int i, i_subfr;
    FLOAT *ptr1, *ptr2;

    /* Update Acf */
    ptr2 = CodCng.Acf + SizAcf;
    ptr1 = ptr2 - LpcOrderP1;
    for (i=LpcOrderP1; i<SizAcf; i++)
        *(--ptr2) = *(--ptr1);

    /* Compute current sum of acfs */
    for (i=0; i<= LpcOrder; i++)
        CodCng.Acf[i] = (FLOAT) 0.0;

    ptr2 = Acf_sf;
    for (i_subfr=0; i_subfr<SubFrames; i_subfr++) {
        for (i=0; i <= LpcOrder; i++) {
            CodCng.Acf[i] += *ptr2++;
        }
    }
}

/*
**
** Function:           ComputePastAvFilter()
**
** Description:        Computes past average filter
**
** Links to text:
**
** Argument:
**
**   FLOAT *Coeff      set of LPC coefficients
**
** Output:
**
**   FLOAT *Coeff
**
** Return value:       None
**
*/
static void ComputePastAvFilter(FLOAT *Coeff)
{
    int i, j;
    FLOAT *ptr_Acf;
    FLOAT sumAcf[LpcOrderP1];
    FLOAT temp;

    /* Compute sum of NbAvAcf frame-Acfs  */
    for (j=0; j <= LpcOrder; j++)
        sumAcf[j] = (FLOAT) 0.0;

    ptr_Acf = CodCng.Acf + LpcOrderP1;
    for (i=1; i <= NbAvAcf; i++) {
        for (j=0; j <= LpcOrder; j++) {
            sumAcf[j] += *ptr_Acf++;
        }
    }

    Durbin(Coeff, &sumAcf[1], sumAcf[0], &temp);

    return;
}

/*
**
** Function:           CalcRC()
**
** Description:        Computes function derived from
**                     the autocorrelation of LPC coefficients
**                     used for Itakura distance
**
** Links to text:
**
** Arguments :
**
**   FLOAT *Coeff      set of LPC coefficients
**   FLOAT *RC         derived from LPC coefficients autocorrelation
**
** Outputs :
**
**   FLOAT *RC
**
** Return value:       None
**
*/
static void CalcRC(FLOAT *Coeff, FLOAT *RC)
{
    int i, j;
    FLOAT temp;

    temp = (FLOAT) 1.0 + DotProd(Coeff, Coeff, LpcOrder);
    RC[0] = temp;

    for (i=1; i<=LpcOrder; i++) {
        temp = -Coeff[i-1];
        for (j=0; j<LpcOrder-i; j++) {
            temp += Coeff[j] * Coeff[j+i];
        }
        RC[i] = (FLOAT)2.0 * temp;
    }
    return;
}

/*
**
** Function:           LpcDiff()
**
** Description:        Comparison of two filters
**                     using Itakura distance
**                     1st filter : defined by *ptrAcf
**                     2nd filter : defined by *RC
**                     the autocorrelation of LPC coefficients
**                     used for Itakura distance
**
** Links to text:
**
** Arguments :
**
**   FLOAT *RC         derived from LPC coefficients autocorrelation
**   FLOAT *ptrAcf     pointer on signal autocorrelation function
**   FLOAT alpha       residual energy in LPC analysis using *ptrAcf
**
** Output:             None
**
** Return value:       flag = 1 if similar filters
**                     flag = 0 if different filters
**
*/
static Flag LpcDiff(FLOAT *RC, FLOAT *ptrAcf, FLOAT alpha)
{
    FLOAT temp0, temp1;
    Flag diff;

    temp0 = DotProd(RC, ptrAcf, LpcOrderP1);
    temp1 = alpha * FracThreshP1;

    if (temp0 <= temp1)	/* G723.1 Maintenance April 2006 */
    					/* Before : if (temp0 < temp1) */
        diff = 1;
    else
        diff = 0;
    return(diff);
}
