/*
 *
  Copyright (c) Dialogic(R), 2010

 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifdef CC_AST_HAS_VERSION_10_0 /* { */

#define CC_FORMAT_ALAW      (1ULL << 3)
#define CC_FORMAT_G722      (1ULL << 12)
#define CC_FORMAT_G723_1    (1ULL << 0)
#define CC_FORMAT_G726      (1ULL << 11)
#define CC_FORMAT_G729A     (1ULL << 8)
#define CC_FORMAT_GSM       (1ULL << 1)
#define CC_FORMAT_ILBC      (1ULL << 10)
#define CC_FORMAT_SIREN14   (1ULL << 14)
#define CC_FORMAT_SIREN7    (1ULL << 13)
#define CC_FORMAT_SLINEAR   (1ULL << 6)
#define CC_FORMAT_SLINEAR16 (1ULL << 15)
#define CC_FORMAT_ULAW      (1ULL << 2)

#ifdef CC_AST_HAS_VERSION_13_0
#include <asterisk/format_compatibility.h>
#include <asterisk/format_cache.h>
#endif

#ifdef CC_AST_HAS_VERSION_13_0
static inline struct ast_format * ccCodec2AstCodec(int ccCodec)
{
	return (ast_format_compatibility_bitfield2format(ccCodec));
}
#else
static inline enum ast_format_id ccCodec2AstCodec(int ccCodec)
{
	return (ast_format_id_from_old_bitfield(ccCodec));
}
#endif

static inline const char* cc_getformatname(int ccCodec)
{
#ifdef CC_AST_HAS_VERSION_13_0
	struct ast_format * id = ccCodec2AstCodec (ccCodec);
	return ast_format_get_name(id);
#else
	enum ast_format_id id = ccCodec2AstCodec (ccCodec);
	struct ast_format fmt;

	ast_format_clear(&fmt);
	ast_format_set(&fmt, id, 0);

	return ast_getformatname(&fmt);
#endif
}

static inline void cc_add_formats(struct ast_format_cap *fmts, unsigned int divaFormats)
{
	int i;

	for (i = 0; i < 32; i++) {
		unsigned int ccCodec = (1U << i);

		if ((divaFormats & ccCodec) != 0) {
#ifdef CC_AST_HAS_VERSION_13_0
			struct ast_format * id = ccCodec2AstCodec (ccCodec);
			ast_format_cap_remove_by_type(fmts, AST_MEDIA_TYPE_AUDIO);
			ast_format_cap_append(fmts, id, 0);
#else
			enum ast_format_id id = ccCodec2AstCodec (ccCodec);
			struct ast_format fmt;

			ast_format_clear(&fmt);
			ast_format_set(&fmt, id, 0);
			ast_format_cap_add(fmts, &fmt);
#endif
		}
	}
}

static inline int cc_set_best_codec(struct ast_channel *a)
{
#ifdef CC_AST_HAS_VERSION_13_0
	struct ast_format * bestCodec;
	
	bestCodec = ast_format_cap_get_format(ast_channel_nativeformats(a), 0);
	if (bestCodec == ast_format_none) {
		bestCodec = ast_format_alaw;
	}
	
	ast_set_read_format(a, bestCodec);
	ast_set_write_format(a, bestCodec);
	
	return (int)ast_format_compatibility_format2bitfield(bestCodec);
#else
	struct ast_format bestCodec;

	ast_format_clear(&bestCodec);

	if (ast_best_codec(ast_channel_nativeformats(a), &bestCodec) == NULL) {
		/*
			Fallback to aLaw
			*/
		ast_format_set(&bestCodec, CC_FORMAT_ALAW, 0);
	}

	ast_format_copy(ast_channel_readformat(a),  &bestCodec);
	ast_format_copy(ast_channel_readformat(a),     &bestCodec);
	ast_format_copy(ast_channel_rawwriteformat(a), &bestCodec);
	ast_format_copy(ast_channel_writeformat(a),    &bestCodec);

	return (int)ast_format_to_old_bitfield(&bestCodec);
#endif
}

static inline void cc_set_read_format(struct ast_channel* a, int ccCodec)
{
#ifdef CC_AST_HAS_VERSION_13_0
	struct ast_format * ccFmt;
	
	ccFmt = ccCodec2AstCodec(ccCodec);
	
	ast_set_read_format(a, ccFmt);
#else
	struct ast_format ccFmt;

	ast_format_clear(&ccFmt);
	ast_format_set(&ccFmt, ccCodec, 0);
	ast_set_read_format(a, &ccFmt);
#endif
}

static inline void cc_set_write_format(struct ast_channel* a, int ccCodec)
{
#ifdef CC_AST_HAS_VERSION_13_0
	struct ast_format * ccFmt;
	
	ccFmt = ccCodec2AstCodec(ccCodec);
	
	ast_set_write_format(a, ccFmt);
#else
	struct ast_format ccFmt;

	ast_format_clear(&ccFmt);
	ast_format_set(&ccFmt, ccCodec, 0);
	ast_set_write_format(a, &ccFmt);
#endif
}

#ifdef CC_AST_HAS_VERSION_13_0
#define cc_parse_allow_disallow(__capability__, __value__, __allowing__, __cap__) do{\
	__capability__ = ast_format_cap_update_by_allow_disallow(__cap__, __value__, __allowing__);}while(0)

static inline uint64_t cc_get_formats_as_bits(const struct ast_format_cap *cap)
{
	uint64_t bitfield = 0;
	int x;

	for (x = 0; x < ast_format_cap_count(cap); x++) {
		struct ast_format *format = ast_format_cap_get_format(cap, x);
		bitfield |= ast_format_compatibility_format2bitfield(format);
		ao2_ref(format, -1);
	}

	return bitfield;
}

#else
#define cc_parse_allow_disallow(__prefs__, __capability__, __value__, __allowing__, __cap__) do{\
	ast_parse_allow_disallow(__prefs__, __cap__, __value__, __allowing__); \
	*(__capability__) = (int)ast_format_cap_to_old_bitfield(__cap__); }while(0)

#define cc_get_formats_as_bits(__a__) (int)ast_format_cap_to_old_bitfield(__a__)
#endif

static inline int cc_get_best_codec_as_bits(int src)
{
	int ret = 0;
#ifdef CC_AST_HAS_VERSION_13_0
	struct ast_format_cap *dst;
	struct ast_format *bestCodec;
	int bit;
	
	dst = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	for (bit = 0; bit < 64; ++bit) {
		uint64_t mask = (1ULL << bit);

		if (mask & src) {
			struct ast_format *format;

			format = ast_format_compatibility_bitfield2format(mask);
			ast_format_cap_remove_by_type(dst, AST_MEDIA_TYPE_AUDIO);
			if (format && ast_format_cap_append(dst, format, 0)) {
				return 0;
			}
		}
	}

	bestCodec = ast_format_cap_get_format(dst, 0);
	ao2_ref(dst, -1);

	ret = (int)ast_format_compatibility_format2bitfield(bestCodec);
#else
	struct ast_format_cap *dst = ast_format_cap_alloc_nolock();

	if (dst != 0) {
		struct ast_format bestCodec;

		ast_format_cap_from_old_bitfield(dst, src);
		ast_format_clear(&bestCodec);
		if (ast_best_codec(dst, &bestCodec) != NULL) {
			ret = (int)ast_format_to_old_bitfield(&bestCodec);
		}
		ast_format_cap_destroy(dst);
	}

#endif
	return (ret);
}

static inline int cc_getformatbyname(const char* value)
{
#ifdef CC_AST_HAS_VERSION_13_0
	struct ast_format * fmt;
	int ret = 0;
	
	fmt = ast_format_cache_get(value);
	if (fmt) {
		ret = (int)ast_format_compatibility_format2bitfield(fmt);
	}
#else
	struct ast_format fmt;
	int ret = 0;

	if (ast_getformatbyname(value, &fmt) != NULL) {
		ret = (int)ast_format_to_old_bitfield(&fmt);
	}
#endif

	return (ret);
}

#else /* CC_AST_HAS_VERSION_10_0 */

#define CC_FORMAT_ALAW      AST_FORMAT_ALAW
#ifdef AST_FORMAT_G722
#define CC_FORMAT_G722      AST_FORMAT_G722
#endif
#define CC_FORMAT_G723_1    AST_FORMAT_G723_1
#define CC_FORMAT_G726      AST_FORMAT_G726
#define CC_FORMAT_G729A     AST_FORMAT_G729A
#define CC_FORMAT_GSM       AST_FORMAT_GSM
#define CC_FORMAT_ILBC      AST_FORMAT_ILBC
#ifdef AST_FORMAT_SIREN14
#define CC_FORMAT_SIREN14   AST_FORMAT_SIREN14
#endif
#ifdef AST_FORMAT_SIREN7
#define CC_FORMAT_SIREN7    AST_FORMAT_SIREN7
#endif
#ifdef AST_FORMAT_SLINEAR
#define CC_FORMAT_SLINEAR   AST_FORMAT_SLINEAR
#endif
#ifdef AST_FORMAT_SLINEAR16
#define CC_FORMAT_SLINEAR16 AST_FORMAT_SLINEAR16
#endif
#define CC_FORMAT_ULAW      AST_FORMAT_ULAW


#define cc_getformatname(__x__) ast_getformatname((__x__))
#define cc_add_formats(__x__,__y__) do {(__x__)=(__y__);}while(0)

static inline int cc_set_best_codec(struct ast_channel *a)
{
	int fmt = ast_best_codec(a->nativeformats);

	a->readformat     = fmt;
	a->writeformat    = fmt;
	a->rawreadformat  = fmt;
	a->rawwriteformat = fmt;

	return fmt;
}

#define cc_set_read_format(__a__, __b__) ast_set_read_format(__a__, __b__)
#define cc_set_write_format(__a__, __b__) ast_set_write_format(__a__, __b__)
#define cc_parse_allow_disallow(__a__, __b__, __c__, __d__, __e__) ast_parse_allow_disallow(__a__, __b__, __c__, __d__)
#define cc_get_formats_as_bits(__a__) (__a__)
#define cc_get_best_codec_as_bits(__a__) ast_best_codec(__a__)
#define cc_getformatbyname(__a__) ast_getformatbyname(__a__)
#endif /* } */
