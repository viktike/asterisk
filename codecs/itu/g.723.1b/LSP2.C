/*
**
** File:    lsp2.c
**
** Description: Functions that implement line spectral pair
**      (LSP) operations.
**
** Functions:
**
**  Converting between linear predictive coding (LPC) coefficients
**  and LSP frequencies:
**
**      AtoLsp()
**      LsptoA()
**
**  Vector quantization (VQ) of LSP frequencies:
**
**      Lsp_Qnt()
**      Lsp_Svq()
**      Lsp_Inq()
**
**  Interpolation of LSP frequencies:
**
**      Lsp_Int()
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

#include "typedef2.h"
#include "cst2.h"
#include "tab2.h"
#include "util2.h"
#include "lsp2.h"

/*
**
** Function:        Polynomial
**
** Description:     Sub-function of AtoLsp().  Evaluates the polynomial once.
*/

FLOAT Polynomial(FLOAT *Lpq, int CosPtr)
{
    FLOAT  Ret;
    int    j;

    Ret = (FLOAT)0.0;
    for (j=0; j<=LpcOrder/2; j++)
        Ret += Lpq[LpcOrder-2*j]*CosineTable[(CosPtr*j) % CosineTableSize];

    return(Ret);
}


/*
**
** Function:            AtoLsp()
**
** Description:     Transforms 10 LPC coefficients to the 10
**          corresponding LSP frequencies for a subframe.
**          This transformation is done once per frame,
**          for subframe 3 only.  The transform algorithm
**          generates sum and difference polynomials from
**          the LPC coefficients.  It then evaluates the
**          sum and difference polynomials at uniform
**          intervals of pi/256 along the unit circle.
**          Intervals where a sign change occurs are
**          interpolated to find the zeros of the
**          polynomials, which are the LSP frequencies.
**
** Links to text:   Section 2.5
**
** Arguments:
**
**  FLOAT  *LspVect     Empty Buffer
**  FLOAT  Lpc[]        Unquantized LPC coefficients (10 words)
**  FLOAT  PrevLsp[]    LSP frequencies from the previous frame (10 words)
**
** Outputs:
**
**  FLOAT  LspVect[]    LSP frequencies for the current frame (10 words)
**
** Return value:        None
**
**/
void  AtoLsp(FLOAT *LspVect, FLOAT *Lpc, FLOAT *PrevLsp)
{
    int    i,j,k;
    int    LspCnt;
    FLOAT  Lpq[LpcOrder+2];
    FLOAT  PrevVal,CurrVal,AbsPrev,AbsCurr;

    /*
     * Perform a bandwidth expansion on the LPC coefficients.  This
     * scales the poles of the LPC synthesis filter by a factor of
     * 0.994.
     */

    for (i=0; i < LpcOrder; i++)
        LspVect[i] = Lpc[i]*BandExpTable[i];

    /* This loop computes the coefficients of P(z) and Q(z).  The long
     * division (to remove the real zeros) is done recursively.
     */

    Lpq[0] = Lpq[1] = (FLOAT)1.0;

    for (i=0; i < LpcOrder/2; i++)
    {
        Lpq[2*i+2] = -Lpq[2*i+0] - LspVect[i] - LspVect[LpcOrder-1-i];
        Lpq[2*i+3] =  Lpq[2*i+1] - LspVect[i] + LspVect[LpcOrder-1-i];
    }
    Lpq[LpcOrder+0] *= (FLOAT)0.5;
    Lpq[LpcOrder+1] *= (FLOAT)0.5;

    /* Do first evaluation */

    k = 0;
    LspCnt = 0;
    PrevVal = Polynomial(Lpq,0);

    /*
     * Search loop.  Evaluate P(z) and Q(z) at uniform intervals of
     * pi/256 along the unit circle.  Check for zero crossings.  The
     * zeros of P(w) and Q(w) alternate, so only one of them need by
     * evaluated at any given step.
     */

    for (i=1; i < CosineTableSize/2; i++)
    {
        /* Evaluate the polynomial */

        CurrVal = Polynomial(&Lpq[k],i);

        /* Test for sign change indicating a zero crossing */

        if (CurrVal*PrevVal < 0)
        {
            AbsPrev = (FLOAT)fabs(PrevVal);
            AbsCurr = (FLOAT)fabs(CurrVal);

            LspVect[LspCnt++] = (i-1 + AbsPrev/(AbsPrev+AbsCurr));

            /* Check if all found */

            if (LspCnt == LpcOrder)
                break;

            /* Switch the pointer, evaluate again */

            k ^= 1;
            CurrVal = Polynomial(&Lpq[k],i);
        }
        PrevVal = CurrVal;
    }

    /*
     *  Check if all 10 zeros were found.  If not, ignore the results of
     *  the search and use the previous frame's LSP frequencies instead.
     */

    if (LspCnt != LpcOrder)
    {
        for (j=0; j < LpcOrder; j++)
            LspVect[j] = PrevLsp[j];
    }
    return;
}


/*
**
** Function:            Lsp_Qnt()
**
** Description:     Vector quantizes the LSP frequencies.  The LSP
**          vector is divided into 3 sub-vectors, or
**          bands, of dimension 3, 3, and 4.  Each band is
**          quantized separately using a different VQ
**          table.  Each table has 256 entries, so the
**          quantization generates three indices of 8 bits
**          each.  (Only the LSP vector for subframe 3 is
**          quantized per frame.)
**
** Links to text:   Section 2.5
**
** Arguments:
**
**  FLOAT CurrLsp[]    Unquantized LSP frequencies (10) for the current frame
**  FLOAT PrevLsp[]    LSP frequencies (10) from the previous frame
**
** Outputs:            Quantized LSP frequencies (10) for the current frame
**
** Return value:
**
**  Word32  Long word packed with the 3 VQ indices.  Band 0
**          corresponds to bits [23:16], band 1 corresponds
**          to bits [15:8], and band 2 corresponds to bits [7:0].
**          (Bit 0 is the least significant.)
**
*/
Word32 Lsp_Qnt(FLOAT *CurrLsp, FLOAT *PrevLsp)
{
    int   i;

    FLOAT Wvect[LpcOrder];
    FLOAT Min,Tmp;

    /*
     * Compute the VQ weighting vector.  The weights assign greater
     * precision to those frequencies that are closer together.
     */

    /* Compute the end differences */

    Wvect[0] = ((FLOAT)1.0)/(CurrLsp[1] - CurrLsp[0]);
    Wvect[LpcOrder-1] = ((FLOAT)1.0)/(CurrLsp[LpcOrder-1] - CurrLsp[LpcOrder-2]);

    /* Compute the rest of the differences */

    for (i=1; i < LpcOrder-1; i++)
    {
        Min = CurrLsp[i+1] - CurrLsp[i];
        Tmp = CurrLsp[i] - CurrLsp[i-1];

        if (Tmp < Min)
            Min = Tmp;

        if (Min > (FLOAT)0.0)
            Wvect[i] = ((FLOAT)1.0)/Min;
        else
            Wvect[i] = (FLOAT)1.0;
    }

    /* Generate the prediction vector and subtract it.  Use a constant
     * first-order predictor based on the previous (DC-free) LSP vector.
     */

    for (i=0; i < LpcOrder; i++)
        CurrLsp[i] = (CurrLsp[i] - LspDcTable[i]) -
                        LspPred0*(PrevLsp[i] - LspDcTable[i]);

    /* Do the vector quantization for all three bands */

    return Lsp_Svq(CurrLsp, Wvect);
}


/*
**
** Function:            Lsp_Svq()
**
** Description:     Performs the search of the VQ tables to find
**          the optimum LSP indices for all three bands.
**          For each band, the search finds the index which
**          minimizes the weighted squared error between
**          the table entry and the target.
**
** Links to text:   Section 2.5
**
** Arguments:
**
**  FLOAT  Tv[]     VQ target vector (10 words)
**  FLOAT  Wvect[]      VQ weight vector (10 words)
**
** Outputs:         None
**
** Return value:
**
**  Word32  Long word packed with the 3 VQ indices.  Band 0
**          corresponds to bits [23:16], band 1 corresponds
**          to bits [15:8], and band 2 corresponds to bits [7:0].
**
*/
Word32  Lsp_Svq(FLOAT *Lsp, FLOAT *Wvect)
{
    int  i,j,k;

    Word32 Rez;
    Word32 Indx;
    int    Start,Dim;
    FLOAT  Tmp[LpcOrder];
    FLOAT *LspQntPnt;
    FLOAT  Max,Err;

    /*  Initialize the return value  */

    Rez = (Word32) 0;

    /*  Quantize each band separately  */

    for (k=0; k < LspQntBands; k++)
    {

        /*
         * Search over the entire VQ table to find the index that
         * minimizes the error.
         */

        /* Initialize the search */

        Max = (FLOAT)-1.0;
        Indx = 0;
        LspQntPnt = BandQntTable[k];
        Start = BandInfoTable[k][0];
        Dim = BandInfoTable[k][1];

        for (i=0; i < LspCbSize; i++)
        {
            /* Generate weighted vector */

            for (j=0; j<Dim; j++)
                Tmp[j] = Wvect[Start+j]*LspQntPnt[j];

            /* Compute the Error */

            Err = ((FLOAT)2.0)*DotProd(&Lsp[Start],Tmp,Dim) -
                    DotProd(LspQntPnt,Tmp,Dim);
            LspQntPnt += BandInfoTable[k][1];

            if (Err > Max)
            {
                Max = Err;
                Indx = (Word32) i;
            }
        }
        Rez = (Rez << 8) | Indx;
    }

    return Rez;
}


/*
**
** Function:            Lsp_Inq()
**
** Description:     Performs inverse vector quantization of the
**          LSP frequencies.  The LSP vector is divided
**          into 3 sub-vectors, or bands, of dimension 3,
**          3, and 4.  Each band is inverse quantized
**          separately using a different VQ table.  Each
**          table has 256 entries, so each VQ index is 8
**          bits.  (Only the LSP vector for subframe 3 is
**          quantized per frame.)
**
** Links to text:   Sections 2.6, 3.2
**
** Arguments:
**
**  FLOAT  *Lsp     Empty buffer
**  FLOAT  PrevLsp[]    Quantized LSP frequencies from the previous frame
**               (10 words)
**  Word32 LspId        Long word packed with the 3 VQ indices.  Band 0
**               corresponds to bits [23:16], band 1 corresponds
**               to bits [15:8], and band 2 corresponds to bits
**               [7:0].
**  Word16 Crc      Frame erasure indicator
**
** Outputs:
**
**  FLOAT  Lsp[]        Quantized LSP frequencies for current frame (10
**               words)
**
** Return value:         None
**
*/
void  Lsp_Inq(FLOAT *Lsp, FLOAT *PrevLsp, Word32 LspId, Word16 Crc)
{
    int    i,j;

    FLOAT *LspQntPnt;
    FLOAT  Lprd,Scon,Tmpf;
    int    Tmp;
    Flag   Test;

    /*
     * Check for frame erasure.  If a frame erasure has occurred, the
     * resulting VQ table entries are zero.  In addition, a different
     * fixed predictor and minimum frequency separation are used.
     */
    if (Crc == 0)
    {
        Scon = (FLOAT)2.0;
        Lprd = LspPred0;
    }
    else
    {
        LspId = (Word32) 0;
        Scon = (FLOAT)4.0;
        Lprd = LspPred1;
    }

    /*
     * Inverse quantize the 10th-order LSP vector.  Each band is done
     * separately.
     */

    for (i=LspQntBands-1; i >= 0; i--)
    {
        /* Get the VQ table entry corresponding to the transmitted index */
        Tmp = (int)(LspId & (Word32) 0x000000ff);
        LspId >>= 8;

        LspQntPnt = BandQntTable[i];

        for (j=0; j < BandInfoTable[i][1]; j++)
            Lsp[BandInfoTable[i][0] + j] = LspQntPnt[Tmp*BandInfoTable[i][1] + j];
    }

    /* Add predicted vector and DC to decoded vector */

    for (j=0; j < LpcOrder; j++)
        Lsp[j] = Lsp[j] + (PrevLsp[j] - LspDcTable[j])*Lprd + LspDcTable[j];


    /*
     * Perform a stability test on the quantized LSP frequencies.  This
     * test checks that the frequencies are ordered, with a minimum
     * separation between each.  If the test fails, the frequencies are
     * iteratively modified until the test passes.  If after 10
     * iterations the test has not passed, the previous frame's
     * quantized LSP vector is used.
     */

    for (i=0; i < LpcOrder; i++)
    {

        /* Check the first frequency */
        if (Lsp[0] < (FLOAT)3.0)
            Lsp[0] = (FLOAT)3.0;

        /* Check the last frequency */
        if (Lsp[LpcOrder-1] > (FLOAT)252.0)
            Lsp[LpcOrder-1] = (FLOAT)252.0;

        /* Perform the modification */
        for (j=1; j < LpcOrder; j++)
        {
            Tmpf = Scon + Lsp[j-1] - Lsp[j];
            if (Tmpf > 0)
            {
                Tmpf *= (FLOAT)0.5;
                Lsp[j-1] -= Tmpf;
                Lsp[j] += Tmpf;
            }
        }

        /* Test the modified frequencies for stability.  Break out of
         * the loop if the frequencies are stable.
         */

        Test = False;
        for (j=1; j < LpcOrder; j++)
            if ((Lsp[j] - Lsp[j-1]) < (Scon - (FLOAT)0.03125))
                Test = True;

        if (Test == False)
            break;
    }

    /*
     * If the result of the stability check is True (not stable),
     * set Lsp to PrevLsp
     */

    if (Test == True)
    {
        for ( j = 0 ; j < LpcOrder ; j ++ )
            Lsp[j] = PrevLsp[j] ;
    }

    return;
}


/*
**
** Function:            Lsp_Int()
**
** Description:     Computes the quantized LPC coefficients for a
**          frame.  First the quantized LSP frequencies
**          for all subframes are computed by linear
**          interpolation.  These frequencies are then
**          transformed to quantized LPC coefficients.
**
** Links to text:   Sections 2.7, 3.3
**
** Arguments:
**
**  FLOAT  *QntLpc      Empty buffer
**  FLOAT  CurrLsp[]    Quantized LSP frequencies for the current frame,
**               subframe 3 (10 words)
**  FLOAT  PrevLsp[]    Quantized LSP frequencies for the previous frame,
**               subframe 3 (10 words)
**
** Outputs:
**
**  FLOAT  QntLpc[]     Quantized LPC coefficients for current frame, all
**               subframes (40 words)
**  FLOAT  PrevLsp[]    Quantized LSP frequencies for the current frame,
**               subframe 3.  For use in the next frame. (10 words)
**
** Return value:        None
**
*/
void  Lsp_Int(FLOAT *QntLpc, FLOAT *CurrLsp, FLOAT *PrevLsp)
{
    int    i,j;

    FLOAT  *Dpnt;
    FLOAT  Fac[4] = {(FLOAT)0.25, (FLOAT)0.5, (FLOAT)0.75, (FLOAT)1.0};

    Dpnt = QntLpc;
    for (i=0; i < SubFrames; i++) {

        /* Compute the quantized LSP frequencies by linear interpolation
         * of the frequencies from subframe 3 of the current and
         * previous frames
         */

        for (j=0; j < LpcOrder; j++)
            Dpnt[j] = ((FLOAT)1.0 - Fac[i])*PrevLsp[j] + Fac[i]*CurrLsp[j];

        /* Convert the quantized LSP frequencies to quantized LPC
         * coefficients
         */

        LsptoA(Dpnt);
        Dpnt += LpcOrder;
    }
}


/*
**
** Function:            LsptoA()
**
** Description:     Converts LSP frequencies to LPC coefficients
**          for a subframe.  Sum and difference
**          polynomials are computed from the LSP
**          frequencies (which are the roots of these
**          polynomials).  The LPC coefficients are then
**          computed by adding the sum and difference
**          polynomials.
**
** Links to text:   Sections 2.7, 3.3
**
** Arguments:
**
**  FLOAT  Lsp[]        LSP frequencies (10 words)
**
** Outputs:
**
**  FLOAT  Lsp[]        LPC coefficients (10 words)
**
** Return value:        None
**
*/
void  LsptoA(FLOAT *Lsp)
{
    int   i,j;

    FLOAT P[LpcOrder/2+1];
    FLOAT Q[LpcOrder/2+1];
    FLOAT Floor;
    FLOAT Fac[(LpcOrder/2)-2] = {(FLOAT)1.0,(FLOAT)0.5,(FLOAT)0.25};

    /*
     * Compute the cosines of the LSP frequencies by table lookup and
     * linear interpolation
     */

    for (i=0; i < LpcOrder; i++)
    {
        Floor = (FLOAT)floor(Lsp[i]);
        j = (int) Floor;
        Lsp[i] = -(CosineTable[j] +
        (CosineTable[j+1]-CosineTable[j])*(Lsp[i]-Floor));
    }

    /*
     * Compute the sum and difference polynomials with the real roots
     * removed.  These are computed by polynomial multiplication as
     * follows.  Let the sum polynomial be P(z).  Define the elementary
     * polynomials P_i(z) = 1 - 2cos(w_i) z^{-1} + z^{-2}, for 1<=i<=
     * 5, where {w_i} are the LSP frequencies corresponding to the sum
     * polynomial.  Then P(z) = P_1(z)P_2(z)...P_5(z).  Similarly
     * the difference polynomial Q(z) = Q_1(z)Q_2(z)...Q_5(z).
     */

    /* Init P and Q. */

    P[0] = (FLOAT)0.5;
    P[1] = Lsp[0] + Lsp[2];
    P[2] = (FLOAT)1.0 + ((FLOAT)2.0)*Lsp[0]*Lsp[2];

    Q[0] = (FLOAT)0.5;
    Q[1] = Lsp[1] + Lsp[3];
    Q[2] = (FLOAT)1.0 + ((FLOAT)2.0)*Lsp[1]*Lsp[3];

    /* Compute the intermediate polynomials P_1(z)P_2(z)...P_i(z) and
     * Q_1(z)Q_2(z)...Q_i(z), for i = 2, 3, 4.  Each intermediate
     * polynomial is symmetric, so only the coefficients up to i+1 need
     * by computed.  Scale by 1/2 each iteration for a total of 1/8.
     */

    for (i=2; i < LpcOrder/2; i++)
    {
        /* Compute coefficient (i+1) */

        P[i+1] = P[i-1] + P[i]*Lsp[2*i+0];
        Q[i+1] = Q[i-1] + Q[i]*Lsp[2*i+1];

        /* Compute coefficients i, i-1, ..., 2 */

        for (j=i; j >= 2; j--)
        {
            P[j] = P[j-1]*Lsp[2*i+0] + ((FLOAT)0.5)*(P[j]+P[j-2]);
            Q[j] = Q[j-1]*Lsp[2*i+1] + ((FLOAT)0.5)*(Q[j]+Q[j-2]);
        }

        /* Compute coefficients 1, 0 */

        P[0] = P[0]*((FLOAT)0.5);
        Q[0] = Q[0]*((FLOAT)0.5);

        P[1] = (P[1] + Lsp[2*i+0]*Fac[i-2])*((FLOAT)0.5);
        Q[1] = (Q[1] + Lsp[2*i+1]*Fac[i-2])*((FLOAT)0.5);
    }

    /*
     * Convert the sum and difference polynomials to LPC coefficients
     * The LPC polynomial is the sum of the sum and difference
     * polynomials with the real zeros factored in: A(z) = 1/2 {P(z) (1
     * + z^{-1}) + Q(z) (1 - z^{-1})}.  The LPC coefficients are scaled
     * here by 16; the overall scale factor for the LPC coefficients
     * returned by this function is therefore 1/4.
     */

    for (i=0; i < LpcOrder/2; i++)
    {
        Lsp[i] =            (-P[i] - P[i+1] + Q[i] - Q[i+1])*((FLOAT)8.0);
        Lsp[LpcOrder-1-i] = (-P[i] - P[i+1] - Q[i] + Q[i+1])*((FLOAT)8.0);
    }
}
