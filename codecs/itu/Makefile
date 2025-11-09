all:
	$(CC) -o ../codec_itu_g723.so -DG72X_3 -DG72X_ITU -I../../include -Ig.723.1a -Wall -shared -Xlinker -x -D_GNU_SOURCE -fPIC -march=native -O3 -fomit-frame-pointer \
        g.723.1a/basop.c \
        g.723.1a/cod_cng.c \
        g.723.1a/coder.c \
        g.723.1a/dec_cng.c \
        g.723.1a/decod.c \
        g.723.1a/exc_lbc.c \
        g.723.1a/lpc.c \
        g.723.1a/lsp.c \
        g.723.1a/tab_lbc.c \
        g.723.1a/tame.c \
        g.723.1a/util_cng.c \
        g.723.1a/util_lbc.c \
        g.723.1a/vad.c \
        ../codec_itu_g723.c

	$(CC) -o ../codec_itu_g729.so -DG72X_9 -DG72X_ITU -I../../include -Ig.729a -Wall -shared -Xlinker -x -D_GNU_SOURCE -fPIC -march=native -O3 -fomit-frame-pointer \
        g.729a/acelp_ca.c \
        g.729a/basic_op.c \
        g.729a/cod_ld8a.c \
        g.729a/cor_func.c \
        g.729a/de_acelp.c \
        g.729a/dec_gain.c \
        g.729a/dec_lag3.c \
        g.729a/dec_ld8a.c \
        g.729a/dspfunc.c \
        g.729a/filter.c \
        g.729a/gainpred.c \
        g.729a/lpc.c \
        g.729a/lpcfunc.c \
        g.729a/lspdec.c \
        g.729a/lspgetq.c \
        g.729a/oper_32b.c \
        g.729a/p_parity.c \
        g.729a/pitch_a.c \
        g.729a/post_pro.c \
        g.729a/postfilt.c \
        g.729a/pre_proc.c \
        g.729a/pred_lt3.c \
        g.729a/qua_gain.c \
        g.729a/qua_lsp.c \
        g.729a/tab_ld8a.c \
        g.729a/taming.c \
        g.729a/util.c \
        ../codec_itu_g729.c
