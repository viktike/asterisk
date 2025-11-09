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

	struct ast_frame out = {
		.frametype = AST_FRAME_VOICE,
		.subclass.format = ast_format_get_by_id(ast_format_id_by_rate("CELT", state->sample_rate)),
		.data.ptr = state->bitstream,
		.datalen = encoded,
		.samples = state->frame_size,
		.src = __PRETTY_FUNCTION__,
		.mallocd = 0,
	};
	ast_trans_frameout(pvt, &out);
	return 0;
}

/* --------------------- Decoder --------------------- */

static int celt_decode_frame(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct celt_coder_pvt *state = pvt->pvt;
	int16_t pcm[960 * 2]; /* enough for mono/stereo */
	int ret;

	ret = celt_decode(state->dec, f->data.ptr, f->datalen, pcm, state->frame_size);
	if (ret < 0) {
		ast_log(LOG_WARNING, "CELT decode error %d\n", ret);
		return -1;
	}

	struct ast_frame out = {
		.frametype = AST_FRAME_VOICE,
		.subclass.format = ast_format_get_by_id(ast_format_id_by_rate("slin", state->sample_rate)),
		.data.ptr = pcm,
		.datalen = state->frame_size * 2,
		.samples = state->frame_size,
		.src = __PRETTY_FUNCTION__,
		.mallocd = 0,
	};
	ast_trans_frameout(pvt, &out);
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

#define DECL_CELT_PAIR(rate) \
static struct ast_translator celttolin##rate = { \
	.name = "celttolin" #rate, \
	.src_codec = "CELT" #rate, \
	.dst_codec = "slin" #rate, \
	.newpvt = celt_new, \
	.framein = celt_decode_frame, \
	.destroy = celt_destroy, \
	.sample_rate = rate, \
	.buf_size = 2 * celt_compute_frame_size(rate), \
	.desc_size = sizeof(struct celt_coder_pvt), \
}; \
static struct ast_translator lintocelt##rate = { \
	.name = "lintocelt" #rate, \
	.src_codec = "slin" #rate, \
	.dst_codec = "CELT" #rate, \
	.newpvt = celt_new, \
	.framein = celt_encode_frame, \
	.destroy = celt_destroy, \
	.sample_rate = rate, \
	.buf_size = CELT_MAX_BITSTREAM, \
	.desc_size = sizeof(struct celt_coder_pvt), \
};

DECL_CELT_PAIR(32000)
DECL_CELT_PAIR(44100)
DECL_CELT_PAIR(48000)

/* --------------------- Load / Unload --------------------- */

static int register_celt_pair(struct ast_translator *a, struct ast_translator *b, int rate)
{
	int res = 0;
	res |= ast_register_translator(a);
	res |= ast_register_translator(b);
	if (res)
		ast_log(LOG_ERROR, "Failed to register CELT %d Hz translators\n", rate);
	else
		ast_log(LOG_NOTICE, "Registered CELT translators for %d Hz\n", rate);
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

	/* Set per-instance sample rate field */
	celttolin32000.sample_rate = lintocelt32000.sample_rate = 32000;
	celttolin44100.sample_rate = lintocelt44100.sample_rate = 44100;
	celttolin48000.sample_rate = lintocelt48000.sample_rate = 48000;

	res |= register_celt_pair(&lintocelt32000, &celttolin32000, 32000);
	res |= register_celt_pair(&lintocelt44100, &celttolin44100, 44100);
	res |= register_celt_pair(&lintocelt48000, &celttolin48000, 48000);

	return res ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	unregister_celt_pair(&lintocelt32000, &celttolin32000);
	unregister_celt_pair(&lintocelt44100, &celttolin44100);
	unregister_celt_pair(&lintocelt48000, &celttolin48000);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "CELT codec translators (32k/44.1k/48k)");
