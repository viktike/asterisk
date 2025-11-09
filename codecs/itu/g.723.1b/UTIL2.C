/*
**
** File:    util2.c
**
** Description: utility functions for the lbc codec
**
** Functions:
**
**  I/O functions:
**
**      Read_lbc()
**      Write_lbc()
**
**  High-pass filtering:
**
**      Rem_Dc()
**
**  Miscellaneous signal processing functions:
**
**      Vec_Norm()
**      Mem_Shift()
**      Scale()
**
**  Bit stream packing/unpacking:
**
**      Line_Pack()
**      Line_Unpk()
**
**  Mathematical functions:
**
**      Rand_lbc()
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
#include "lbccode2.h"
#include "coder2.h"
#include "decod2.h"
#include "util2.h"

/*
**
** Function:        Read_lbc()
**
** Description:     Read in a file
**
** Links to text:   Sections 2.2 & 4
**
** Arguments:
**
**  FLOAT  *Dpnt
**  int     Len
**  FILE *Fp
**
** Outputs:
**
**  FLOAT  *Dpnt
**
** Return value:    None
**
*/
void  Read_lbc (FLOAT *Dpnt, int Len, FILE *Fp)
{
    Word16 Ibuf[Frame];
    int    i,n;

    n = fread (Ibuf, sizeof(Word16), Len, Fp);
    for (i=0; i<n; i++)
        Dpnt[i] = (FLOAT) Ibuf[i];
    for (i=n; i<Len; i++)
        Dpnt[i] = (FLOAT)0.0;
}

/*
**
** Function:        Write_lbc()
**
** Description:     Write a file
**
** Links to text:   Section
**
** Arguments:
**
**  FLOAT  *Dpnt
**  int     Len
**  FILE *Fp
**
** Outputs:         None
**
** Return value:    None
**
*/
void  Write_lbc(FLOAT *Dpnt, int Len, FILE *Fp)
{
    Word16 Obuf[Frame];
    int    i;

    for (i=0; i<Len; i++)
    {
        if (Dpnt[i] < (FLOAT)-32767.5)
            Obuf[i] = -32768L;
        else if (Dpnt[i] > (FLOAT)32766.5)
            Obuf[i] = 32767;
        else
        {
            if (Dpnt[i] < 0)
                Obuf[i] = (Word16) (Dpnt[i]-(FLOAT)0.5);
            else
                Obuf[i] = (Word16) (Dpnt[i]+(FLOAT)0.5);
        }
    }
    fwrite(Obuf, sizeof(Word16), Len, Fp);
}

void    Line_Wr(char *Line, FILE *Fp)
{
    Word16    Info;
    int       Size;

    Info = (Word16) (Line[0] & 0x0003);

    /* Check frame type and rate information */
    switch (Info) {

        case 0x0002 : {  /* SID frame */
            Size = 4;
            break;
        }

        case 0x0003 : {  /* untransmitted silence frame */
            Size = 1;
            break;
        }

        case 0x0001 : {  /* active frame, low rate */
            Size = 20;
            break;
        }

        default : {      /* active frame, high rate */
            Size = 24;
        }
    }

    fwrite(Line, Size, 1, Fp);
}

int    Line_Rd(char *Line, FILE *Fp)
{
    Word16    Info;
    int       Size;

    if (fread(Line, 1,1, Fp) != 1)
        return (-1);

    Info = (Word16) (Line[0] & 0x0003);

    /* Check frame type and rate information */
    switch (Info) {

        /* Active frame, high rate */
        case 0 : {
            Size = 23;
            break;
        }

        /* Active frame, low rate */
        case 1 : {
            Size = 19;
            break;
        }

        /* Sid Frame */
        case 2 : {
            Size = 3;
            break;
        }

        /* untransmitted */
        default : {
            return 0;
        }
    }

    fread(&Line[1], Size, 1, Fp);
    return 0;
}

/*
**
** Function:        Rem_Dc()
**
** Description:     High-pass filtering
**
** Links to text:   Section 2.3
**
** Arguments:
**
**  FLOAT  *Dpnt
**
** Inputs:
**
**  CodStat.HpfZdl  FIR filter memory from previous frame (1 word)
**  CodStat.HpfPdl  IIR filter memory from previous frame (1 word)
**
** Outputs:
**
**  FLOAT  *Dpnt
**
** Return value:    None
**
*/

void  Rem_Dc(FLOAT *Dpnt)
{
    int   i;
    FLOAT acc0;

    if (UseHp)
    {
        for (i=0; i < Frame; i++)
        {
            acc0 = Dpnt[i] - CodStat.HpfZdl;
            CodStat.HpfZdl = Dpnt[i];

            Dpnt[i] = CodStat.HpfPdl =
                            acc0 + CodStat.HpfPdl*((FLOAT)127.0/(FLOAT)128.0);
        }
    }
}


/*
**
** Function:        Mem_Shift()
**
** Description:     Memory shift, update of the high-passed input speech signal
**
** Links to text:
**
** Arguments:
**
**  FLOAT *PrevDat
**  FLOAT *DataBuff
**
** Outputs:
**
**  FLOAT *PrevDat
**  FLOAT *DataBuff
**
** Return value:    None
**
*/

void  Mem_Shift(FLOAT *PrevDat, FLOAT *DataBuff)
{
    int  i;

    FLOAT Dpnt[Frame+LpcFrame-SubFrLen];

    /*  Form Buffer  */

    for (i=0; i < LpcFrame-SubFrLen; i++)
        Dpnt[i] = PrevDat[i];
    for (i=0; i < Frame; i++)
        Dpnt[i+LpcFrame-SubFrLen] = DataBuff[i];

    /*  Update PrevDat  */

    for (i=0; i < LpcFrame-SubFrLen; i++)
        PrevDat[i] = Dpnt[Frame+i];

    /*  Update DataBuff  */

    for (i=0; i < Frame; i++)
        DataBuff[i] = Dpnt[(LpcFrame-SubFrLen)/2+i];
}

/*
**
** Function:        Line_Pack()
**
** Description:     Packing coded parameters in bitstream of 16-bit words
**
** Links to text:   Section 4
**
** Arguments:
**
**  LINEDEF *Line     Coded parameters for a frame
**  char    *Vout     bitstream chars
**  Word16   Ftyp     Voice Activity Indicator
**
** Outputs:
**
**  Word16 *Vout
**
** Return value:    None
**
*/
void    Line_Pack(LINEDEF *Line, char *Vout, Word16 Ftyp)
{
    int       i;
    int       BitCount;

    Word16    BitStream[192];
    Word16    *Bsp = BitStream;
    Word32    Temp;

    /* Clear the output vector */
    for ( i = 0 ; i < 24 ; i ++ )
        Vout[i] = 0 ;

 /*
  * Add the coder rate info and frame type info to the 2 msb
  * of the first word of the frame.
  * The signaling is as follows:
  *     Ftyp  WrkRate => X1X0
  *       1     Rate63     00  :   High Rate
  *       1     Rate53     01  :   Low  Rate
  *       2       x        10  :   Silence Insertion Descriptor frame
  *       0       x        11  :   Used only for simulation of
  *                                 untransmitted silence frames
  */
    switch (Ftyp) {

        case 0 : {
            Temp = 0x00000003L;
            break;
        }

        case 2 : {
            Temp = 0x00000002L;
            break;
        }

        default : {
            if ( WrkRate == Rate63 )
                Temp = 0x00000000L;
            else
                Temp = 0x00000001L;
            break;
        }
    }

    /* Serialize Control info */
    Bsp = Par2Ser( Temp, Bsp, 2 ) ;

    /* Check for Speech/NonSpeech case */
    if ( Ftyp == 1 ) {

        /* 24 bit LspId */
        Temp = (*Line).LspId ;
        Bsp = Par2Ser( Temp, Bsp, 24 ) ;

        /*
         *  Do the part common to both rates
         */

        /* Adaptive code book lags */
        Temp = (Word32) (*Line).Olp[0] - (Word32) PitchMin ;
        Bsp = Par2Ser( Temp, Bsp, 7 ) ;

        Temp = (Word32) (*Line).Sfs[1].AcLg ;
        Bsp = Par2Ser( Temp, Bsp, 2 ) ;

        Temp = (Word32) (*Line).Olp[1] - (Word32) PitchMin ;
        Bsp = Par2Ser( Temp, Bsp, 7 ) ;

        Temp = (Word32) (*Line).Sfs[3].AcLg ;
        Bsp = Par2Ser( Temp, Bsp, 2 ) ;

        /* Write combined 12 bit index of all the gains */
        for ( i = 0 ; i < SubFrames ; i ++ ) {
            Temp = (*Line).Sfs[i].AcGn*NumOfGainLev + (*Line).Sfs[i].Mamp ;
            if ( WrkRate == Rate63 )
                Temp += (Word32) (*Line).Sfs[i].Tran << 11 ;
            Bsp = Par2Ser( Temp, Bsp, 12 ) ;
        }

        /* Write all the Grid indices */
        for ( i = 0 ; i < SubFrames ; i ++ )
            *Bsp ++ = (Word16)(*Line).Sfs[i].Grid ;

        /* High rate only part */
        if ( WrkRate == Rate63 ) {

            /* Write the reserved bit as 0 */
            *Bsp ++ = (Word16) 0 ;

            /* Write 13 bit combined position index */
            Temp = (*Line).Sfs[0].Ppos >> 16 ;
            Temp = Temp * 9 + ( (*Line).Sfs[1].Ppos >> 14) ;
            Temp *= 90 ;
            Temp += ((*Line).Sfs[2].Ppos >> 16) * 9 + ( (*Line).Sfs[3].Ppos >> 14 ) ;
            Bsp = Par2Ser( Temp, Bsp, 13 ) ;

            /* Write all the pulse positions */
            Temp = (*Line).Sfs[0].Ppos & 0x0000ffffL ;
            Bsp = Par2Ser( Temp, Bsp, 16 ) ;

            Temp = (*Line).Sfs[1].Ppos & 0x00003fffL ;
            Bsp = Par2Ser( Temp, Bsp, 14 ) ;

            Temp = (*Line).Sfs[2].Ppos & 0x0000ffffL ;
            Bsp = Par2Ser( Temp, Bsp, 16 ) ;

            Temp = (*Line).Sfs[3].Ppos & 0x00003fffL ;
            Bsp = Par2Ser( Temp, Bsp, 14 ) ;

            /* Write pulse amplitudes */
            Temp = (Word32) (*Line).Sfs[0].Pamp ;
            Bsp = Par2Ser( Temp, Bsp, 6 ) ;

            Temp = (Word32) (*Line).Sfs[1].Pamp ;
            Bsp = Par2Ser( Temp, Bsp, 5 ) ;

            Temp = (Word32) (*Line).Sfs[2].Pamp ;
            Bsp = Par2Ser( Temp, Bsp, 6 ) ;

            Temp = (Word32) (*Line).Sfs[3].Pamp ;
            Bsp = Par2Ser( Temp, Bsp, 5 ) ;
        }
        /* Low rate only part */
        else {

            /* Write 12 bits of positions */
            for ( i = 0 ; i < SubFrames ; i ++ ) {
                Temp = (*Line).Sfs[i].Ppos ;
                Bsp = Par2Ser( Temp, Bsp, 12 ) ;
            }

            /* Write 4 bit Pamps */
            for ( i = 0 ; i < SubFrames ; i ++ ) {
                Temp = (*Line).Sfs[i].Pamp ;
                Bsp = Par2Ser( Temp, Bsp, 4 ) ;
            }
        }

    }
    else if (Ftyp == 2) {    /* SID frame */

        /* 24 bit LspId */
        Temp = (*Line).LspId ;
        Bsp = Par2Ser( Temp, Bsp, 24 ) ;

        /* Do Sid frame gain */
        Temp = (Word32)(*Line).Sfs[0].Mamp ;
        Bsp = Par2Ser( Temp, Bsp, 6 ) ;
    }

    /* Write out active frames */
    if (Ftyp == 1) {
        if ( WrkRate == Rate63 )
            BitCount = 192;
        else
            BitCount = 160;
    }
    /* Non active frames */
    else if (Ftyp == 2)
        BitCount = 32;
    else
        BitCount = 2;

    for ( i = 0 ; i < BitCount ; i ++ )
        Vout[i>>3] ^= BitStream[i] << (i & 0x0007) ;
}

Word16* Par2Ser( Word32 Inp, Word16 *Pnt, int BitNum )
{
    int     i;
    Word16  Temp ;

    for ( i = 0 ; i < BitNum ; i ++ ) {
        Temp = (Word16)(Inp & 0x0001);
        Inp >>= 1 ;
        *Pnt ++ = Temp ;
    }

    return Pnt ;
}

/*
**
** Function:        Line_Unpk()
**
** Description:     unpacking of bitstream, gets coding parameters for a frame
**
** Links to text:   Section 4
**
** Arguments:
**
**  char   *Vinp        bitstream chars
**  Word16 *Ftyp
**  Word16 Crc
**
** Outputs:
**
**  Word16 *Ftyp
**
** Return value:
**
**  LINEDEF             coded parameters
**     Word16   Crc
**     Word32   LspId
**     Word16   Olp[SubFrames/2]
**     SFSDEF   Sfs[SubFrames]
**
*/
LINEDEF  Line_Unpk(char *Vinp, Word16 *Ftyp, Word16 Crc)
{
    int     i  ;
    Word16  BitStream[192] ;
    Word16  *Bsp = BitStream ;
    LINEDEF Line ;
    Word32  Temp ;
    Word16  Info ;
    Word16  Bound_AcGn ;

    Line.Crc = Crc;
    if (Crc != 0)
        return Line;

    /* Unpack the byte info to BitStream vector */
    for ( i = 0 ; i < 192 ; i ++ )
        BitStream[i] = (Word16) (( Vinp[i>>3] >> (i & (Word16)0x0007) ) & 1);

    /* Decode the first two bits */
    Info = (Word16)Ser2Par( &Bsp, 2 ) ;

    if (Info == 3) {
        *Ftyp = 0;
        Line.LspId = 0L;    /* Dummy : to avoid Borland C3.1 warning */
        return Line;
    }

    /* Decode the LspId */
    Line.LspId = Ser2Par( &Bsp, 24 ) ;

    if (Info == 2) {
        /* Decode the Noise Gain */
        Line.Sfs[0].Mamp = (Word16)Ser2Par( &Bsp, 6);
        *Ftyp = 2;
        return Line ;
    }

    /*
     * Decode the common information to both rates
     */

    *Ftyp = 1;

    /* Decode the bit-rate */
    WrkRate = (Info == 0) ? Rate63 : Rate53;

    /* Decode the adaptive codebook lags */
    Temp = Ser2Par( &Bsp, 7 ) ;
    /* Test if forbidden code */
    if (Temp <= 123) {
        Line.Olp[0] = (Word16) Temp + (Word16)PitchMin ;
    }
    else {
        /* transmission error */
        Line.Crc = 1;
        return Line;
    }

    Line.Sfs[1].AcLg = (Word16) Ser2Par( &Bsp, 2 ) ;

    Temp = Ser2Par( &Bsp, 7 ) ;
    /* Test if forbidden code */
    if (Temp <= 123) {
        Line.Olp[1] = (Word16) Temp + (Word16)PitchMin ;
    }
    else {
        /* transmission error */
        Line.Crc = 1;
        return Line ;
    }

    Line.Sfs[3].AcLg = (Word16) Ser2Par( &Bsp, 2 ) ;

    Line.Sfs[0].AcLg = 1 ;
    Line.Sfs[2].AcLg = 1 ;

    /* Decode the combined gains accordingly to the rate */
    for ( i = 0 ; i < SubFrames ; i ++ ) {

        Temp = Ser2Par( &Bsp, 12 ) ;

        Line.Sfs[i].Tran = 0 ;

        Bound_AcGn = NbFilt170 ;
        if ( (WrkRate == Rate63) && (Line.Olp[i>>1] < (SubFrLen-2) ) ) {
            Line.Sfs[i].Tran = (Word16)(Temp >> 11) ;
            Temp &= 0x000007ffL ;
            Bound_AcGn = NbFilt085 ;
        }
        Line.Sfs[i].AcGn = (Word16)(Temp / (Word16)NumOfGainLev) ;

        if (Line.Sfs[i].AcGn < Bound_AcGn ) {
            Line.Sfs[i].Mamp = (Word16)(Temp % (Word16)NumOfGainLev) ;
        }
        else {
            /* error detected */
            Line.Crc = 1;
            return Line ;
        }
    }

    /* Decode the grids */
    for ( i = 0 ; i < SubFrames ; i ++ )
        Line.Sfs[i].Grid = *Bsp ++ ;

    if (Info == 0) {

        /* Skip the reserved bit */
        Bsp ++ ;

        /* Decode 13 bit combined position index */
        Temp = Ser2Par( &Bsp, 13 ) ;
        Line.Sfs[0].Ppos = ( Temp/90 ) / 9 ;
        Line.Sfs[1].Ppos = ( Temp/90 ) % 9 ;
        Line.Sfs[2].Ppos = ( Temp%90 ) / 9 ;
        Line.Sfs[3].Ppos = ( Temp%90 ) % 9 ;

        /* Decode all the pulse positions */
        Line.Sfs[0].Ppos = ( Line.Sfs[0].Ppos << 16 ) + Ser2Par( &Bsp, 16 ) ;
        Line.Sfs[1].Ppos = ( Line.Sfs[1].Ppos << 14 ) + Ser2Par( &Bsp, 14 ) ;
        Line.Sfs[2].Ppos = ( Line.Sfs[2].Ppos << 16 ) + Ser2Par( &Bsp, 16 ) ;
        Line.Sfs[3].Ppos = ( Line.Sfs[3].Ppos << 14 ) + Ser2Par( &Bsp, 14 ) ;

        /* Decode pulse amplitudes */
        Line.Sfs[0].Pamp = (Word16)Ser2Par( &Bsp, 6 ) ;
        Line.Sfs[1].Pamp = (Word16)Ser2Par( &Bsp, 5 ) ;
        Line.Sfs[2].Pamp = (Word16)Ser2Par( &Bsp, 6 ) ;
        Line.Sfs[3].Pamp = (Word16)Ser2Par( &Bsp, 5 ) ;
    }
    else {

        /* Decode the positions */
        for ( i = 0 ; i < SubFrames ; i ++ )
            Line.Sfs[i].Ppos = Ser2Par( &Bsp, 12 ) ;

        /* Decode the amplitudes */
        for ( i = 0 ; i < SubFrames ; i ++ )
            Line.Sfs[i].Pamp = (Word16)Ser2Par( &Bsp, 4 ) ;
    }
    return Line;
}

Word32  Ser2Par( Word16 **Pnt, int Count )
{
    int     i;
    Word32  Rez = 0L;

    for ( i = 0 ; i < Count ; i ++ ) {
        Rez += (Word32) **Pnt << i ;
        (*Pnt) ++ ;
    }
    return Rez ;
}


/*
**
** Function:        Rand_lbc()
**
** Description:     Generator of random numbers
**
** Links to text:   Section 3.10.2
**
** Arguments:
**
**  Word16   *p
**
** Outputs:
**
**  Word16   *p
**
** Return value:
**
**  Word16    random number
**
*/
Word16 Rand_lbc(Word16 *p)
{
    *p = (Word16)(((*p)*521L + 259) & 0x0000ffff);
    return(*p);
}


/*
**
** Function:        Scale()
**
** Description:     Postfilter gain scaling
**
** Links to text:   Section 3.9
**
** Arguments:
**
**  FLOAT *Tv
**  FLOAT  Sen
**
**  Inputs:
**
**  FLOAT DecStat.Gain
**
** Outputs:
**
**  FLOAT *Tv
**
** Return value:    None
**
*/
void  Scale(FLOAT *Tv, FLOAT Sen)
{
    int  i;

    FLOAT Acc1;
    FLOAT SfGain;

    Acc1 = DotProd(Tv,Tv,SubFrLen);

    if (Acc1 > (FLOAT) FLT_MIN)
        SfGain = (FLOAT) sqrt(Sen/Acc1) * (FLOAT)0.0625;
    else
        SfGain = (FLOAT)0.0625;

    /*
     * Update gain and scale the Postfiltered Signal
     */
    for (i=0; i < SubFrLen; i++)
    {
        DecStat.Gain = (FLOAT)0.9375*DecStat.Gain + SfGain;
        Tv[i] = (FLOAT)1.0625*Tv[i]*DecStat.Gain;
    }
}

/*
**
** Function:        DotProd()
**
** Description:     Dot product
**
** Links to text:   Section 3.9
**
** Arguments:
**
**  FLOAT *in1
**  FLOAT *in2
**  int   len
**
**  Inputs:
**
**  Outputs:
**
**  Return value:
**
**  FLOAT dot product
**
*/
FLOAT DotProd(FLOAT *in1, FLOAT *in2, int len)
{
    int   i;
    FLOAT sum;

    sum = (FLOAT)0.0;
    for (i=0; i<len; i++)
        sum += in1[i]*in2[i];

    return(sum);
}
