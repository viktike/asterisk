/*
**
** File:        "exc2.h"
**
** Description:     Function prototypes for "exc2.c"
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

int   Estim_Pitch(FLOAT *Dpnt, int Start);
PWDEF Comp_Pw(FLOAT *Dpnt, int Start, int Olp);
void  Filt_Pw(FLOAT *DataBuff, FLOAT *Dpnt, int Start, PWDEF Pw);
void  Find_Fcbk(FLOAT *Dpnt, FLOAT *ImpResp, LINEDEF *Line, int Sfc);
void  Find_Best(BESTDEF *Best, FLOAT *Tv, FLOAT *ImpResp, int Np, int Olp);
void  Fcbk_Pack(FLOAT *Dpnt, SFSDEF *Sfs, BESTDEF *Best, int Np);
void  Fcbk_Unpk(FLOAT *Tv, SFSDEF Sfs, int Olp, int Sfc);
int   ACELP_LBC_code(FLOAT *X, FLOAT *h, int T0, FLOAT *code,
                     int *ind_gain, int *shift, int *sign, FLOAT gain_T0);
void  Cor_h(FLOAT *H, FLOAT *rr);
void  Cor_h_X(FLOAT h[],FLOAT X[],FLOAT D[]);
int   D4i64_LBC(FLOAT Dn[], FLOAT rr[], FLOAT h[], FLOAT cod[],
                FLOAT y[], int *code_shift, int *sign);
int   G_code(FLOAT X[], FLOAT Y[], FLOAT *gain_q);
void  Gen_Trn(FLOAT *Dst, FLOAT *Src, int Olp);

void  Find_Acbk(FLOAT *Tv, FLOAT *ImpResp, FLOAT *PrevExc, LINEDEF
                *Line, int Sfc);
void  Get_Rez(FLOAT *Tv, FLOAT *PrevExc, int Lag);
void  Decod_Acbk(FLOAT *Tv, FLOAT *PrevExc, int Olp, int Lid, int Gid);
Word16 Comp_Info(FLOAT *Buff, int Olp, FLOAT *Gain);
void  Regen(FLOAT *DataBuff, FLOAT *Buff, Word16 Lag, FLOAT Gain,
            int Ecount, Word16 *Sd);
PFDEF Comp_Lpf(FLOAT *Buff, int Olp, int Sfc);
int   Find_B(FLOAT *Buff, int Olp, int Sfc);
int   Find_F(FLOAT *Buff, int Olp, int Sfc);
PFDEF Get_Ind(int Ind, FLOAT Ten, FLOAT Ccr, FLOAT Enr);
void  Filt_Lpf(FLOAT *Tv, FLOAT *Buff, PFDEF Pf, int Sfc);
int   search_T0(int T0, int Gid, FLOAT *gain_T0);
void  reset_max_time(void);

void Update_Err(int Olp, int AcLg, int AcGn);
int  Test_Err(int Lag1, int Lag2);
