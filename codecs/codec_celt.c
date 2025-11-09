/*
 * codec_celt.c — CELT codec translator for Asterisk 20 (LTS)
 *
 * Depends on libcelt (from celt-codec.org)
 *
 * Provides translators:
 *   CELT <-> SLIN (signed linear 16-bit)
 */

/*** MODULEINFO
        <depend>celt</depend>
        <defaultenabled>yes</defaultenabled>
 ***/

#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/format.h"
#include "asterisk/logger.h"

#include <celt/celt.h>

#define CELT_MAX_BITSTREAM 1024

struct celt_coder_pvt {
	CELTEncoder *enc;
	CELTDecoder *dec;
	int sample_rate;
	int frame_size;
	unsigned char bitstream[CELT_MAX_BITSTREAM];
};

/* --------------------- Helpers --------------------- */

static int celt_compute_frame_size(int rate)
{
	switch (rate) {
	case 32000: return 640;   /* 20ms */
	case 44100: return 882;   /* 20ms */
	case 48000: return 960;   /* 20ms */
	default:    return 960;
	}
}

/* --------------------- Encoder --------------------- */

static int celt_encode_frame(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct celt_coder_pvt *state = pvt->pvt;
	struct ast_format *format;
	int16_t *pcm = (int16_t *) f->data.ptr;
	int encoded;

	if (!f->samples)
		return 0;

	encoded = celt_encode(state->enc, pcm, state->frame_size,
	                      state->bitstream, sizeof(state->bitstream));
	if (encoded < 0) {
		ast_log(LOG_WARNING, "CELT encode error %d\n", encoded);
		return -1;
	}

	// TODO

	ast_trans_frameout(pvt, ???);

	return 0;
}

/* --------------------- Decoder --------------------- */

static int celt_decode_frame(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct celt_coder_pvt *state = pvt->pvt;
	int16_t pcm[960]; /* enough for mono */
	int ret;

	ret = celt_decode(state->dec, f->data.ptr, f->datalen, pcm, state->frame_size);
	if (ret < 0) {
		ast_log(LOG_WARNING, "CELT decode error %d\n", ret);
		return -1;
	}

	// TODO

	ast_trans_frameout(pvt, ???);
	return 0;
}

/* --------------------- Alloc / Free --------------------- */

static int celt_new(struct ast_trans_pvt *pvt)
{
	struct celt_coder_pvt *state = pvt->pvt;
	int err;

	state->frame_size = celt_compute_frame_size(state->sample_rate);

	state->enc = celt_encoder_create(state->sample_rate, 1, &err);
	if (!state->enc) {
		ast_log(LOG_ERROR, "CELT encoder create failed (%d)\n", err);
		return -1;
	}
	state->dec = celt_decoder_create(state->sample_rate, 1, &err);
	if (!state->dec) {
		ast_log(LOG_ERROR, "CELT decoder create failed (%d)\n", err);
		celt_encoder_destroy(state->enc);
		return -1;
	}
	return 0;
}

static void celt_destroy(struct ast_trans_pvt *pvt)
{
	struct celt_coder_pvt *state = pvt->pvt;
	if (state->enc)
		celt_encoder_destroy(state->enc);
	if (state->dec)
		celt_decoder_destroy(state->dec);
}

/* --------------------- Translator Factory --------------------- */

static struct ast_translator celt32tolin32 = {
	.name = "celt32tolin32",
	.src_codec = {
		.name = "celt32",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 32000
	},
	.dst_codec = {
		.name = "slin32",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 32000
	},
	.newpvt = celt_new,
	.framein = celt_decode_frame,
	.destroy = celt_destroy,
	.buf_size = 1280,
	.desc_size = sizeof(struct celt_coder_pvt),
};
static struct ast_translator lin32tocelt32 = {
	.name = "lin32tocelt32",
	.src_codec = {
                .name = "slin32",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 32000
        },
	.dst_codec = {
                .name = "celt32",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 32000
        },
	.newpvt = celt_new,
	.framein = celt_encode_frame,
	.destroy = celt_destroy,
	.buf_size = CELT_MAX_BITSTREAM,
	.desc_size = sizeof(struct celt_coder_pvt),
};

static struct ast_translator celt44tolin44 = {
        .name = "celt44tolin44",
        .src_codec = {
                .name = "celt44",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 44100
        },
        .dst_codec = {
                .name = "slin44",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 44100
        },
        .newpvt = celt_new,
        .framein = celt_decode_frame,
        .destroy = celt_destroy,
        .buf_size = 1764,
        .desc_size = sizeof(struct celt_coder_pvt),
};
static struct ast_translator lin44tocelt44 = {
        .name = "lin44tocelt44",
	.src_codec = {
                .name = "slin44",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 44100
        },
        .dst_codec = {
                .name = "celt44",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 44100
        },
        .newpvt = celt_new,
        .framein = celt_encode_frame,
        .destroy = celt_destroy,
        .buf_size = CELT_MAX_BITSTREAM,
        .desc_size = sizeof(struct celt_coder_pvt),
};

static struct ast_translator celt48tolin48 = {
        .name = "celt48tolin48",
        .src_codec = {
                .name = "celt48",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000
        },
        .dst_codec = {
                .name = "slin48",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000
        },
        .newpvt = celt_new,
        .framein = celt_decode_frame,
        .destroy = celt_destroy,
        .buf_size = 1920,
        .desc_size = sizeof(struct celt_coder_pvt),
};
static struct ast_translator lin48tocelt48 = {
        .name = "lin48tocelt48",
        .src_codec = {
                .name = "slin48",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000
        },
        .dst_codec = {
                .name = "celt48",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000
        },
        .newpvt = celt_new,
        .framein = celt_encode_frame,
        .destroy = celt_destroy,
        .buf_size = CELT_MAX_BITSTREAM,
        .desc_size = sizeof(struct celt_coder_pvt),
};
/* --------------------- Load / Unload --------------------- */

static int register_celt_pair(struct ast_translator *a, struct ast_translator *b)
{
	int res = 0;
	res |= ast_register_translator(a);
	res |= ast_register_translator(b);
	return res;
}

static void unregister_celt_pair(struct ast_translator *a, struct ast_translator *b)
{
	ast_unregister_translator(a);
	ast_unregister_translator(b);
}

static int load_module(void)
{
	int res = 0;
	res |= register_celt_pair(&lin32tocelt32, &celt32tolin32);
	res |= register_celt_pair(&lin44tocelt44, &celt44tolin44);
	res |= register_celt_pair(&lin48tocelt48, &celt48tolin48);
	return res ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	unregister_celt_pair(&lin32tocelt32, &celt32tolin32);
	unregister_celt_pair(&lin44tocelt44, &celt44tolin44);
	unregister_celt_pair(&lin48tocelt48, &celt48tolin48);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "CELT codec translators (32k/44.1k/48k)");
