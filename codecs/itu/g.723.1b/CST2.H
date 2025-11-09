/*
**
** File:        "cst2.h"
**
** Description:  This file contains global definition of the SG15
**    LBC Coder for 6.3/5.3 kbps.
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

#define  False 0
#define  True  1

/* Definition of the working mode */
enum  Wmode {Both, Cod, Dec};

/* Coder rate */
enum  Crate    {Rate63, Rate53};

/* Coder global constants */
#define  Frame       240
#define  LpcFrame    180
#define  SubFrames   4
#define  SubFrLen    (Frame/SubFrames)

/* LPC constants */
#define  LpcOrder          10
#define  RidgeFact         10
#define  CosineTableSize   512
#define  PreCoef           ((FLOAT) -0.25)

#define  LspPred0          ((FLOAT)12.0/(FLOAT)32.0)
#define  LspPred1          ((FLOAT)23.0/(FLOAT)32.0)

#define  LspQntBands       3
#define  LspCbSize         256
#define  LspCbBits         8

/* LTP constants */
#define  PitchMin          18
#define  PitchMax          (PitchMin+127)
#define  PwRange           3
#define  ClPitchOrd        5
#define  Pstep             1
#define NbFilt085          85
#define NbFilt170          170

/* MP-MLQ constants */
#define  Sgrid             2
#define  MaxPulseNum       6
#define  MlqSteps          2

/* acelp constants */

#define SubFrLen2          (SubFrLen +4)
#define DIM_RR             416
#define NB_POS             8
#define STEP               8
#define MSIZE              64
#define threshold          ((FLOAT) 0.5)
#define max_time           120

/* Gain constant */
#define  NumOfGainLev      24

/* FER constant */
#define  ErrMaxNum         3

/* CNG constants  */
#define NbAvAcf            3  /* Nb of frames for Acf average               */
#define NbAvGain           3  /* Nb of frames for gain average              */
#define ThreshGain         3  /* Theshold for quantized gains               */
#define FracThreshP1       ((FLOAT) 1.2136)
#define MaxLev             22 /* Max. gain index                            */
#define NbPulsBlk          11 /* Nb of pulses in 2-subframes blocks         */

#define InvNbPulsBlk       ((FLOAT)1.0/(FLOAT)NbPulsBlk)
#define NbFilt             50 /* size of LT filters table                   */
#define LpcOrderP1         (LpcOrder+1)
#define SizAcf             ((NbAvAcf+1)*LpcOrderP1) /* size of array Acf    */
#define SubFrLenD          (2*SubFrLen)
#define Gexc_Max           ((FLOAT)5000.0) /* Max gain for fixed excitation */

/* Taming constants */
#define NbFilt085_min       51
#define NbFilt170_min       93
#define SizErr              5
#define Err0                ((FLOAT) 0.00000381464)
#define ThreshErr           ((FLOAT) 128.0)

#define MAXV                ((FLOAT) 256.0)

/*   Used structures */

typedef  struct   {
/* High pass variables */
   FLOAT    HpfZdl;
   FLOAT    HpfPdl;
/* Lsp previos vector */
   FLOAT   PrevLsp[LpcOrder];

/* All pitch operation buffers */
   FLOAT    PrevWgt[PitchMax];
   FLOAT    PrevErr[PitchMax];
   FLOAT    PrevExc[PitchMax];

/* Requered memory for the delay */
   FLOAT   PrevDat[LpcFrame-SubFrLen];

/* Used delay lines */
   FLOAT    WghtFirDl[LpcOrder];
   FLOAT    WghtIirDl[LpcOrder];
   FLOAT    RingFirDl[LpcOrder];
   FLOAT    RingIirDl[LpcOrder];

/* For taming procedure */

   Word16   SinDet;
   FLOAT    Err[SizErr];

   } CODSTATDEF;

typedef  struct   {
   int     Ecount;
   FLOAT   InterGain;
   Word16  InterIndx;
   Word16  Rseed;
   FLOAT   Park;
   FLOAT   Gain;
/* Lsp previous vector */
   FLOAT   PrevLsp[LpcOrder];

/* All pitch operation buffers */
   FLOAT   PrevExc[PitchMax];

/* Used delay lines */
   FLOAT   SyntIirDl[LpcOrder];
   FLOAT   PostFirDl[LpcOrder];
   FLOAT   PostIirDl[LpcOrder];

   } DECSTATDEF;

/* subframe coded parameters */
typedef  struct   {
   int      AcLg;
   int      AcGn;
   int      Mamp;
   int      Grid;
   int      Tran;
   int      Pamp;
   Word32   Ppos;
   } SFSDEF;

/* frame coded parameters */
typedef  struct   {
   Word16   Crc;
   Word32   LspId;
   int      Olp[SubFrames/2];
   SFSDEF   Sfs[SubFrames];
   } LINEDEF;

/* harmonic noise shaping filter parameters */
typedef  struct   {
   int   Indx;
   FLOAT Gain;
   } PWDEF;

/* pitch postfilter parameters */
typedef  struct   {
   int     Indx;
   FLOAT   Gain;
   FLOAT   ScGn;
   } PFDEF;

/* best excitation vector parameters for the high rate */
typedef  struct {
   FLOAT    MaxErr;
   int      GridId;
   int      MampId;
   int      UseTrn;
   int      Ploc[MaxPulseNum];
   FLOAT    Pamp[MaxPulseNum];
   } BESTDEF;

    /* VAD static variables */
typedef struct {
    Word16  Hcnt;
    Word16  Vcnt;
    FLOAT   Penr;
    FLOAT   Nlev;
    Word16  Aen;
    Word16  Polp[4];
    FLOAT   NLpc[LpcOrder];
} VADSTATDEF;


/* CNG features */

/* Coder part */
typedef struct {
    FLOAT   CurGain;
    Word16  PastFtyp;
    FLOAT   Acf[SizAcf];
    FLOAT   LspSid[LpcOrder];
    FLOAT   SidLpc[LpcOrder];
    FLOAT   RC[LpcOrderP1];
    FLOAT   Ener[NbAvGain];
    Word16  NbEner;
    Word16  IRef;
    FLOAT   SidGain;
    Word16  RandSeed;
} CODCNGDEF;

/* Decoder part */
typedef struct {
    FLOAT   CurGain;
    Word16  PastFtyp;
    FLOAT   LspSid[LpcOrder];
    FLOAT   SidGain;
    Word16  RandSeed;
} DECCNGDEF;
