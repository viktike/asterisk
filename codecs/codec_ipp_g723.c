/*
This program is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.
If not, see <https://www.gnu.org/licenses/>.

SPDX-FileCopyrightText: 2024 Arkadi Shishlov <arkadi.shishlov@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
*/

/*** MODULEINFO
        <depend>ipp</depend>
        <conflict>codec_itu_g723</conflict>
        <conflict>codec_g723</conflict>
        <defaultenabled>no</defaultenabled>
        <support_level>extended</support_level>
 ***/

#include <pthread.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* single file implements both G.729 and G.723.1, both IPP and Bcg729 based codecs,
   Asterisk 1.4 to 20 */
/* quite a lot of preprocessor abuse, but still better than maintaining multiple
   similar files */

// #define AST_MODULE_SELF_SYM __internal_g72x_self

    #ifdef ASTERISK_ASTERISK_H
        /* Ubuntu */
        #include <asterisk/asterisk.h>
    #else
        #include <asterisk.h>
    #endif
    #include <asterisk/lock.h>
    #include <asterisk/translate.h>
    #include <asterisk/module.h>
    #include <asterisk/logger.h>
    #include <asterisk/channel.h>
    #include <asterisk/utils.h>
    #include <asterisk/options.h>
    #include <asterisk/cli.h>

    #include <ippcore.h>
    #include <ipps.h>

    #define G72X_CODEC "g723"

        #include "ipp/g723api.h"

    #include "slin_g72x_ex.h"
    #include "g723_slin_ex.h"

    #define SLIN_FRAME_LEN  480
    #define G723_FRAME_LEN  24 /* maximum frame length */
    #define G723_SAMPLES    240 /* 30ms at 8000 hz, 480 bytes signed linear */
    #define BUFFER_SAMPLES  8000

    #define G72X_FRAME_LEN    G723_FRAME_LEN
    #define G72X_SAMPLES      G723_SAMPLES
    #define G72X_AST_FORMAT   AST_FORMAT_G723_1
    #define G72X_AST_FORMAT13 ast_format_g723

    #define G723_RATE_63 0 /* G723_Rate63 in owng723.h */
    #define G723_RATE_53 1 /* G723_Rate53 */
    #define G723_DEFAULT_SEND_RATE G723_RATE_63

    static int g723_sendrate = G723_DEFAULT_SEND_RATE;

    #define FRAME_SUBCLASS subclass.codec

    #define FRAME_DATA data.ptr
    #define OUTBUF_SLIN outbuf.i16
    #define OUTBUF_G72X outbuf.uc

#define AST_MODULE "codec_" G72X_CODEC
#define G72X_DESC G72X_CODEC " Coder/Decoder, based on Intel IPP"

struct g72x_coder_pvt {
    void *coder;
    void *scratch_mem;
    int16_t buf[BUFFER_SAMPLES]; /* 1 second */
};

    static int encoder_size;
    static int decoder_size;
    static int coder_size_scratch;

/* debug array to collect information about incoming frame sizes */
/* the code is not aiming at correctness so there are no locking and no atomic operations */
static int *frame_sizes = NULL;
#define DEBUG_MAX_FRAME_SIZE 2000
#define DEBUG_FRAME_SIZE_INC \
    do { \
    if (frame_sizes != NULL) { \
        if (f->datalen >= DEBUG_MAX_FRAME_SIZE) \
            ++frame_sizes[DEBUG_MAX_FRAME_SIZE]; \
        else \
            ++frame_sizes[f->datalen]; \
        } \
    } while (0)

static int lintog72x_new(struct ast_trans_pvt *pvt)
{
    struct g72x_coder_pvt *state = pvt->pvt;

    #ifndef IPPCORE_NO_SSE
        ippSetFlushToZero(1, NULL); /* is FZM flag per-thread or not? does it matter at all? */
    #endif
    state->coder = ippsMalloc_8u(encoder_size);
    state->scratch_mem = ippsMalloc_8u(coder_size_scratch);
        apiG723Encoder_InitBuff(state->coder, state->scratch_mem);
        apiG723Encoder_Init(state->coder, G723Encode_DefaultMode);
    return 0;
}

static int g72xtolin_new(struct ast_trans_pvt *pvt)
{
    struct g72x_coder_pvt *state = pvt->pvt;

    #ifndef IPPCORE_NO_SSE
        ippSetFlushToZero(1, NULL);
    #endif
    state->coder = ippsMalloc_8u(decoder_size);
    state->scratch_mem = ippsMalloc_8u(coder_size_scratch);
        apiG723Decoder_InitBuff(state->coder, state->scratch_mem);
        apiG723Decoder_Init(state->coder, G723Decode_DefaultMode);
    return 0;
}

static struct ast_frame *lintog72x_sample(void)
{
    static struct ast_frame f;
    f.frametype = AST_FRAME_VOICE;
    f.subclass.format = ast_format_slin;
    f.datalen = sizeof(slin_g72x_ex);
    f.samples = sizeof(slin_g72x_ex)/2;
    f.mallocd = 0;
    f.offset = 0;
    f.src = __PRETTY_FUNCTION__;
    f.FRAME_DATA = slin_g72x_ex;
    return &f;
}

static struct ast_frame *g72xtolin_sample(void)
{
    static struct ast_frame f;
    f.frametype = AST_FRAME_VOICE;
    f.subclass.format = G72X_AST_FORMAT13;
    f.datalen = sizeof(g72x_slin_ex);
    f.samples = G72X_SAMPLES;
    f.mallocd = 0;
    f.offset = 0;
    f.src = __PRETTY_FUNCTION__;
    f.FRAME_DATA = g72x_slin_ex;
    return &f;
}

static unsigned char lost_frame[G72X_FRAME_LEN] = { 0 };

static int g723_frame_length(int frametype)
{
    switch(frametype) {
        case 0: return 24; /* 6.3kbps */
        case 1: return 20; /* 5.3kbps */
        case 2: return 4;  /* SID */
    }
    return 1; /* XXX untransmitted */
}

static int g72xtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
    struct g72x_coder_pvt *state = pvt->pvt;
    int16_t *dst = (int16_t *)pvt->OUTBUF_SLIN;
    int badframe;
    int frametype;
    int framesize;
    int x;

    DEBUG_FRAME_SIZE_INC;

    if (f->datalen == 0) {  /* Native PLC interpolation */
        ast_debug(5, "G.723.1 PLC\n");
        if (pvt->samples + G723_SAMPLES > BUFFER_SAMPLES) {
            ast_log(LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        badframe = 1; /* the frame is lost */
        apiG723Decode(state->coder, (void *)lost_frame, badframe, dst + pvt->samples);
        pvt->samples += G723_SAMPLES;
        pvt->datalen += 2 * G723_SAMPLES; /* 2 bytes/sample */
        return 0;
    }

    badframe = 0;
    for(x = 0; x < f->datalen; x += framesize) {
        if (pvt->samples + G723_SAMPLES > BUFFER_SAMPLES) {
            ast_log(LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        frametype = *((unsigned char *)f->FRAME_DATA + x) & (short)0x0003;
        framesize = g723_frame_length(frametype);
        apiG723Decode(state->coder, (void *)f->FRAME_DATA + x, badframe, dst + pvt->samples);
        pvt->samples += G723_SAMPLES;
        pvt->datalen += 2*G723_SAMPLES;
    }
    return 0;
}


static int lintog72x_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
    struct g72x_coder_pvt *state = pvt->pvt;

    memcpy(state->buf + pvt->samples, f->FRAME_DATA, f->datalen);
    pvt->samples += f->samples;
    return 0;
}

static struct ast_frame *lintog72x_frameout(struct ast_trans_pvt *pvt)
{
    struct g72x_coder_pvt *state = pvt->pvt;
    int datalen = 0;
    int samples = 0;

    /* We can't work on anything less than a frame in size */
    if (pvt->samples < G72X_SAMPLES)
        return NULL;
    while (pvt->samples >= G72X_SAMPLES) {
        apiG723Encode(state->coder, state->buf + samples, g723_sendrate, (void *)(pvt->OUTBUF_G72X + datalen));
        datalen += (g723_sendrate == G723_RATE_63) ? 24 : 20;
        samples += G72X_SAMPLES;
        pvt->samples -= G72X_SAMPLES;
    }

    /* Move the data at the end of the buffer to the front */
    if (pvt->samples)
        memmove(state->buf, state->buf + samples, pvt->samples * 2);

    return ast_trans_frameout(pvt, datalen, samples);
}

static void g72x_print_debug_on_destroy(void)
{
    int i;
    /* output the sizes of frames passed to decoder */
    if (option_debug >= 1 && frame_sizes != NULL) {
        ast_debug(1, G72X_CODEC " frames\n");
        ast_debug(1, "length: count\n");
        for (i = 0; i <= DEBUG_MAX_FRAME_SIZE; ++i) {
            if (frame_sizes[i] > 0)
                ast_debug(1, "%6d: %d\n", i, frame_sizes[i]);
        }
    }
}

static void g72xtolin_destroy(struct ast_trans_pvt *pvt)
{
    struct g72x_coder_pvt *state = pvt->pvt;
    ippsFree(state->coder);
    ippsFree(state->scratch_mem);
    g72x_print_debug_on_destroy();
}

static void lintog72x_destroy(struct ast_trans_pvt *pvt)
{
    struct g72x_coder_pvt *state = pvt->pvt;
    ippsFree(state->coder);
    ippsFree(state->scratch_mem);
    g72x_print_debug_on_destroy();
}

static struct ast_translator g72xtolin = {
    .name = G72X_CODEC "tolin",
    .src_codec = { .name = G72X_CODEC, .type = AST_MEDIA_TYPE_AUDIO, .sample_rate = 8000 },
    .dst_codec = { .name = "slin",     .type = AST_MEDIA_TYPE_AUDIO, .sample_rate = 8000 },
    .newpvt = g72xtolin_new,
    .framein = g72xtolin_framein,
    .destroy = g72xtolin_destroy,
    .sample = g72xtolin_sample,
    .desc_size = sizeof(struct g72x_coder_pvt) - BUFFER_SAMPLES*2, /* buffer is not needed for g723/9 -> slin */
    .buf_size = SLIN_FRAME_LEN*100, /* 1 second */
    .native_plc = 1
};

static struct ast_translator lintog72x = {
    .name = "linto" G72X_CODEC,
    .src_codec = { .name = "slin",     .type = AST_MEDIA_TYPE_AUDIO, .sample_rate = 8000 },
    .dst_codec = { .name = G72X_CODEC, .type = AST_MEDIA_TYPE_AUDIO, .sample_rate = 8000 },
    .newpvt = lintog72x_new,
    .framein = lintog72x_framein,
    .frameout = lintog72x_frameout,
    .destroy = lintog72x_destroy,
    .sample = lintog72x_sample,
    .desc_size = sizeof(struct g72x_coder_pvt), /* buffer up-to 1 second of speech */
        .buf_size = G723_FRAME_LEN*33 /* almost 1 sec of g723 at 6.3kbps */
};

    static void parse_config(void)
    {
        /* XXX struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 }; */
        struct ast_flags config_flags = { 0 };
        struct ast_config *cfg = ast_config_load("codecs.conf", config_flags);
        struct ast_variable *var;
        int rate;

        if (cfg == NULL)
            return;
        for (var = ast_variable_browse(cfg, "g723"); var; var = var->next) {
            if (!strcasecmp(var->name, "sendrate")) {
                rate = atoi(var->value);
                if (rate == 53 || rate == 63) {
                    ast_debug(5, "G.723.1 setting sendrate to %d\n", rate);
                    g723_sendrate = (rate == 63) ? G723_RATE_63 : G723_RATE_53;
                } else {
                    ast_log(LOG_ERROR, "G.723.1 sendrate must be 53 or 63\n");
                }
            } else {
                ast_log(LOG_ERROR, "G.723.1 has only one option \"sendrate=<53|63>\" for 5.3 and 6.3Kbps respectivelly\n");
            }
        }
        ast_config_destroy(cfg);
    }

    static char* g72x_toggle_debug(int fd)
{
    struct timespec delay = { 0, 100000000 }; /* 100ms */
    void *tmp;

    /* no locking intentionally */
    if (frame_sizes != NULL) {
        tmp = frame_sizes;
        frame_sizes = NULL;
        nanosleep(&delay, NULL); /* hope all users are gone */
        ast_free(tmp);
        ast_cli(fd, G72X_CODEC " debug disabled\n");
    } else {
        tmp = ast_malloc((DEBUG_MAX_FRAME_SIZE+1)*sizeof(int));
        if (tmp == NULL)
            return CLI_FAILURE;
        memset(tmp, 0, (DEBUG_MAX_FRAME_SIZE+1)*sizeof(int));
        ast_cli(fd, G72X_CODEC " debug enabled\n");
        frame_sizes = tmp;
    }
    return CLI_SUCCESS;
}

static char g72x_toggle_debug_desc[] = "Toggle " G72X_CODEC " codec frame size statistics";
static char g72x_usage[] =
    "Usage: " G72X_CODEC " debug\n"
    "       Toggle " G72X_CODEC " codec frame size statistics\n";

    static char *handle_cli_g72x_toggle_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
    {
        switch (cmd) {
        case CLI_INIT:
            e->command = G72X_CODEC " debug";
            e->usage = g72x_usage;
            return NULL;
        case CLI_GENERATE:
            return NULL;
        }
        if (a->argc != 2)
            return CLI_SHOWUSAGE;
        g72x_toggle_debug(a->fd);
        return CLI_SUCCESS;
    }

    static struct ast_cli_entry cli_g72x = AST_CLI_DEFINE(handle_cli_g72x_toggle_debug, g72x_toggle_debug_desc);


static int load_module(void)
{
    int res;

#if IPPCORE_STATIC_INIT
    ippStaticInit();
#endif

    parse_config();

        apiG723Decoder_Alloc(&decoder_size);
        apiG723Encoder_Alloc(&encoder_size);
        apiG723Codec_ScratchMemoryAlloc(&coder_size_scratch);

    res = ast_register_translator(&g72xtolin);
    if (!res)
        res = ast_register_translator(&lintog72x);
    else
        ast_unregister_translator(&g72xtolin);

    ast_cli_register(&cli_g72x);

    return res;
}

static int unload_module(void)
{
    int res;

    res = ast_unregister_translator(&lintog72x);
    res |= ast_unregister_translator(&g72xtolin);

    ast_cli_unregister(&cli_g72x);

    return res;
}

    /* AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, G72X_CODEC " Coder/Decoder"); */
    AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, G72X_DESC, .load = load_module, .unload = unload_module, .buildopt_sum = "");
