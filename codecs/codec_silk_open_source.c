/*** MODULEINFO
	<depend>silk</depend>
	<conflict>codec_silk</conflict>
	<defaultenabled>yes</defaultenabled>
 ***/

#include "asterisk.h"

#include "asterisk/codec.h"             /* for AST_MEDIA_TYPE_AUDIO */
#include "asterisk/format.h"            /* for ast_format_get_attribute_data */
#include "asterisk/frame.h"             /* for ast_frame, etc */
#include "asterisk/linkedlists.h"       /* for AST_LIST_NEXT, etc */
#include "asterisk/logger.h"            /* for ast_log, ast_debug, etc */
#include "asterisk/module.h"
#include "asterisk/translate.h"         /* for ast_trans_pvt, etc */

#include <SKP_Silk_SDK_API.h>

#define SILK_FRAME_LENGTH_MS       20
#define SILK_MAX_BYTES_PER_FRAME   1024 /* apparently.. */
#define SILK_MAX_SAMPLES_PER_FRAME 960
#define SILK_MAX_INTERNAL_FRAMES   5
#define SILK_MAX_LBRR_DELAY        2

#define SILK_BUFFER_SIZE_BYTES 5120 /* MAX_BYTES * MAX_FRAMES */
#define SLIN_BUFFER_SIZE_BYTES 9600 /* 100 ms @ 48KHZ * 2 bytes */

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_silk.h"

SKP_int32 encSizeBytes;
SKP_int32 decSizeBytes;

struct silk_coder_pvt {
	void *psEnc;
	SKP_SILK_SDK_EncControlStruct encControl;
	void *psDec;
	SKP_SILK_SDK_DecControlStruct decControl;
	int16_t buf[SLIN_BUFFER_SIZE_BYTES / 2];
};

/************ CONSTRUCTORS ************/

static int lintosilk_new(struct ast_trans_pvt *pvt)
{
	SKP_int32 ret;
	SKP_SILK_SDK_EncControlStruct *attr = pvt->explicit_dst ? ast_format_get_attribute_data(pvt->explicit_dst) : NULL;
	struct silk_coder_pvt *coder = pvt->pvt;

	/* init the silk encoder */
	coder->psEnc = malloc(encSizeBytes);
	ret = SKP_Silk_SDK_InitEncoder(coder->psEnc, &coder->encControl);
	if (ret) {
		return ret;
	}

	/* set the parameters for the coder */
	coder->encControl.API_sampleRate = (SKP_int32) pvt->t->src_codec.sample_rate;
	coder->encControl.maxInternalSampleRate = (SKP_int32) pvt->t->dst_codec.sample_rate;
	coder->encControl.packetSize = SILK_FRAME_LENGTH_MS * pvt->t->dst_codec.sample_rate / 1000;
	/* defaults; change here and in res/res_format_attr_silk.c:default_silk_attr */
	coder->encControl.complexity           = 2;
	coder->encControl.useDTX               = attr ? attr->useDTX               : 0;
	coder->encControl.useInBandFEC         = attr ? attr->useInBandFEC         : 1;
	coder->encControl.packetLossPercentage = attr ? attr->packetLossPercentage : 0;
	coder->encControl.bitRate              = attr ? attr->bitRate              : SKP_int32_MAX;

	return 0;
}

static int silktolin_new(struct ast_trans_pvt *pvt)
{
	struct silk_coder_pvt *coder = pvt->pvt;

	/* init the silk decoder */
	coder->psDec = malloc(decSizeBytes);
	coder->decControl.API_sampleRate = (SKP_int32) pvt->t->dst_codec.sample_rate;

	/* reset decoder */
	return SKP_Silk_SDK_InitDecoder(coder->psDec);
}

/************ TRANSLATOR FUNCTIONS ************/

/* Just copy in the samples for later encoding */
static int lintosilk_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct silk_coder_pvt *coder = pvt->pvt;

	/* just add the frames to the buffer */
	memcpy(coder->buf + pvt->samples, f->data.ptr, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/* And decode everything we can in the buffer */
static struct ast_frame *lintosilk_frameout(struct ast_trans_pvt *pvt)
{
	struct silk_coder_pvt *coder = pvt->pvt;
	struct ast_frame *result = NULL;
	struct ast_frame *last = NULL;
	int samples = 0; /* output samples */

	while (pvt->samples >= coder->encControl.packetSize) {
		struct ast_frame *current;
		SKP_int16 nBytesOut = SILK_BUFFER_SIZE_BYTES;
		SKP_int ret = SKP_Silk_SDK_Encode(coder->psEnc,
			&coder->encControl,
			(SKP_int16 *)(coder->buf + samples),
			coder->encControl.packetSize,
			(SKP_uint8 *) pvt->outbuf.ui8,
			&nBytesOut);

		samples += coder->encControl.packetSize;
		pvt->samples -= coder->encControl.packetSize;

		if (ret) {
			ast_log(LOG_WARNING, "Silk_Encode returned %d\n", ret);
			current = NULL;
		} else {
			current = ast_trans_frameout(pvt, nBytesOut, coder->encControl.packetSize);
		}

		if (!current) {
			continue;
		} else if (last) {
			AST_LIST_NEXT(last, frame_list) = current;
		} else {
			result = current;
		}
		last = current;
	}

	/* Move the data at the end of the buffer to the front */
	if (samples) {
		memmove(coder->buf, coder->buf + samples, pvt->samples * 2);
	}

	return result;
}

static int silktolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct silk_coder_pvt *coder = pvt->pvt;
	SKP_int   ret = 0;
	SKP_int16 numSamplesOut = 0;
	SKP_int16 totalSamplesOut = 0;
	SKP_int16 *dst = (SKP_int16*)pvt->outbuf.i16;
	SKP_uint8 *src = (SKP_uint8*)f->data.ptr;
	SKP_int   srcBytes = (SKP_int)f->datalen;
	SKP_int   lostFlag = 0; /* assume no loss for now */
	int       decodeIterations = SILK_MAX_INTERNAL_FRAMES;
	/*
	int i = 0;
	struct ast_frame *nf = NULL;
	SKP_uint8 FECPayload[SILK_MAX_BYTES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES];
	SKP_int16 FECBytes = 0;
	*/

	/* If we indicate native PLC in the translator for silktolin
	 * then we may get passed an empty frame (f->datalen = 0), indicating
	 * that we should fill in some PLC data. So when we get f->datalen=0
	 * we should check to see if we have any next frames (check
	 * f->frame_list.next ?) and look inside them with
	 * SKP_Silk_SDK_search_for_LBRR(). We can search up to 2 frames ahead.
	 * If we find LBRR data, then we should pass that data to the
	 * decoder. If we do not find any LBRR data, then we should
	 * just pass lostFlag=1 to the decoder for normal PLC */

	if (srcBytes == 0) { /* Native PLC */
		lostFlag = 1;
		/* search for LBRR data */
		/* FIXME: Actually do the lookahead, which I guess will require a jitterbuffer? */
		/*
		if (f->frame_list && f->frame_list.next) {
			nf = f->frame_list.next;
		}
		for (i = 0; i < SILK_MAX_LBRR_DELAY; i++) {
			if (nf) {
				if (nf->datalen) {
					SKP_Silk_SDK_search_for_LBRR((SKP_uint8*)nf->data.ptr,
						nf->datalen,
						i + 1,
						FECPayload,
						&FECBytes);
					if (FECBytes > 0) {
						src = FECPayload;
						srcBytes = FECBytes;
						lostFlag = 0;
						break;
					}
				}
				if (nf->frame_list && nf->frame_list.next) {
					nf = nf->frame_list.next;
				}
			}
		}
		*/
	}

	if (lostFlag) {
		/* set the decodeIterations for the do{}while() to be the
		 * number of frames in the last decoded packet */
		decodeIterations = coder->decControl.framesPerPacket;
		ast_log(LOG_NOTICE, "silktolin indicated lost packet - no LBRR\n");
	}

	do {
		ret = SKP_Silk_SDK_Decode(coder->psDec,
			&coder->decControl,
			lostFlag,
			src,
			srcBytes,
			dst,
			&numSamplesOut);

		if (ret) {
			ast_log(LOG_NOTICE, "SKP_Silk_SDK_Decode returned %d\n", ret);
		}

		dst += numSamplesOut;
		totalSamplesOut += numSamplesOut;

		/* decrement the number of iterations remaining */
		decodeIterations--;

	/* while we have more data and have not gone too far */
	} while (coder->decControl.moreInternalDecoderFrames
		&& decodeIterations > 0);

	/* okay, we've decoded everything we can */
	pvt->samples = totalSamplesOut;
	pvt->datalen = totalSamplesOut * 2;
	return 0;
}

/************ DESTRUCTORS ************/

static void lintosilk_destroy(struct ast_trans_pvt *pvt)
{
	struct silk_coder_pvt *coder = pvt->pvt;
	free(coder->psEnc);
}


static void silktolin_destroy(struct ast_trans_pvt *pvt)
{
	struct silk_coder_pvt *coder = pvt->pvt;
	free(coder->psDec);
}

/************ TRANSLATOR DEFINITIONS ************/


static struct ast_translator silk8tolin = {
	.name = "silk8tolin",
	.src_codec = {
		.name = "silk8",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "slin8",
	.newpvt = silktolin_new,
	.framein = silktolin_framein,
	.destroy = silktolin_destroy,
	.sample = silk8_sample,
	.desc_size = sizeof(struct silk_coder_pvt),
	.buffer_samples = SLIN_BUFFER_SIZE_BYTES / 2,
	.buf_size = SLIN_BUFFER_SIZE_BYTES,
	.native_plc = 1
};

static struct ast_translator lintosilk8 = {
	.name = "lintosilk8",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "silk8",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "silk8",
	.newpvt = lintosilk_new,
	.framein = lintosilk_framein,
	.frameout = lintosilk_frameout,
	.destroy = lintosilk_destroy,
	.sample = slin8_sample,
	.desc_size = sizeof(struct silk_coder_pvt),
	.buffer_samples = SILK_MAX_SAMPLES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES,
	.buf_size = SILK_BUFFER_SIZE_BYTES
};

static struct ast_translator silk12tolin = {
	.name = "silk12tolin",
	.src_codec = {
		.name = "silk12",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 12000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 12000,
	},
	.format = "slin12",
	.newpvt = silktolin_new,
	.framein = silktolin_framein,
	.destroy = silktolin_destroy,
	.sample = silk12_sample,
	.desc_size = sizeof(struct silk_coder_pvt),
	.buffer_samples = SLIN_BUFFER_SIZE_BYTES / 2,
	.buf_size = SLIN_BUFFER_SIZE_BYTES,
	.native_plc = 1
};

static struct ast_translator lintosilk12 = {
	.name = "lintosilk12",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 12000,
	},
	.dst_codec = {
		.name = "silk12",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 12000,
	},
	.format = "silk12",
	.newpvt = lintosilk_new,
	.framein = lintosilk_framein,
	.frameout = lintosilk_frameout,
	.destroy = lintosilk_destroy,
	.desc_size = sizeof(struct silk_coder_pvt),
	.buffer_samples = SILK_MAX_SAMPLES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES,
	.buf_size = SILK_BUFFER_SIZE_BYTES
};

static struct ast_translator silk16tolin = {
	.name = "silk16tolin",
	.src_codec = {
		.name = "silk16",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.format = "slin16",
	.newpvt = silktolin_new,
	.framein = silktolin_framein,
	.destroy = silktolin_destroy,
	.sample = silk16_sample,
	.desc_size = sizeof(struct silk_coder_pvt),
	.buffer_samples = SLIN_BUFFER_SIZE_BYTES / 2,
	.buf_size = SLIN_BUFFER_SIZE_BYTES,
	.native_plc = 1
};

static struct ast_translator lintosilk16 = {
	.name = "lintosilk16",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.dst_codec = {
		.name = "silk16",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.format = "silk16",
	.newpvt = lintosilk_new,
	.framein = lintosilk_framein,
	.frameout = lintosilk_frameout,
	.destroy = lintosilk_destroy,
	.sample = slin16_sample,
	.desc_size = sizeof(struct silk_coder_pvt),
	.buffer_samples = SILK_MAX_SAMPLES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES,
	.buf_size = SILK_BUFFER_SIZE_BYTES
};

static struct ast_translator silk24tolin = {
	.name = "silk24tolin",
	.src_codec = {
		.name = "silk24",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 24000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 24000,
	},
	.format = "slin24",
	.newpvt = silktolin_new,
	.framein = silktolin_framein,
	.destroy = silktolin_destroy,
	.sample = silk24_sample,
	.desc_size = sizeof(struct silk_coder_pvt),
	.buffer_samples = SLIN_BUFFER_SIZE_BYTES / 2,
	.buf_size = SLIN_BUFFER_SIZE_BYTES,
	.native_plc = 1
};

static struct ast_translator lintosilk24 = {
	.name = "lintosilk24",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 24000,
	},
	.dst_codec = {
		.name = "silk24",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 24000,
	},
	.format = "silk24",
	.newpvt = lintosilk_new,
	.framein = lintosilk_framein,
	.frameout = lintosilk_frameout,
	.destroy = lintosilk_destroy,
	.desc_size = sizeof(struct silk_coder_pvt),
	.buffer_samples = SILK_MAX_SAMPLES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES,
	.buf_size = SILK_BUFFER_SIZE_BYTES
};



/************ MODULE LOAD / UNLOAD **************/

static int load_module(void)
{
	SKP_int32 ret;
	int res = 0;

	/* print the skype version */
	ast_debug(2, "SILK %s\n", SKP_Silk_SDK_get_version());

	/* get the encoder / decoder sizes */
	ret = SKP_Silk_SDK_Get_Encoder_Size(&encSizeBytes);
	if (ret) {
		ast_log(LOG_WARNING, "SKP_Silk_SDK_Get_Encoder_size returned %d\n", ret);
	}
	ret = SKP_Silk_SDK_Get_Decoder_Size(&decSizeBytes);
	if (ret) {
		ast_log(LOG_WARNING, "SKP_Silk_SDK_Get_Decoder_size returned %d\n", ret);
	}

	res |= ast_register_translator(&silk8tolin);
	res |= ast_register_translator(&lintosilk8);
	res |= ast_register_translator(&silk12tolin);
	res |= ast_register_translator(&lintosilk12);
	res |= ast_register_translator(&silk16tolin);
	res |= ast_register_translator(&lintosilk16);
	res |= ast_register_translator(&silk24tolin);
	res |= ast_register_translator(&lintosilk24);

	/* register the encoder / decoder */
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res = 0;
	ast_debug(2, "Silk Coder/Encoder unloading\n");

	res |= ast_unregister_translator(&lintosilk8);
	res |= ast_unregister_translator(&silk8tolin);
	res |= ast_unregister_translator(&lintosilk12);
	res |= ast_unregister_translator(&silk12tolin);
	res |= ast_unregister_translator(&lintosilk16);
	res |= ast_unregister_translator(&silk16tolin);
	res |= ast_unregister_translator(&lintosilk24);
	res |= ast_unregister_translator(&silk24tolin);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SILK Coder/Decoder");
