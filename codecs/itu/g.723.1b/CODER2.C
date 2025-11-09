/*
**
** File:        "coder2.c"
**
** Description:     Top-level source code for G.723.1 dual-rate coder
**
** Functions:       Init_Coder()
**                  Coder()
**
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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "typedef2.h"
#include "cst2.h"
#include "tab2.h"
#include "coder2.h"
#include "lpc2.h"
#include "lsp2.h"
#include "exc2.h"
#include "util2.h"
#include "vad2.h"
#include "codcng2.h"

/*
   This file includes the coder main functions
*/

CODSTATDEF  CodStat ;

/*
**
** Function:        Init_Coder()
**
** Description:     Initializes non-zero state variables
**          for the coder.
**
** Links to text:   Section 2.21
**
** Arguments:       None
**
** Outputs:     None
**
** Return value:    None
**
*/
void  Init_Coder(void)
{
    int i;

    /* Initialize encoder data structure with zeros */
    memset(&CodStat,0,sizeof(CODSTATDEF));

    /* Initialize the previously decoded LSP vector to the DC vector */

    for (i=0; i < LpcOrder; i++)
        CodStat.PrevLsp[i] = LspDcTable[i];

    /* Initialize the taming procedure */
    for(i=0; i<SizErr; i++)
        CodStat.Err[i] = Err0;

    return;
}

/*
**
** Function:        Coder()
**
** Description:     Implements G.723.1 dual-rate coder for    a frame
**          of speech
**
** Links to text:   Section 2
**
** Arguments:
**
**  FLOAT  DataBuff[]   frame (240 samples)
**
** Outputs:
**
**  char   Vout[]       Encoded frame (0/4/20/24 bytes)
**
** Return value:
**
**  Flag            Always True
**
*/
Flag  Coder(FLOAT *DataBuff, char *Vout)
{
    int i,j;

    /*
     *  Local variables
     */
    FLOAT   UnqLpc[SubFrames*LpcOrder];
    FLOAT   QntLpc[SubFrames*LpcOrder];
    FLOAT   PerLpc[2*SubFrames*LpcOrder];

    FLOAT   LspVect[LpcOrder];
    LINEDEF Line;
    PWDEF   Pw[SubFrames];

    FLOAT   ImpResp[SubFrLen];
    FLOAT   *Dpnt;

    Word16  Ftyp = 1;

    /*  Coder Start  */

    Line.Crc = 0;

    Rem_Dc(DataBuff);

    /* Compute the Unquantized Lpc set for whole frame */
    Comp_Lpc(UnqLpc, CodStat.PrevDat, DataBuff);

    /* Convert to Lsp */
    AtoLsp(LspVect, &UnqLpc[LpcOrder*(SubFrames-1)], CodStat.PrevLsp);

    /* Compute the Vad */
    Ftyp = (Word16) Comp_Vad(DataBuff);

    /* VQ Lsp vector */
    Line.LspId = Lsp_Qnt(LspVect, CodStat.PrevLsp);

    Mem_Shift(CodStat.PrevDat, DataBuff);

    /* Compute Percetual filter Lpc coefficeints */
    Wght_Lpc(PerLpc, UnqLpc);

    /* Apply the perceptual weighting filter */
    Error_Wght(DataBuff, PerLpc);

    /*  Compute Open loop pitch estimates  */

    Dpnt = (FLOAT *) malloc(sizeof(FLOAT)*(PitchMax+Frame));

    /* Construct the buffer */
    for (i=0; i < PitchMax;i++)
        Dpnt[i] = CodStat.PrevWgt[i];

    for (i=0;i < Frame;i++)
        Dpnt[PitchMax+i] = DataBuff[i];

    j = PitchMax;
    for (i=0; i < SubFrames/2; i++) {
        Line.Olp[i] = Estim_Pitch(Dpnt, j);
        VadStat.Polp[i+2] = (Word16) Line.Olp[i];
        j += 2*SubFrLen;
    }

    if (Ftyp != 1) {

        /*
        // Case of inactive signal
        */
        free((char *) Dpnt);

        /* Save PrevWgt */
        for ( i = 0 ; i < PitchMax ; i ++ )
            CodStat.PrevWgt[i] = DataBuff[i+Frame-PitchMax];

        /* CodCng => Ftyp = 0 (untransmitted) or 2 (SID) */
        Cod_Cng(DataBuff, &Ftyp, &Line, QntLpc);

        /* Update the ringing delays */
        Dpnt = DataBuff;
        for ( i = 0 ; i < SubFrames; i++ ) {

            /* Update exc_err */
            Update_Err(Line.Olp[i>>1], Line.Sfs[i].AcLg, Line.Sfs[i].AcGn);

            Upd_Ring( Dpnt, &QntLpc[i*LpcOrder], &PerLpc[i*2*LpcOrder],
                                                        CodStat.PrevErr);
            Dpnt += SubFrLen;
        }
    }
    else {

        /* Compute the Hmw */
        j = PitchMax;
        for (i=0; i < SubFrames; i++) {
            Pw[i] = Comp_Pw(Dpnt, j, Line.Olp[i>>1]);
            j += SubFrLen;
        }

        /* Reload the buffer */
        for (i=0; i < PitchMax; i++)
            Dpnt[i] = CodStat.PrevWgt[i];
        for (i=0; i < Frame; i++)
            Dpnt[PitchMax+i] = DataBuff[i];

        /* Save PrevWgt */
        for (i=0; i < PitchMax; i++)
          CodStat.PrevWgt[i] = Dpnt[Frame+i];

        /* Apply the Harmonic filter */
        j = 0;
        for (i=0; i < SubFrames; i++) {
            Filt_Pw(DataBuff, Dpnt, j , Pw[i]);
            j += SubFrLen;
        }

        free((char *) Dpnt);

        /* Inverse quantization of the LSP */
        Lsp_Inq(LspVect, CodStat.PrevLsp, Line.LspId, Line.Crc);

        /* Interpolate the Lsp vectors */
        Lsp_Int(QntLpc, LspVect, CodStat.PrevLsp);

        /* Copy the LSP vector for the next frame */
        for ( i = 0 ; i < LpcOrder ; i ++ )
            CodStat.PrevLsp[i] = LspVect[i];

        /*  Start the sub frame processing loop  */

        Dpnt = DataBuff;

        for (i=0; i < SubFrames; i++) {

            /* Compute full impulse response */
            Comp_Ir(ImpResp, &QntLpc[i*LpcOrder], &PerLpc[i*2*LpcOrder], Pw[i]);

            /* Subtruct the ringing of previos sub-frame */
            Sub_Ring(Dpnt, &QntLpc[i*LpcOrder], &PerLpc[i*2*LpcOrder],
                     CodStat.PrevErr, Pw[i]);

            /* Compute adaptive code book contribution */
            Find_Acbk(Dpnt, ImpResp, CodStat.PrevExc, &Line, i);

            /* Compute fixed code book contribution */
            Find_Fcbk(Dpnt, ImpResp, &Line, i);

            /* Reconstruct the excitation */
            Decod_Acbk(ImpResp, CodStat.PrevExc, Line.Olp[i>>1],
                       Line.Sfs[i].AcLg, Line.Sfs[i].AcGn);

            for (j=SubFrLen; j < PitchMax; j++)
                CodStat.PrevExc[j-SubFrLen] = CodStat.PrevExc[j];

            for (j=0; j < SubFrLen; j++) {
                Dpnt[j] = Dpnt[j] + ImpResp[j];
                CodStat.PrevExc[PitchMax-SubFrLen+j] = Dpnt[j];

                /* Clip the new samples */
                if (CodStat.PrevExc[PitchMax-SubFrLen+j] < (FLOAT)-32767.5)
                    CodStat.PrevExc[PitchMax-SubFrLen+j] = (FLOAT)-32768.0;
                else if (CodStat.PrevExc[PitchMax-SubFrLen+j] > (FLOAT)32766.5)
                    CodStat.PrevExc[PitchMax-SubFrLen+j] = (FLOAT)32767.0;
            }

            /* Update exc_err */
            Update_Err(Line.Olp[i>>1], Line.Sfs[i].AcLg, Line.Sfs[i].AcGn);

            /* Update the ringing delays  */
            Upd_Ring(Dpnt, &QntLpc[i*LpcOrder], &PerLpc[i*2*LpcOrder],
                     CodStat.PrevErr);

            Dpnt += SubFrLen;
        }  /* end of subframes loop */

        /*
        // Save Vad information and reset CNG random generator
        */
        CodCng.PastFtyp = 1;
        CodCng.RandSeed = 12345;

    } /* End of active frame case */

    /* Pack the Line structure */
    Line_Pack(&Line, Vout, Ftyp);

    return (Flag) True;
}
