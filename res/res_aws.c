/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Joshua Elson
 *
 * Joshua Elson <joshelson@gmail.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This software is available under two licenses:
 *
 * 1. Creative Commons BY-NC (free for non-commercial use, requires attribution)
 * 2. Commercial License (required for commercial use)
 *
 * See LICENSE file for CC BY-NC license terms.
 * For commercial licensing, contact joshelson@gmail.com
 */

/*! \file
 *
 * \brief AWS integration resource module
 *
 * Provides comprehensive AWS integration including:
 * - AWS credential discovery (environment, ECS, IMDS, static config)
 * - STS AssumeRole support
 * - SigV4 request signing for AWS APIs
 * - SQS message sending via AwsSqsSend() dialplan application
 * - S3 operations via S3Upload(), S3Download(), S3Delete() applications
 *
 * Dependencies: libcurl, OpenSSL
 *
 * \author Joshua Elson <joshelson@gmail.com>
 */

/*** MODULEINFO
	<depend>curl</depend>
	<depend>crypto</depend>
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<application name="AwsSqsSend" language="en_US">
		<synopsis>
			Send a message to an AWS SQS queue.
		</synopsis>
		<syntax>
			<parameter name="queue" required="true">
				<para>Queue URL or queue name alias from res_aws.conf</para>
			</parameter>
			<parameter name="body" required="true">
				<para>Message body text</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="delay">
						<para>Delay delivery by specified seconds</para>
					</option>
					<option name="group">
						<para>Message group ID for FIFO queues</para>
					</option>
					<option name="dedup">
						<para>Message deduplication ID for FIFO queues</para>
					</option>
					<option name="attrs">
						<para>Message attributes as key=value,key2=value2</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Sends a message to an AWS SQS queue using AWS credentials discovered
			through the standard AWS credential chain (environment variables, instance
			metadata, ECS task roles, or static configuration).</para>
			<para>Sets the following channel variables:</para>
			<variablelist>
				<variable name="AWS_SQS_STATUS">
					<para>Set to <literal>OK</literal> on success, <literal>ERROR</literal> on failure</para>
				</variable>
				<variable name="AWS_SQS_MESSAGE_ID">
					<para>SQS message ID on successful send</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">S3Upload</ref>
			<ref type="application">S3Download</ref>
			<ref type="application">S3Delete</ref>
			<ref type="filename">res_aws.conf</ref>
		</see-also>
	</application>
	<application name="S3Upload" language="en_US">
		<synopsis>
			Upload a file to AWS S3.
		</synopsis>
		<syntax>
			<parameter name="bucket" required="true">
				<para>S3 bucket name</para>
			</parameter>
			<parameter name="key" required="true">
				<para>S3 object key (path/filename)</para>
			</parameter>
			<parameter name="filepath" required="true">
				<para>Local file path to upload</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="content_type">
						<para>MIME content type (default: application/octet-stream)</para>
					</option>
					<option name="metadata">
						<para>Custom metadata as key=value,key2=value2</para>
					</option>
					<option name="tags">
						<para>Object tags as key=value&key2=value2</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Uploads a file to Amazon S3 using AWS credentials discovered
			through the standard AWS credential chain. Supports custom metadata,
			content types, and tagging.</para>
			<para>Sets the following channel variables:</para>
			<variablelist>
				<variable name="S3UPLOAD_STATUS">
					<para>Set to <literal>SUCCESS</literal> on success, <literal>FAILED</literal> on failure</para>
				</variable>
				<variable name="S3UPLOAD_ETAG">
					<para>S3 ETag of uploaded object on successful upload</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">S3Download</ref>
			<ref type="application">S3Delete</ref>
			<ref type="application">AwsSqsSend</ref>
			<ref type="filename">res_aws.conf</ref>
		</see-also>
	</application>
	<application name="S3Download" language="en_US">
		<synopsis>
			Download a file from AWS S3.
		</synopsis>
		<syntax>
			<parameter name="bucket" required="true">
				<para>S3 bucket name</para>
			</parameter>
			<parameter name="key" required="true">
				<para>S3 object key (path/filename)</para>
			</parameter>
			<parameter name="filepath" required="true">
				<para>Local file path to save downloaded content</para>
			</parameter>
		</syntax>
		<description>
			<para>Downloads a file from Amazon S3 using AWS credentials discovered
			through the standard AWS credential chain.</para>
			<para>Sets the following channel variables:</para>
			<variablelist>
				<variable name="S3DOWNLOAD_STATUS">
					<para>Set to <literal>SUCCESS</literal> on success, <literal>FAILED</literal> on failure</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">S3Upload</ref>
			<ref type="application">S3Delete</ref>
			<ref type="application">AwsSqsSend</ref>
			<ref type="filename">res_aws.conf</ref>
		</see-also>
	</application>
	<application name="S3Delete" language="en_US">
		<synopsis>
			Delete an object from AWS S3.
		</synopsis>
		<syntax>
			<parameter name="bucket" required="true">
				<para>S3 bucket name</para>
			</parameter>
			<parameter name="key" required="true">
				<para>S3 object key (path/filename) to delete</para>
			</parameter>
		</syntax>
		<description>
			<para>Deletes an object from Amazon S3 using AWS credentials discovered
			through the standard AWS credential chain.</para>
			<para>Sets the following channel variables:</para>
			<variablelist>
				<variable name="S3DELETE_STATUS">
					<para>Set to <literal>SUCCESS</literal> on success, <literal>FAILED</literal> on failure</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">S3Upload</ref>
			<ref type="application">S3Download</ref>
			<ref type="application">AwsSqsSend</ref>
			<ref type="filename">res_aws.conf</ref>
		</see-also>
	</application>
 ***/

#include "asterisk.h"

#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/conversions.h"
#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/json.h"
#include "asterisk/xml.h"
#include "asterisk/uri.h"

#define MODNAME "res_aws"
#define APPNAME "AWSToolkit"

/* ----------------------------- Config ----------------------------- */

struct aws_config {
    struct ast_str *region;
    struct ast_str *default_queue_url;
    struct ast_str *sts_role_arn;
    struct ast_str *sts_external_id;
    struct ast_str *sts_session_name;
    int refresh_skew; /* seconds before expiry to refresh */
    int http_timeout_ms;
    int debug;
    /* static creds (optional) */
    struct ast_str *access_key;
    struct ast_str *secret_key;
    struct ast_str *session_token;
};

static struct aws_config g_cfg;

/* ----------------------------- HTTP helpers ----------------------------- */

static size_t write_cb_ast_str(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct ast_str **str_ptr = (struct ast_str**)userp;

	if (!*str_ptr) {
		*str_ptr = ast_str_create(realsize + 1);
		if (!*str_ptr) {
			return 0;
		}
	}

	/* Append the new content using ast_str_append_substr */
	if (ast_str_append_substr(str_ptr, 0, (const char*)contents, realsize) != 0) {
		return 0;
	}

	return realsize;
}

static int http_post(const char *url, const struct curl_slist *headers,
		      const char *body, long timeout_ms, long *http_code,
		      struct ast_str **out)
{
	RAII_VAR(CURL *, curl, curl_easy_init(), curl_easy_cleanup);
	struct curl_slist *hdrs_dup = NULL;
	const struct curl_slist *h;
	CURLcode res;

	if (!curl) {
		return -1;
	}

	for (h = headers; h; h = h->next) {
		hdrs_dup = curl_slist_append(hdrs_dup, h->data);
	}
	*out = NULL; /* Will be allocated by callback */

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs_dup);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb_ast_str);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)out);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, MODNAME "/1.0");

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_log(LOG_ERROR, "HTTP POST error: %s\n", 
			curl_easy_strerror(res));
		curl_slist_free_all(hdrs_dup);
		return -1;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_code);
	curl_slist_free_all(hdrs_dup);
	return 0;
}

static int http_get(const char *url, const struct curl_slist *headers,
		     long timeout_ms, long *http_code, struct ast_str **out)
{
	RAII_VAR(CURL *, curl, curl_easy_init(), curl_easy_cleanup);
	struct curl_slist *hdrs_dup = NULL;
	const struct curl_slist *h;
	CURLcode res;

	if (!curl) {
		return -1;
	}

	for (h = headers; h; h = h->next) {
		hdrs_dup = curl_slist_append(hdrs_dup, h->data);
	}
	*out = NULL; /* Will be allocated by callback */

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs_dup);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb_ast_str);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)out);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, MODNAME "/1.0");

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_log(LOG_ERROR, "HTTP GET error: %s\n", 
			curl_easy_strerror(res));
		curl_slist_free_all(hdrs_dup);
		return -1;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_code);
	curl_slist_free_all(hdrs_dup);
	return 0;
}

/* ----------------------------- Time & crypto ----------------------------- */


static int sha256_hex_to_str(const unsigned char *data, size_t len, struct ast_str **hex_out)
{
	unsigned char hash[SHA256_DIGEST_LENGTH];
	int i;

	if (!*hex_out) {
		*hex_out = ast_str_create(65); /* 64 hex chars + null terminator */
		if (!*hex_out) {
			return -1;
		}
	}

	SHA256(data, len, hash);
	ast_str_reset(*hex_out);
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		ast_str_append(hex_out, 0, "%02x", hash[i]);
	}
	return 0;
}


static void hmac_sha256(const unsigned char *key, size_t key_len,
			const unsigned char *data, size_t data_len,
			unsigned char *out, unsigned int *out_len)
{
	HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out, out_len);
}

/*! \brief Parse ISO8601 timestamp to epoch using ast_strptime */
static time_t parse_iso8601_to_epoch(const char *iso_str)
{
	struct ast_tm tm_result = {0};
	char *parse_result;
	struct tm std_tm = {0};
	
	if (!iso_str || strlen(iso_str) < 19) {
		return 0;
	}
	
	/* Parse ISO8601 format: YYYY-MM-DDTHH:MM:SSZ */
	parse_result = ast_strptime(iso_str, "%Y-%m-%dT%H:%M:%S", &tm_result);
	if (!parse_result) {
		return 0;
	}
	
	/* Convert ast_tm to standard tm for timegm */
	std_tm.tm_year = tm_result.tm_year;
	std_tm.tm_mon = tm_result.tm_mon;
	std_tm.tm_mday = tm_result.tm_mday;
	std_tm.tm_hour = tm_result.tm_hour;
	std_tm.tm_min = tm_result.tm_min;
	std_tm.tm_sec = tm_result.tm_sec;
	
	return timegm(&std_tm);
}

/*! \brief derive kSigning per AWS SigV4 */
static int aws_signing_key(const char *secret_key, const char *yyyymmdd,
			    const char *region, const char *service,
			    unsigned char *out_key, unsigned int *out_len)
{
	unsigned char kDate[SHA256_DIGEST_LENGTH];
	unsigned char kRegion[SHA256_DIGEST_LENGTH];
	unsigned char kService[SHA256_DIGEST_LENGTH];
	unsigned int l;
	RAII_VAR(struct ast_str *, keybuf, ast_str_create(128), ast_free);

	if (!keybuf) {
		return -1;
	}

	ast_str_set(&keybuf, 0, "AWS4%s", secret_key);
	hmac_sha256((unsigned char*)ast_str_buffer(keybuf), ast_str_strlen(keybuf),
		    (unsigned char*)yyyymmdd, strlen(yyyymmdd), kDate, &l);
	hmac_sha256(kDate, l, (unsigned char*)region, strlen(region), 
		    kRegion, &l);
	hmac_sha256(kRegion, l, (unsigned char*)service, strlen(service),
		    kService, &l);
	hmac_sha256(kService, l, (unsigned char*)"aws4_request",
		    strlen("aws4_request"), out_key, out_len);
	return 0;
}

static char *url_encode_component(const char *in)
{
	char *out;
	struct ast_str *s = ast_str_create(128);
	const unsigned char *p;

	for (p = (const unsigned char*)in; *p; ++p) {
		if (isalnum(*p) || *p == '-' || *p == '_' || 
		    *p == '.' || *p == '~' || *p == '/') {
			ast_str_append(&s, 0, "%c", *p);
		} else if (*p == ' ') {
			ast_str_append(&s, 0, "%c", '+');
		} else {
			ast_str_append(&s, 0, "%%%02X", *p);
		}
	}
	out = ast_strdup(ast_str_buffer(s));
	ast_free(s);
	return out;
}

/* ----------------------------- Credentials ----------------------------- */

enum cred_source {
	CRED_NONE = 0,
	CRED_ENV,
	CRED_STATIC,
	CRED_ECS,
	CRED_IMDS,
	CRED_STS_ASSUMED
};

struct aws_creds {
    enum cred_source source;
    struct ast_str *access_key;
    struct ast_str *secret_key;
    struct ast_str *session_token;
    time_t expiration; /* epoch seconds, 0 for none */
};

static struct aws_creds g_creds;
AST_MUTEX_DEFINE_STATIC(g_creds_lock);

/*! \brief Parse AWS credentials from JSON response into ast_str */
static int parse_aws_creds_json(const char *json_str, 
					struct ast_str **access_key, struct ast_str **secret_key,
					struct ast_str **token, struct ast_str **expiration)
{
	struct ast_json_error error;
	RAII_VAR(struct ast_json *, json, ast_json_load_string(json_str, &error), ast_json_unref);
	struct ast_json *field;
	const char *value;
	
	if (!json) {
		ast_log(LOG_ERROR, "Failed to parse JSON: %s\n", error.text);
		return -1;
	}
	
	/* Initialize ast_str objects */
	*access_key = ast_str_create(128);
	*secret_key = ast_str_create(128);
	*token = ast_str_create(2048);
	*expiration = ast_str_create(64);
	
	if (!*access_key || !*secret_key || !*token || !*expiration) {
		if (*access_key) ast_free(*access_key);
		if (*secret_key) ast_free(*secret_key);
		if (*token) ast_free(*token);
		if (*expiration) ast_free(*expiration);
		return -1;
	}
	
	/* Access Key ID - required */
	field = ast_json_object_get(json, "AccessKeyId");
	value = field ? ast_json_string_get(field) : NULL;
	if (!value) {
		ast_free(*access_key);
		ast_free(*secret_key);
		ast_free(*token);
		ast_free(*expiration);
		return -1;
	}
	ast_str_set(access_key, 0, "%s", value);
	
	/* Secret Access Key - required */
	field = ast_json_object_get(json, "SecretAccessKey");
	value = field ? ast_json_string_get(field) : NULL;
	if (!value) {
		ast_free(*access_key);
		ast_free(*secret_key);
		ast_free(*token);
		ast_free(*expiration);
		return -1;
	}
	ast_str_set(secret_key, 0, "%s", value);
	
	/* Session Token - optional */
	field = ast_json_object_get(json, "Token");
	value = field ? ast_json_string_get(field) : NULL;
	if (value) {
		ast_str_set(token, 0, "%s", value);
	}
	
	/* Expiration - optional */
	field = ast_json_object_get(json, "Expiration");
	value = field ? ast_json_string_get(field) : NULL;
	if (value) {
		ast_str_set(expiration, 0, "%s", value);
	}
	
	return 0;
}


static int creds_from_env(struct aws_creds *out)
{
	const char *ak = getenv("AWS_ACCESS_KEY_ID");
	const char *sk = getenv("AWS_SECRET_ACCESS_KEY");
	const char *st;

	if (!ak || !sk) {
		return -1;
	}
	st = getenv("AWS_SESSION_TOKEN");
	/* Initialize aws_creds ast_str fields */
	out->access_key = ast_str_create(128);
	out->secret_key = ast_str_create(128);
	out->session_token = ast_str_create(2048);
	
	if (!out->access_key || !out->secret_key || !out->session_token) {
		if (out->access_key) ast_free(out->access_key);
		if (out->secret_key) ast_free(out->secret_key);
		if (out->session_token) ast_free(out->session_token);
		return -1;
	}
	
	ast_str_set(&out->access_key, 0, "%s", ak);
	ast_str_set(&out->secret_key, 0, "%s", sk);
	if (st) {
		ast_str_set(&out->session_token, 0, "%s", st);
	}
	out->expiration = 0;
	out->source = CRED_ENV;
	return 0;
}

static int creds_from_static_cfg(struct aws_creds *out)
{
	if (!g_cfg.access_key || !g_cfg.secret_key || 
	    ast_str_strlen(g_cfg.access_key) == 0 || ast_str_strlen(g_cfg.secret_key) == 0) {
		return -1;
	}
	
	out->access_key = ast_str_create(128);
	out->secret_key = ast_str_create(128);
	out->session_token = ast_str_create(2048);
	
	if (!out->access_key || !out->secret_key || !out->session_token) {
		if (out->access_key) ast_free(out->access_key);
		if (out->secret_key) ast_free(out->secret_key);
		if (out->session_token) ast_free(out->session_token);
		return -1;
	}
	
	ast_str_set(&out->access_key, 0, "%s", ast_str_buffer(g_cfg.access_key));
	ast_str_set(&out->secret_key, 0, "%s", ast_str_buffer(g_cfg.secret_key));
	if (g_cfg.session_token && ast_str_strlen(g_cfg.session_token) > 0) {
		ast_str_set(&out->session_token, 0, "%s", ast_str_buffer(g_cfg.session_token));
	}
	
	out->expiration = 0;
	out->source = CRED_STATIC;
	return 0;
}

static int creds_from_ecs(struct aws_creds *out)
{
	const char *rel = getenv("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
	const char *full = getenv("AWS_CONTAINER_CREDENTIALS_FULL_URI");
	RAII_VAR(struct ast_str *, url_str, ast_str_create(1024), ast_free);
	RAII_VAR(struct ast_str *, body, NULL, ast_free);
	long code = 0;
	int rc;
	RAII_VAR(struct ast_str *, ak, ast_str_create(128), ast_free);
	RAII_VAR(struct ast_str *, sk, ast_str_create(128), ast_free);
	RAII_VAR(struct ast_str *, tok, ast_str_create(2048), ast_free);
	RAII_VAR(struct ast_str *, exp, ast_str_create(64), ast_free);

	if ((!rel && !full) || !url_str || !ak || !sk || !tok || !exp) {
		return -1;
	}

	if (rel) {
		ast_str_set(&url_str, 0, "http://169.254.170.2%s", rel);
	} else {
		ast_str_set(&url_str, 0, "%s", full);
	}

	rc = http_get(ast_str_buffer(url_str), NULL, g_cfg.http_timeout_ms, &code, &body);
	if (rc || code != 200 || !body) {
		return -1;
	}

	struct ast_str *ak_str = NULL, *sk_str = NULL, *tok_str = NULL, *exp_str = NULL;
	if (parse_aws_creds_json(ast_str_buffer(body), &ak_str, &sk_str, &tok_str, &exp_str)) {
		goto fail;
	}

	/* Initialize aws_creds ast_str fields */
	out->access_key = ast_str_create(128);
	out->secret_key = ast_str_create(128);
	out->session_token = ast_str_create(2048);
	
	if (!out->access_key || !out->secret_key || !out->session_token) {
		if (out->access_key) ast_free(out->access_key);
		if (out->secret_key) ast_free(out->secret_key);
		if (out->session_token) ast_free(out->session_token);
		goto fail;
	}

	ast_str_set(&out->access_key, 0, "%s", ast_str_buffer(ak_str));
	ast_str_set(&out->secret_key, 0, "%s", ast_str_buffer(sk_str));
	if (ast_str_strlen(tok_str) > 0) {
		ast_str_set(&out->session_token, 0, "%s", ast_str_buffer(tok_str));
	}

	/* parse ISO8601 to epoch */
	if (ast_str_strlen(exp_str) >= 19) {
		out->expiration = parse_iso8601_to_epoch(ast_str_buffer(exp_str));
	}
	
	/* Cleanup temp strings */
	if (ak_str) ast_free(ak_str);
	if (sk_str) ast_free(sk_str);
	if (tok_str) ast_free(tok_str);
	if (exp_str) ast_free(exp_str);
	out->source = CRED_ECS;
	return 0;

fail:
	if (ak_str) ast_free(ak_str);
	if (sk_str) ast_free(sk_str);
	if (tok_str) ast_free(tok_str);
	if (exp_str) ast_free(exp_str);
	if (g_cfg.debug) {
		ast_log(LOG_WARNING, "Failed to parse ECS creds JSON: %.120s\n",
			body ? ast_str_buffer(body) : "");
	}
	return -1;
}

static int creds_from_imds(struct aws_creds *out)
{
	/* IMDSv2 token */
	long code = 0;
	RAII_VAR(struct ast_str *, tok, NULL, ast_free);
	RAII_VAR(struct ast_str *, role, NULL, ast_free);
	RAII_VAR(struct ast_str *, body, NULL, ast_free);
	struct curl_slist *hdrs = NULL;
	int rc;
	RAII_VAR(struct ast_str *, hdrline_str, ast_str_create(512), ast_free);
	RAII_VAR(struct ast_str *, url_str, ast_str_create(512), ast_free);
	struct curl_slist *hdrs2 = NULL;

	if (!hdrline_str || !url_str) {
		return -1;
	}

	hdrs = curl_slist_append(hdrs, "X-aws-ec2-metadata-token-ttl-seconds: 21600");
	rc = http_post("http://169.254.169.254/latest/api/token", hdrs, "",
		       g_cfg.http_timeout_ms, &code, &tok);
	curl_slist_free_all(hdrs);
	if (rc || code != 200 || !tok) {
		return -1;
	}

	/* role name */
	ast_str_set(&hdrline_str, 0, "X-aws-ec2-metadata-token: %s", ast_str_buffer(tok));
	hdrs2 = curl_slist_append(hdrs2, ast_str_buffer(hdrline_str));
	code = 0;
	rc = http_get("http://169.254.169.254/latest/meta-data/iam/security-credentials/",
		      hdrs2, g_cfg.http_timeout_ms, &code, &role);
	if (rc || code != 200 || !role) {
		curl_slist_free_all(hdrs2);
		return -1;
	}

	/* creds */
	ast_str_set(&url_str, 0, 
		 "http://169.254.169.254/latest/meta-data/iam/security-credentials/%s",
		 ast_str_buffer(role));
	code = 0;
	rc = http_get(ast_str_buffer(url_str), hdrs2, g_cfg.http_timeout_ms, &code, &body);
	curl_slist_free_all(hdrs2);
	if (rc || code != 200 || !body) {
		return -1;
	}

	struct ast_str *ak_str = NULL, *sk_str = NULL, *tok_str = NULL, *exp_str = NULL;
	if (parse_aws_creds_json(ast_str_buffer(body), &ak_str, &sk_str, &tok_str, &exp_str)) {
		goto fail;
	}

	/* Initialize aws_creds ast_str fields */
	out->access_key = ast_str_create(128);
	out->secret_key = ast_str_create(128);
	out->session_token = ast_str_create(2048);
	
	if (!out->access_key || !out->secret_key || !out->session_token) {
		if (out->access_key) ast_free(out->access_key);
		if (out->secret_key) ast_free(out->secret_key);
		if (out->session_token) ast_free(out->session_token);
		goto fail;
	}

	ast_str_set(&out->access_key, 0, "%s", ast_str_buffer(ak_str));
	ast_str_set(&out->secret_key, 0, "%s", ast_str_buffer(sk_str));
	if (ast_str_strlen(tok_str) > 0) {
		ast_str_set(&out->session_token, 0, "%s", ast_str_buffer(tok_str));
	}
	if (ast_str_strlen(exp_str) >= 19) {
		out->expiration = parse_iso8601_to_epoch(ast_str_buffer(exp_str));
	}
	
	/* Cleanup */
	if (ak_str) ast_free(ak_str);
	if (sk_str) ast_free(sk_str);
	if (tok_str) ast_free(tok_str);
	if (exp_str) ast_free(exp_str);
	out->source = CRED_IMDS;
	return 0;

fail:
	if (ak_str) ast_free(ak_str);
	if (sk_str) ast_free(sk_str);
	if (tok_str) ast_free(tok_str);
	if (exp_str) ast_free(exp_str);
	if (g_cfg.debug) {
		ast_log(LOG_WARNING, "Failed to parse IMDS creds JSON: %.120s\n",
			body ? ast_str_buffer(body) : "");
	}
	return -1;
}

/*! \brief STS AssumeRole (XML response) */
static int assume_role(const struct aws_creds *base, struct aws_creds *assumed)
{
	RAII_VAR(struct ast_str *, date_iso_str, ast_str_create(32), ast_free);
	RAII_VAR(struct ast_str *, date_short_str, ast_str_create(16), ast_free);
	const char *service = "sts";
	const char *host = "sts.amazonaws.com";
	RAII_VAR(struct ast_str *, form, ast_str_create(512), ast_free);
	RAII_VAR(char *, arn_enc, NULL, ast_free);
	RAII_VAR(char *, sn, NULL, ast_free);
	RAII_VAR(char *, ex, NULL, ast_free);
	const char *sess;

	if (ast_str_strlen(g_cfg.sts_role_arn) == 0 || !date_iso_str || !date_short_str || !form) {
		return -1;
	}

	time_t now = time(NULL);
	struct tm g;
	gmtime_r(&now, &g);
	char temp_buf[32];
	strftime(temp_buf, sizeof(temp_buf), "%Y%m%dT%H%M%SZ", &g);
	ast_str_set(&date_iso_str, 0, "%s", temp_buf); strftime(temp_buf, 16, "%Y%m%d", &g);
	
	ast_str_set(&date_short_str, 0, "%s", temp_buf);

	/* Query params for Action=AssumeRole & Version=2011-06-15 */
	ast_str_set(&form, 0, "Action=AssumeRole&Version=2011-06-15");
	arn_enc = url_encode_component(ast_str_buffer(g_cfg.sts_role_arn));
	if (!arn_enc) {
		return -1;
	}
	ast_str_append(&form, 0, "&RoleArn=%s", arn_enc);

	sess = ast_str_strlen(g_cfg.sts_session_name) == 0 ? 
		"asterisk-session" : ast_str_buffer(g_cfg.sts_session_name);
	sn = url_encode_component(sess);
	if (!sn) {
		return -1;
	}
	ast_str_append(&form, 0, "&RoleSessionName=%s", sn);

	if (ast_str_strlen(g_cfg.sts_external_id) > 0) {
		ex = url_encode_component(ast_str_buffer(g_cfg.sts_external_id));
		if (!ex) {
			return -1;
		}
		ast_str_append(&form, 0, "&ExternalId=%s", ex);
	}

	/* SigV4 canonical request for STS using RAII */
	RAII_VAR(struct ast_str *, payload_hash_str, ast_str_create(65), ast_free);
	RAII_VAR(struct ast_str *, canon, ast_str_create(1024), ast_free);
	RAII_VAR(struct ast_str *, canon_hash_str, ast_str_create(65), ast_free);
	RAII_VAR(struct ast_str *, scope_str, ast_str_create(128), ast_free);
	RAII_VAR(struct ast_str *, string_to_sign, ast_str_create(512), ast_free);
	unsigned char kSigning[SHA256_DIGEST_LENGTH];
	unsigned int klen = 0;
	unsigned char sig_bin[SHA256_DIGEST_LENGTH];
	unsigned int sig_len = 0;
	int i;
	RAII_VAR(struct ast_str *, auth_str, ast_str_create(1024), ast_free);
	struct curl_slist *hdrs = NULL;
	RAII_VAR(struct ast_str *, date_hdr_str, ast_str_create(64), ast_free);
	RAII_VAR(struct ast_str *, tokhdr_str, ast_str_create(2400), ast_free);
	const char *region;

	if (!payload_hash_str || !canon || !canon_hash_str || !scope_str || !string_to_sign || !auth_str || !date_hdr_str || !tokhdr_str) {
		return -1;
	}

	if (sha256_hex_to_str((unsigned char*)ast_str_buffer(form), ast_str_strlen(form), 
		   &payload_hash_str) != 0) {
		return -1;
	}

	ast_str_set(&canon, 0, 
		    "POST\n/\n\ncontent-type:application/x-www-form-urlencoded\n"
		    "host:%s\nx-amz-date:%s\n\ncontent-type;host;x-amz-date\n%s",
		    host, ast_str_buffer(date_iso_str), ast_str_buffer(payload_hash_str));

	if (sha256_hex_to_str((unsigned char*)ast_str_buffer(canon), ast_str_strlen(canon),
		   &canon_hash_str) != 0) {
		return -1;
	}

	region = ast_str_strlen(g_cfg.region) > 0 ? ast_str_buffer(g_cfg.region) : "us-east-1";
	ast_str_set(&scope_str, 0, "%s/%s/%s/aws4_request", 
		 ast_str_buffer(date_short_str), region, service);
	ast_str_set(&string_to_sign, 0, "AWS4-HMAC-SHA256\n%s\n%s\n%s", 
		    ast_str_buffer(date_iso_str), ast_str_buffer(scope_str), ast_str_buffer(canon_hash_str));

	aws_signing_key(ast_str_buffer(base->secret_key), ast_str_buffer(date_short_str), region, service, 
			kSigning, &klen);

	hmac_sha256(kSigning, klen, (unsigned char*)ast_str_buffer(string_to_sign),
		    ast_str_strlen(string_to_sign), sig_bin, &sig_len);
	RAII_VAR(struct ast_str *, sig_hex_temp, ast_str_create(65), ast_free);
	if (!sig_hex_temp) {
		return -1;
	}
	for (i = 0; i < 32; i++) {
		ast_str_append(&sig_hex_temp, 0, "%02x", sig_bin[i]);
	}

	ast_str_set(&auth_str, 0,
		 "Authorization: AWS4-HMAC-SHA256 Credential=%s/%s, "
		 "SignedHeaders=content-type;host;x-amz-date, Signature=%s",
		 ast_str_buffer(base->access_key), ast_str_buffer(scope_str), ast_str_buffer(sig_hex_temp));

	hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");
	hdrs = curl_slist_append(hdrs, ast_str_buffer(auth_str));
	ast_str_set(&date_hdr_str, 0, "x-amz-date: %s", ast_str_buffer(date_iso_str));
	hdrs = curl_slist_append(hdrs, ast_str_buffer(date_hdr_str));
	if (ast_str_strlen(base->session_token) > 0) {
		ast_str_set(&tokhdr_str, 0, "x-amz-security-token: %s", 
			 ast_str_buffer(base->session_token));
		hdrs = curl_slist_append(hdrs, ast_str_buffer(tokhdr_str));
	}

	long code = 0;
	RAII_VAR(struct ast_str *, out, NULL, ast_free);
	int rc;

	rc = http_post("https://sts.amazonaws.com/", hdrs, ast_str_buffer(form),
		       g_cfg.http_timeout_ms, &code, &out);
	curl_slist_free_all(hdrs);
	if (rc || code != 200 || !out) {
		return -1;
	}

	/* Parse STS AssumeRole XML response using Asterisk's XML parser */
	RAII_VAR(struct ast_xml_doc *, doc, ast_xml_read_memory(ast_str_buffer(out), ast_str_strlen(out)), ast_xml_close);
	if (!doc) {
		ast_log(LOG_ERROR, "Failed to parse STS XML response\n");
		return -1;
	}

	struct ast_xml_node *root = ast_xml_get_root(doc);
	if (!root) {
		return -1;
	}

	/* Navigate to Credentials node - typical structure is:
	 * AssumeRoleResponse -> AssumeRoleResult -> Credentials */
	struct ast_xml_node *result = ast_xml_find_element(ast_xml_node_get_children(root), 
							    "AssumeRoleResult", NULL, NULL);
	struct ast_xml_node *creds_node = result ? 
		ast_xml_find_element(ast_xml_node_get_children(result), "Credentials", NULL, NULL) : NULL;
	
	if (!creds_node) {
		/* Try alternate structure */
		creds_node = ast_xml_find_element(ast_xml_node_get_children(root), 
						   "Credentials", NULL, NULL);
	}

	if (!creds_node) {
		ast_log(LOG_ERROR, "Could not find Credentials in STS response\n");
		return -1;
	}

	/* Extract credential fields - moved inline above */
	struct ast_xml_node *field;
	const char *text;

	/* Extract credential fields using ast_str */
	RAII_VAR(struct ast_str *, ak_str, ast_str_create(128), ast_free);
	RAII_VAR(struct ast_str *, sk_str, ast_str_create(128), ast_free);
	RAII_VAR(struct ast_str *, tok_str, ast_str_create(2048), ast_free);
	RAII_VAR(struct ast_str *, exp_str, ast_str_create(64), ast_free);
	
	if (!ak_str || !sk_str || !tok_str || !exp_str) {
		return -1;
	}
	
	field = ast_xml_find_element(ast_xml_node_get_children(creds_node), "AccessKeyId", NULL, NULL);
	text = field ? ast_xml_get_text(field) : NULL;
	if (!text) {
		ast_log(LOG_ERROR, "No AccessKeyId in STS response\n");
		return -1;
	}
	ast_str_set(&ak_str, 0, "%s", text);
	
	field = ast_xml_find_element(ast_xml_node_get_children(creds_node), "SecretAccessKey", NULL, NULL);
	text = field ? ast_xml_get_text(field) : NULL;
	if (!text) {
		ast_log(LOG_ERROR, "No SecretAccessKey in STS response\n");
		return -1;
	}
	ast_str_set(&sk_str, 0, "%s", text);
	
	field = ast_xml_find_element(ast_xml_node_get_children(creds_node), "SessionToken", NULL, NULL);
	text = field ? ast_xml_get_text(field) : NULL;
	if (text) {
		ast_str_set(&tok_str, 0, "%s", text);
	}
	
	field = ast_xml_find_element(ast_xml_node_get_children(creds_node), "Expiration", NULL, NULL);
	text = field ? ast_xml_get_text(field) : NULL;
	if (text) {
		ast_str_set(&exp_str, 0, "%s", text);
	}
	
	/* Initialize aws_creds ast_str fields */
	assumed->access_key = ast_str_create(128);
	assumed->secret_key = ast_str_create(128);
	assumed->session_token = ast_str_create(2048);
	
	if (!assumed->access_key || !assumed->secret_key || !assumed->session_token) {
		if (assumed->access_key) ast_free(assumed->access_key);
		if (assumed->secret_key) ast_free(assumed->secret_key);
		if (assumed->session_token) ast_free(assumed->session_token);
		return -1;
	}
	
	ast_str_set(&assumed->access_key, 0, "%s", ast_str_buffer(ak_str));
	ast_str_set(&assumed->secret_key, 0, "%s", ast_str_buffer(sk_str));
	if (ast_str_strlen(tok_str) > 0) {
		ast_str_set(&assumed->session_token, 0, "%s", ast_str_buffer(tok_str));
	}
	if (ast_str_strlen(exp_str) >= 19) {
		assumed->expiration = parse_iso8601_to_epoch(ast_str_buffer(exp_str));
	}
	assumed->source = CRED_STS_ASSUMED;
	return 0;
}

static int refresh_creds_locked(void)
{
	struct aws_creds c = {0};

	if (!creds_from_env(&c)) {
		goto have;
	}
	if (!creds_from_static_cfg(&c)) {
		goto have;
	}
	if (!creds_from_ecs(&c)) {
		goto have;
	}
	if (!creds_from_imds(&c)) {
		goto have;
	}
	return -1;

have:
	if (ast_str_strlen(g_cfg.sts_role_arn) > 0) {
		struct aws_creds a = {0};
		if (!assume_role(&c, &a)) {
			c = a; /* use assumed */
		}
	}
	g_creds = c;
	return 0;
}

static int ensure_fresh_creds(void)
{
	time_t now = time(NULL);
	int need = 0;
	int ok;
	int refresh_threshold;

	ast_mutex_lock(&g_creds_lock);
	if (!g_creds.access_key || ast_str_strlen(g_creds.access_key) == 0) {
		need = 1;
	} else if (g_creds.expiration > 0) {
		refresh_threshold = g_cfg.refresh_skew > 0 ? 
			g_cfg.refresh_skew : 120;
		if ((g_creds.expiration - now) < refresh_threshold) {
			need = 1;
		}
	}
	if (need) {
		refresh_creds_locked();
	}
	ok = g_creds.access_key && ast_str_strlen(g_creds.access_key) > 0;
	ast_mutex_unlock(&g_creds_lock);
	return ok ? 0 : -1;
}

/* ----------------------------- SigV4 + SQS ----------------------------- */

static int sigv4_headers_for_sqs(const char *queue_url, const char *payload,
				  const struct aws_creds *creds,
				  struct curl_slist **out_headers)
{
	/* Parse URL using Asterisk URI parser */
	RAII_VAR(struct ast_uri *, parsed_uri, ast_uri_parse_http(queue_url), ao2_cleanup);
	const char *host, *path;

	if (!parsed_uri) {
		return -1;
	}

	host = ast_uri_host(parsed_uri);
	path = ast_uri_path(parsed_uri);
	if (!host || !path) {
		return -1;
	}

	RAII_VAR(struct ast_str *, host_str, ast_str_create(256), ast_free);
	if (!host_str) {
		return -1;
	}
	ast_str_set(&host_str, 0, "%s", host);

	RAII_VAR(struct ast_str *, date_iso_str, ast_str_create(32), ast_free);
	RAII_VAR(struct ast_str *, date_short_str, ast_str_create(16), ast_free);
	if (!date_iso_str || !date_short_str) {
		return -1;
	}

	time_t now = time(NULL);
	struct tm g;
	gmtime_r(&now, &g);
	RAII_VAR(struct ast_str *, temp_date_str, ast_str_create(32), ast_free);
	RAII_VAR(struct ast_str *, temp_short_str, ast_str_create(16), ast_free);
	if (!temp_date_str || !temp_short_str) {
		return -1;
	}
	
	char temp_buf[32];
	strftime(temp_buf, sizeof(temp_buf), "%Y%m%dT%H%M%SZ", &g);
	ast_str_set(&temp_date_str, 0, "%s", temp_buf);
	strftime(temp_buf, 16, "%Y%m%d", &g);
	ast_str_set(&temp_short_str, 0, "%s", temp_buf);
	ast_str_set(&date_iso_str, 0, "%s", ast_str_buffer(temp_date_str));
	ast_str_set(&date_short_str, 0, "%s", ast_str_buffer(temp_short_str));

	const char *service = "sqs";
	const char *region = ast_str_strlen(g_cfg.region) > 0 ? ast_str_buffer(g_cfg.region) : "us-east-1";

	RAII_VAR(struct ast_str *, payload_hash_str, ast_str_create(65), ast_free);
	RAII_VAR(struct ast_str *, canon, ast_str_create(1024), ast_free);
	RAII_VAR(struct ast_str *, canon_hash_str, ast_str_create(65), ast_free);
	if (!payload_hash_str || !canon || !canon_hash_str) {
		return -1;
	}
	if (sha256_hex_to_str((unsigned char*)payload, strlen(payload), &payload_hash_str) != 0) {
		return -1;
	}

	ast_str_set(&canon, 0, "POST\n%s\n\ncontent-type:application/x-www-form-urlencoded\nhost:%s\nx-amz-date:%s\n\ncontent-type;host;x-amz-date\n%s", 
	           path, ast_str_buffer(host_str), ast_str_buffer(date_iso_str), ast_str_buffer(payload_hash_str));

	if (sha256_hex_to_str((unsigned char*)ast_str_buffer(canon), ast_str_strlen(canon), &canon_hash_str) != 0) {
		return -1;
	}

	RAII_VAR(struct ast_str *, scope_str, ast_str_create(128), ast_free);
	RAII_VAR(struct ast_str *, string_to_sign, ast_str_create(512), ast_free);
	if (!scope_str || !string_to_sign) {
		return -1;
	}

	ast_str_set(&scope_str, 0, "%s/%s/%s/aws4_request", ast_str_buffer(date_short_str), region, service);
	ast_str_set(&string_to_sign, 0, "AWS4-HMAC-SHA256\n%s\n%s\n%s", 
	           ast_str_buffer(date_iso_str), ast_str_buffer(scope_str), ast_str_buffer(canon_hash_str));

	unsigned char kSigning[SHA256_DIGEST_LENGTH];
	unsigned int klen = 0;
	if (aws_signing_key(ast_str_buffer(creds->secret_key), ast_str_buffer(date_short_str), region, service, kSigning, &klen) != 0) {
		return -1;
	}

	unsigned char sig_bin[SHA256_DIGEST_LENGTH];
	unsigned int sig_len = 0;
	hmac_sha256(kSigning, klen, (unsigned char*)ast_str_buffer(string_to_sign), ast_str_strlen(string_to_sign), sig_bin, &sig_len);
	RAII_VAR(struct ast_str *, sig_hex_str, ast_str_create(65), ast_free);
	if (!sig_hex_str) {
		return -1;
	}
	for (int i = 0; i < 32; i++) {
		ast_str_append(&sig_hex_str, 0, "%02x", sig_bin[i]);
	}
	RAII_VAR(struct ast_str *, auth_str, ast_str_create(1024), ast_free);
	RAII_VAR(struct ast_str *, hdr_str, ast_str_create(512), ast_free);
	if (!auth_str || !hdr_str) {
		return -1;
	}

	ast_str_set(&auth_str, 0, "Authorization: AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=content-type;host;x-amz-date, Signature=%s",
	           ast_str_buffer(creds->access_key), ast_str_buffer(scope_str), ast_str_buffer(sig_hex_str));

	struct curl_slist *hdrs = NULL;
	hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");
	hdrs = curl_slist_append(hdrs, ast_str_buffer(auth_str));

	ast_str_set(&hdr_str, 0, "Host: %s", ast_str_buffer(host_str));
	hdrs = curl_slist_append(hdrs, ast_str_buffer(hdr_str));

	ast_str_set(&hdr_str, 0, "x-amz-date: %s", ast_str_buffer(date_iso_str));
	hdrs = curl_slist_append(hdrs, ast_str_buffer(hdr_str));

	if (ast_str_strlen(creds->session_token) > 0) {
		ast_str_set(&hdr_str, 0, "x-amz-security-token: %s", ast_str_buffer(creds->session_token));
		hdrs = curl_slist_append(hdrs, ast_str_buffer(hdr_str));
	}

	*out_headers = hdrs;
	return 0;
}

static int sqs_send_message(const char *queue_url, const char *body, const char *group_id, const char *dedup_id, int delay_seconds, const char *attrs_kv_pairs, struct ast_str **out_message_id) {
    if (ensure_fresh_creds()) return -1;

    struct aws_creds creds;
    ast_mutex_lock(&g_creds_lock); creds = g_creds; ast_mutex_unlock(&g_creds_lock);

    struct ast_str *form = ast_str_create(256);
    ast_str_set(&form, 0, "Action=SendMessage&Version=2012-11-05");
    char *enc_body = url_encode_component(body);
    ast_str_append(&form, 0, "&MessageBody=%s", enc_body);
    ast_free(enc_body);

    if (delay_seconds > 0) ast_str_append(&form, 0, "&DelaySeconds=%d", delay_seconds);
    if (group_id && *group_id) {
        char *eg = url_encode_component(group_id); ast_str_append(&form, 0, "&MessageGroupId=%s", eg); ast_free(eg);
    }
    if (dedup_id && *dedup_id) {
        char *ed = url_encode_component(dedup_id); ast_str_append(&form, 0, "&MessageDeduplicationId=%s", ed); ast_free(ed);
    }

    /* optional message attributes as comma-separated: key=value,key2=value2 (string type) */
    if (attrs_kv_pairs && *attrs_kv_pairs) {
        char *tmp = ast_strdup(attrs_kv_pairs);
        char *parse = tmp;
        int idx = 1; 
        char *tok;
        while ((tok = ast_strsep(&parse, ',', 0))) {
            char *eq = strchr(tok, '=');
            if (eq && eq != tok) {
                *eq = '\0'; 
                const char *k = tok; 
                const char *v = eq + 1;
                char *ek = url_encode_component(k);
                char *ev = url_encode_component(v);
                ast_str_append(&form, 0, "&MessageAttribute.%d.Name=%s&MessageAttribute.%d.Value.DataType=String&MessageAttribute.%d.Value.StringValue=%s", idx, ek, idx, idx, ev);
                ast_free(ek); 
                ast_free(ev); 
                idx++;
            }
        }
        ast_free(tmp);
        /* Also tell SQS how many attrs we sent */
        ast_str_append(&form, 0, "&MessageAttribute.%d.Name=__end__", 1000); /* harmless */
    }

    struct curl_slist *hdrs=NULL;
    if (sigv4_headers_for_sqs(queue_url, ast_str_buffer(form), &creds, &hdrs)) { 
        ast_free(form); 
        return -1; 
    }

    long code=0;
    RAII_VAR(struct ast_str *, resp, NULL, ast_free);
    int rc = http_post(queue_url, hdrs, ast_str_buffer(form), g_cfg.http_timeout_ms, &code, &resp);
    curl_slist_free_all(hdrs); ast_free(form);
    if (rc || code!=200 || !resp) {
        if (g_cfg.debug) ast_log(LOG_WARNING, "SQS send failed: http=%ld body=%.256s\n", code, resp ? ast_str_buffer(resp) : "");
        return -1;
    }

    /* Extract <MessageId> using XML parser */
    RAII_VAR(struct ast_xml_doc *, doc, ast_xml_read_memory(ast_str_buffer(resp), ast_str_strlen(resp)), ast_xml_close);
    if (doc) {
        struct ast_xml_node *root = ast_xml_get_root(doc);
        struct ast_xml_node *msg_id = ast_xml_find_element(ast_xml_node_get_children(root), "MessageId", NULL, NULL);
        if (msg_id) {
            const char *msg_id_text = ast_xml_get_text(msg_id);
            if (msg_id_text) {
                *out_message_id = ast_str_create(256);
                if (*out_message_id) {
                    ast_str_set(out_message_id, 0, "%s", msg_id_text);
                }
            }
        }
    }
    return 0;
}

/* ----------------------------- S3 SigV4 Signing ----------------------------- */

static int sigv4_headers_for_s3(const char *url, const char *method, const char *content_type, 
                                size_t content_length, const struct aws_creds *creds,
                                struct curl_slist **headers)
{
	/* Parse URL using Asterisk URI parser */
	RAII_VAR(struct ast_uri *, parsed_uri, ast_uri_parse_http(url), ao2_cleanup);
	const char *host, *path;

	if (!parsed_uri) {
		return -1;
	}

	host = ast_uri_host(parsed_uri);
	path = ast_uri_path(parsed_uri);
	if (!host || !path) {
		return -1;
	}

	RAII_VAR(struct ast_str *, host_str, ast_str_create(256), ast_free);
	if (!host_str) {
		return -1;
	}
	ast_str_set(&host_str, 0, "%s", host);

	RAII_VAR(struct ast_str *, date_iso_str, ast_str_create(32), ast_free);
	RAII_VAR(struct ast_str *, date_short_str, ast_str_create(16), ast_free);
	if (!date_iso_str || !date_short_str) {
		return -1;
	}

	time_t now = time(NULL);
	struct tm g;
	gmtime_r(&now, &g);
	RAII_VAR(struct ast_str *, temp_date_str, ast_str_create(32), ast_free);
	RAII_VAR(struct ast_str *, temp_short_str, ast_str_create(16), ast_free);
	if (!temp_date_str || !temp_short_str) {
		return -1;
	}
	
	char temp_buf[32];
	strftime(temp_buf, sizeof(temp_buf), "%Y%m%dT%H%M%SZ", &g);
	ast_str_set(&temp_date_str, 0, "%s", temp_buf);
	strftime(temp_buf, 16, "%Y%m%d", &g);
	ast_str_set(&temp_short_str, 0, "%s", temp_buf);
	ast_str_set(&date_iso_str, 0, "%s", ast_str_buffer(temp_date_str));
	ast_str_set(&date_short_str, 0, "%s", ast_str_buffer(temp_short_str));

	const char *service = "s3";
	const char *region = ast_str_strlen(g_cfg.region) > 0 ? ast_str_buffer(g_cfg.region) : "us-east-1";

	/* For S3, we use UNSIGNED-PAYLOAD for simplicity */
	const char *payload_hash = "UNSIGNED-PAYLOAD";

	/* Canonical request using RAII */
	RAII_VAR(struct ast_str *, canon, ast_str_create(1024), ast_free);
	if (!canon) {
		return -1;
	}

	ast_str_set(&canon, 0, "%s\n%s\n\n", method, path);
	ast_str_append(&canon, 0, "content-type:%s\n", content_type ? content_type : "application/octet-stream");
	ast_str_append(&canon, 0, "host:%s\n", ast_str_buffer(host_str));
	ast_str_append(&canon, 0, "x-amz-content-sha256:%s\n", payload_hash);
	ast_str_append(&canon, 0, "x-amz-date:%s\n", ast_str_buffer(date_iso_str));
	if (ast_str_strlen(creds->session_token) > 0) {
		ast_str_append(&canon, 0, "x-amz-security-token:%s\n", ast_str_buffer(creds->session_token));
	}
	ast_str_append(&canon, 0, "\n");
	ast_str_append(&canon, 0, "content-type;host;x-amz-content-sha256;x-amz-date");
	if (ast_str_strlen(creds->session_token) > 0) {
		ast_str_append(&canon, 0, ";x-amz-security-token");
	}
	ast_str_append(&canon, 0, "\n%s", payload_hash);

	RAII_VAR(struct ast_str *, canon_hash_str, ast_str_create(65), ast_free);
	if (!canon_hash_str) {
		return -1;
	}
	if (sha256_hex_to_str((unsigned char*)ast_str_buffer(canon), ast_str_strlen(canon), &canon_hash_str) != 0) {
		return -1;
	}

	/* String to sign using RAII */
	RAII_VAR(struct ast_str *, scope_str, ast_str_create(128), ast_free);
	RAII_VAR(struct ast_str *, string_to_sign, ast_str_create(512), ast_free);
	if (!scope_str || !string_to_sign) {
		return -1;
	}

	ast_str_set(&scope_str, 0, "%s/%s/%s/aws4_request", ast_str_buffer(date_short_str), region, service);
	ast_str_set(&string_to_sign, 0, "AWS4-HMAC-SHA256\n%s\n%s\n%s", 
	           ast_str_buffer(date_iso_str), ast_str_buffer(scope_str), ast_str_buffer(canon_hash_str));

	/* Generate signing key */
	unsigned char kSigning[SHA256_DIGEST_LENGTH];
	unsigned int klen = 0;
	if (aws_signing_key(ast_str_buffer(creds->secret_key), ast_str_buffer(date_short_str), region, service, kSigning, &klen) != 0) {
		return -1;
	}

	/* Calculate signature */
	unsigned char sig_bin[SHA256_DIGEST_LENGTH];
	unsigned int sig_len = 0;
	hmac_sha256(kSigning, klen, (unsigned char*)ast_str_buffer(string_to_sign), 
	           ast_str_strlen(string_to_sign), sig_bin, &sig_len);
	
	RAII_VAR(struct ast_str *, sig_hex_str, ast_str_create(65), ast_free);
	if (!sig_hex_str) {
		return -1;
	}
	for (int i = 0; i < 32; i++) {
		ast_str_append(&sig_hex_str, 0, "%02x", sig_bin[i]);
	}

	/* Create authorization header using RAII */
	RAII_VAR(struct ast_str *, auth, ast_str_create(1024), ast_free);
	if (!auth) {
		return -1;
	}

	ast_str_set(&auth, 0, "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=", 
	           ast_str_buffer(creds->access_key), ast_str_buffer(scope_str));
	ast_str_append(&auth, 0, "content-type;host;x-amz-content-sha256;x-amz-date");
	if (ast_str_strlen(creds->session_token) > 0) {
		ast_str_append(&auth, 0, ";x-amz-security-token");
	}
	ast_str_append(&auth, 0, ", Signature=%s", ast_str_buffer(sig_hex_str));

	/* Add headers using safe string construction */
	RAII_VAR(struct ast_str *, hdr_str, ast_str_create(2048), ast_free);
	if (!hdr_str) {
		return -1;
	}
	
	ast_str_set(&hdr_str, 0, "Authorization: %s", ast_str_buffer(auth));
	*headers = curl_slist_append(*headers, ast_str_buffer(hdr_str));

	ast_str_set(&hdr_str, 0, "x-amz-date: %s", ast_str_buffer(date_iso_str));
	*headers = curl_slist_append(*headers, ast_str_buffer(hdr_str));

	ast_str_set(&hdr_str, 0, "x-amz-content-sha256: %s", payload_hash);
	*headers = curl_slist_append(*headers, ast_str_buffer(hdr_str));

	if (ast_str_strlen(creds->session_token) > 0) {
		ast_str_set(&hdr_str, 0, "x-amz-security-token: %s", ast_str_buffer(creds->session_token));
		*headers = curl_slist_append(*headers, ast_str_buffer(hdr_str));
	}
	
	return 0;
}

/* ----------------------------- S3 Operations ----------------------------- */

struct s3_upload_data {
	FILE *file;
	size_t remaining;
};

static size_t s3_read_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
	struct s3_upload_data *upload_data = stream;
	size_t max_bytes = size * nmemb;
	size_t bytes_to_read = (upload_data->remaining < max_bytes) ? upload_data->remaining : max_bytes;
	size_t bytes_read;

	if (bytes_to_read == 0) {
		return 0;
	}

	bytes_read = fread(ptr, 1, bytes_to_read, upload_data->file);
	upload_data->remaining -= bytes_read;
	return bytes_read;
}

static int s3_put_object(const char *bucket, const char *key, const char *filepath,
			 const char *content_type, const char *metadata, const char *tags,
			 struct ast_str **out_etag)
{
	if (ensure_fresh_creds()) return -1;

	struct aws_creds creds;
	ast_mutex_lock(&g_creds_lock); 
	creds = g_creds; 
	ast_mutex_unlock(&g_creds_lock);

	CURL *curl = curl_easy_init();
	if (!curl) {
		ast_log(LOG_ERROR, "Failed to initialize CURL for S3 upload\n");
		return -1;
	}

	FILE *file = fopen(filepath, "rb");
	if (!file) {
		ast_log(LOG_ERROR, "Cannot open file for S3 upload: %s\n", filepath);
		curl_easy_cleanup(curl);
		return -1;
	}

	/* Get file size */
	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	struct s3_upload_data upload_data = {
		.file = file,
		.remaining = file_size
	};

	/* Build URL */
	RAII_VAR(struct ast_str *, url_str, ast_str_create(1024), ast_free);
	if (!url_str) {
		curl_easy_cleanup(curl);
		fclose(file);
		return -1;
	}
	const char *region_str = ast_str_strlen(g_cfg.region) > 0 ? ast_str_buffer(g_cfg.region) : "us-east-1";
	ast_str_set(&url_str, 0, "https://%s.s3.%s.amazonaws.com/%s", bucket, region_str, key);

	/* Build headers */
	struct curl_slist *headers = NULL;
	RAII_VAR(struct ast_str *, date_str, ast_str_create(64), ast_free);
	RAII_VAR(struct ast_str *, short_date, ast_str_create(16), ast_free);
	if (!date_str || !short_date) {
		curl_easy_cleanup(curl);
		fclose(file);
		return -1;
	}
	
	time_t now = time(NULL);
	struct tm g;
	gmtime_r(&now, &g);
	char temp_buf[32];
	strftime(temp_buf, sizeof(temp_buf), "%Y%m%dT%H%M%SZ", &g);
	ast_str_set(&date_str, 0, "%s", temp_buf);
	strftime(temp_buf, 16, "%Y%m%d", &g);
	ast_str_set(&short_date, 0, "%s", temp_buf);

	RAII_VAR(struct ast_str *, content_type_hdr, ast_str_create(256), ast_free);
	if (!content_type_hdr) {
		curl_easy_cleanup(curl);
		fclose(file);
		return -1;
	}
	if (content_type && *content_type) {
		ast_str_set(&content_type_hdr, 0, "Content-Type: %s", content_type);
	} else {
		ast_str_set(&content_type_hdr, 0, "Content-Type: application/octet-stream");
	}
	headers = curl_slist_append(headers, ast_str_buffer(content_type_hdr));

	if (metadata && *metadata) {
		/* Parse metadata and add as x-amz-meta-* headers */
		RAII_VAR(char *, meta_copy, ast_strdup(metadata), ast_free);
		RAII_VAR(struct ast_str *, meta_hdr, ast_str_create(512), ast_free);
		if (!meta_copy || !meta_hdr) {
			curl_easy_cleanup(curl);
			fclose(file);
			return -1;
		}
		char *saveptr, *pair;
		for (pair = strtok_r(meta_copy, ",", &saveptr); pair; pair = strtok_r(NULL, ",", &saveptr)) {
			char *eq = strchr(pair, '=');
			if (eq) {
				*eq = '\0';
				ast_str_set(&meta_hdr, 0, "x-amz-meta-%s: %s", pair, eq + 1);
				headers = curl_slist_append(headers, ast_str_buffer(meta_hdr));
			}
		}
	}

	if (tags && *tags) {
		RAII_VAR(struct ast_str *, tag_hdr, ast_str_create(1024), ast_free);
		if (!tag_hdr) {
			curl_easy_cleanup(curl);
			fclose(file);
			return -1;
		}
		ast_str_set(&tag_hdr, 0, "x-amz-tagging: %s", tags);
		headers = curl_slist_append(headers, ast_str_buffer(tag_hdr));
	}

	/* Configure CURL */
	curl_easy_setopt(curl, CURLOPT_URL, ast_str_buffer(url_str));
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, s3_read_callback);
	curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
	curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_size);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, g_cfg.http_timeout_ms / 1000);

	/* Sign request for S3 PUT using SigV4 */
	if (sigv4_headers_for_s3(ast_str_buffer(url_str), "PUT", ast_str_buffer(content_type_hdr) + 14, file_size, &creds, &headers)) {
		curl_easy_cleanup(curl);
		curl_slist_free_all(headers);
		fclose(file);
		return -1;
	}

	RAII_VAR(struct ast_str *, response, NULL, ast_free);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb_ast_str);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	/* Perform upload */
	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	int ret = 0;
	if (res != CURLE_OK || (http_code != 200 && http_code != 201)) {
		ast_log(LOG_ERROR, "S3 upload failed: HTTP %ld, CURL: %s\n", 
			http_code, curl_easy_strerror(res));
		if (response) {
			ast_log(LOG_ERROR, "S3 response: %s\n", ast_str_buffer(response));
		}
		ret = -1;
	} else {
		/* Extract ETag from response if available */
		if (response && out_etag) {
			const char *response_text = ast_str_buffer(response);
			const char *etag_start = strstr(response_text, "<ETag>");
			if (etag_start) {
				etag_start += 6; /* Skip <ETag> */
				const char *etag_end = strstr(etag_start, "</ETag>");
				if (etag_end) {
					*out_etag = ast_str_create(256);
					if (*out_etag) {
						ast_str_set_substr(out_etag, 0, etag_start, etag_end - etag_start);
					}
				}
			}
		}
	}

	/* Cleanup */
	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);
	fclose(file);

	return ret;
}

static int s3_get_object(const char *bucket, const char *key, const char *filepath)
{
	if (ensure_fresh_creds()) return -1;

	struct aws_creds creds;
	ast_mutex_lock(&g_creds_lock); 
	creds = g_creds; 
	ast_mutex_unlock(&g_creds_lock);

	CURL *curl = curl_easy_init();
	if (!curl) return -1;

	FILE *file = fopen(filepath, "wb");
	if (!file) {
		curl_easy_cleanup(curl);
		return -1;
	}

	RAII_VAR(struct ast_str *, url_str, ast_str_create(1024), ast_free);
	if (!url_str) {
		curl_easy_cleanup(curl);
		fclose(file);
		return -1;
	}
	const char *region_str = ast_str_strlen(g_cfg.region) > 0 ? ast_str_buffer(g_cfg.region) : "us-east-1";
	ast_str_set(&url_str, 0, "https://%s.s3.%s.amazonaws.com/%s", bucket, region_str, key);

	struct curl_slist *headers = NULL;
	
	if (sigv4_headers_for_s3(ast_str_buffer(url_str), "GET", "application/octet-stream", 0, &creds, &headers)) {
		curl_easy_cleanup(curl);
		fclose(file);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, ast_str_buffer(url_str));
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, g_cfg.http_timeout_ms / 1000);

	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	int ret = (res == CURLE_OK && http_code == 200) ? 0 : -1;

	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);
	fclose(file);

	return ret;
}

static int s3_delete_object(const char *bucket, const char *key)
{
	if (ensure_fresh_creds()) return -1;

	struct aws_creds creds;
	ast_mutex_lock(&g_creds_lock); 
	creds = g_creds; 
	ast_mutex_unlock(&g_creds_lock);

	CURL *curl = curl_easy_init();
	if (!curl) return -1;

	RAII_VAR(struct ast_str *, url_str, ast_str_create(1024), ast_free);
	if (!url_str) {
		curl_easy_cleanup(curl);
		return -1;
	}
	const char *region_str = ast_str_strlen(g_cfg.region) > 0 ? ast_str_buffer(g_cfg.region) : "us-east-1";
	ast_str_set(&url_str, 0, "https://%s.s3.%s.amazonaws.com/%s", bucket, region_str, key);

	struct curl_slist *headers = NULL;
	
	if (sigv4_headers_for_s3(ast_str_buffer(url_str), "DELETE", "application/octet-stream", 0, &creds, &headers)) {
		curl_easy_cleanup(curl);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, ast_str_buffer(url_str));
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, g_cfg.http_timeout_ms / 1000);

	RAII_VAR(struct ast_str *, response, NULL, ast_free);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb_ast_str);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	int ret = (res == CURLE_OK && (http_code == 204 || http_code == 200)) ? 0 : -1;

	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);

	return ret;
}

/* ----------------------------- S3 Dialplan Applications ----------------------------- */

static const char *s3upload_app = "S3Upload";
static const char *s3download_app = "S3Download";
static const char *s3delete_app = "S3Delete";

static int s3upload_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	RAII_VAR(struct ast_str *, etag, NULL, ast_free);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(bucket);
		AST_APP_ARG(key);
		AST_APP_ARG(filepath);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "S3Upload requires arguments: bucket,key,filepath[,options]\n");
		pbx_builtin_setvar_helper(chan, "S3UPLOAD_STATUS", "FAILED");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.bucket) || ast_strlen_zero(args.key) || ast_strlen_zero(args.filepath)) {
		ast_log(LOG_WARNING, "S3Upload requires bucket, key, and filepath\n");
		pbx_builtin_setvar_helper(chan, "S3UPLOAD_STATUS", "FAILED");
		return -1;
	}

	const char *content_type = NULL;
	const char *metadata = NULL;
	const char *tags = NULL;

	/* Parse options */
	if (!ast_strlen_zero(args.options)) {
		char *opts = ast_strdupa(args.options);
		char *opt;
		while ((opt = strsep(&opts, ","))) {
			char *val = strchr(opt, '=');
			if (val) {
				*val++ = '\0';
				if (!strcasecmp(opt, "content_type")) {
					content_type = val;
				} else if (!strcasecmp(opt, "metadata")) {
					metadata = val;
				} else if (!strcasecmp(opt, "tags")) {
					tags = val;
				}
			}
		}
	}

	if (s3_put_object(args.bucket, args.key, args.filepath, content_type, metadata, tags, &etag) == 0) {
		pbx_builtin_setvar_helper(chan, "S3UPLOAD_STATUS", "SUCCESS");
		if (etag) {
			pbx_builtin_setvar_helper(chan, "S3UPLOAD_ETAG", ast_str_buffer(etag));
		}
		ast_log(LOG_NOTICE, "S3 upload successful: %s to s3://%s/%s\n", args.filepath, args.bucket, args.key);
	} else {
		pbx_builtin_setvar_helper(chan, "S3UPLOAD_STATUS", "FAILED");
		ast_log(LOG_ERROR, "S3 upload failed: %s to s3://%s/%s\n", args.filepath, args.bucket, args.key);
		return -1;
	}

	return 0;
}

static int s3download_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(bucket);
		AST_APP_ARG(key);
		AST_APP_ARG(filepath);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "S3Download requires arguments: bucket,key,filepath\n");
		pbx_builtin_setvar_helper(chan, "S3DOWNLOAD_STATUS", "FAILED");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.bucket) || ast_strlen_zero(args.key) || ast_strlen_zero(args.filepath)) {
		ast_log(LOG_WARNING, "S3Download requires bucket, key, and filepath\n");
		pbx_builtin_setvar_helper(chan, "S3DOWNLOAD_STATUS", "FAILED");
		return -1;
	}

	if (s3_get_object(args.bucket, args.key, args.filepath) == 0) {
		pbx_builtin_setvar_helper(chan, "S3DOWNLOAD_STATUS", "SUCCESS");
		ast_log(LOG_NOTICE, "S3 download successful: s3://%s/%s to %s\n", args.bucket, args.key, args.filepath);
	} else {
		pbx_builtin_setvar_helper(chan, "S3DOWNLOAD_STATUS", "FAILED");
		ast_log(LOG_ERROR, "S3 download failed: s3://%s/%s to %s\n", args.bucket, args.key, args.filepath);
		return -1;
	}

	return 0;
}

static int s3delete_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(bucket);
		AST_APP_ARG(key);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "S3Delete requires arguments: bucket,key\n");
		pbx_builtin_setvar_helper(chan, "S3DELETE_STATUS", "FAILED");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.bucket) || ast_strlen_zero(args.key)) {
		ast_log(LOG_WARNING, "S3Delete requires bucket and key\n");
		pbx_builtin_setvar_helper(chan, "S3DELETE_STATUS", "FAILED");
		return -1;
	}

	if (s3_delete_object(args.bucket, args.key) == 0) {
		pbx_builtin_setvar_helper(chan, "S3DELETE_STATUS", "SUCCESS");
		ast_log(LOG_NOTICE, "S3 delete successful: s3://%s/%s\n", args.bucket, args.key);
	} else {
		pbx_builtin_setvar_helper(chan, "S3DELETE_STATUS", "FAILED");
		ast_log(LOG_ERROR, "S3 delete failed: s3://%s/%s\n", args.bucket, args.key);
		return -1;
	}

	return 0;
}

/* ----------------------------- Original SQS Dialplan app ----------------------------- */

static const char *app = APPNAME;

/*
 * AwsSqsSend(queue, body[, options])
 *
 * queue: either a full QueueUrl (recommended) or a name alias from res_aws.conf [queues]
 * body:  message body (string)
 * options: semicolon- or comma-separated k=v pairs
 *   delay=SECONDS
 *   group=GROUP_ID            (FIFO queues)
 *   dedup=DEDUP_ID            (FIFO queues)
 *   attrs=key1=val1,key2=val2 (message attributes, string type)
 */

static int app_exec(struct ast_channel *chan, const char *data) {
    char *parse = ast_strdupa(data ? data : "");
    AST_DECLARE_APP_ARGS(args,
        AST_APP_ARG(queue);
        AST_APP_ARG(body);
        AST_APP_ARG(opts);
    );
    AST_STANDARD_APP_ARGS(args, parse);

    if (ast_strlen_zero(args.queue) || ast_strlen_zero(args.body)) {
        ast_log(LOG_ERROR, "%s requires queue and body\n", APPNAME);
        return -1;
    }

    /* resolve queue name → url if needed using RAII */
    RAII_VAR(struct ast_str *, queue_url_str, ast_str_create(256), ast_free);
    if (!queue_url_str) {
        return -1;
    }

    if (!strncasecmp(args.queue, "https://", 8)) {
        ast_str_set(&queue_url_str, 0, "%s", args.queue);
    } else {
        /* lookup in config section [queues] */
        struct ast_flags config_flags = { 0 };
        RAII_VAR(struct ast_config *, cfg, ast_config_load("res_aws.conf", config_flags), ast_config_destroy);
        if (cfg) {
            const char *v = ast_variable_retrieve(cfg, "queues", args.queue);
            if (v) {
                ast_str_set(&queue_url_str, 0, "%s", v);
            }
        }
        if (ast_str_strlen(queue_url_str) == 0 && ast_str_strlen(g_cfg.default_queue_url) > 0) {
            ast_str_set(&queue_url_str, 0, "%s", ast_str_buffer(g_cfg.default_queue_url));
        }
        if (ast_str_strlen(queue_url_str) == 0) {
            ast_log(LOG_ERROR, "Queue '%s' not found and no default_queue_url set\n", args.queue);
            return -1;
        }
    }

    int delay=0; const char *group=NULL; const char *dedup=NULL; const char *attrs=NULL;
    if (!ast_strlen_zero(args.opts)) {
        char *opts = ast_strdupa(args.opts);
        char *saveptr;
        for (char *tok = strtok_r(opts, ";,", &saveptr); tok; tok = strtok_r(NULL, ";,", &saveptr)) {
            char *eq = strchr(tok, '='); 
            char *k, *v;
            if (!eq) continue; 
            *eq = '\0'; 
            k = tok; 
            v = eq + 1;
            if (!strcasecmp(k, "delay")) {
                if (ast_str_to_int(v, &delay)) {
                    delay = 0; /* fallback on error */
                }
            } else if (!strcasecmp(k, "group")) {
                group = v;
            } else if (!strcasecmp(k, "dedup")) {
                dedup = v;
            } else if (!strcasecmp(k, "attrs")) {
                attrs = v;
            }
        }
    }

    RAII_VAR(struct ast_str *, msgid_str, ast_str_create(128), ast_free);
    if (!msgid_str) {
        return -1;
    }

    RAII_VAR(struct ast_str *, msgid_result, NULL, ast_free);
    int rc = sqs_send_message(ast_str_buffer(queue_url_str), args.body, group, dedup, delay, attrs, &msgid_result);
    pbx_builtin_setvar_helper(chan, "AWS_SQS_STATUS", rc==0?"OK":"ERROR");
    if (rc==0 && msgid_result) pbx_builtin_setvar_helper(chan, "AWS_SQS_MESSAGE_ID", ast_str_buffer(msgid_result));
    return rc ? -1 : 0;
}

/* ----------------------------- CLI ----------------------------- */

static char *cli_show_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
    switch (cmd) { case CLI_INIT: e->command = "aws show status"; e->usage = "Show AWS credential/source status"; return NULL; case CLI_GENERATE: return NULL; }
    ast_mutex_lock(&g_creds_lock);
    ast_cli(a->fd, "Source: %d (0 none,1 env,2 static,3 ecs,4 imds,5 sts)\n", g_creds.source);
    ast_cli(a->fd, "AccessKey: %s\n", g_creds.access_key ? ast_str_buffer(g_creds.access_key) : "");
    ast_cli(a->fd, "Session? %s\n", (g_creds.session_token && ast_str_strlen(g_creds.session_token) > 0) ? "yes" : "no");
    if (g_creds.expiration) ast_cli(a->fd, "Expires: %ld\n", (long)g_creds.expiration);
    ast_mutex_unlock(&g_creds_lock);
    ast_cli(a->fd, "Region: %s\n", g_cfg.region ? ast_str_buffer(g_cfg.region) : "");
    return CLI_SUCCESS;
}

static char *cli_refresh(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
    switch (cmd) { case CLI_INIT: e->command = "aws refresh"; e->usage = "Force refresh of AWS credentials"; return NULL; case CLI_GENERATE: return NULL; }
    ast_mutex_lock(&g_creds_lock); int rc = refresh_creds_locked(); ast_mutex_unlock(&g_creds_lock);
    ast_cli(a->fd, "Refresh %s\n", rc?"FAILED":"OK");
    return CLI_SUCCESS;
}


static struct ast_cli_entry cli_cmds[] = {
    AST_CLI_DEFINE(cli_show_status, "Show AWS credential status"),
    AST_CLI_DEFINE(cli_refresh, "Refresh AWS credentials"),
};

/* ----------------------------- Config load ----------------------------- */

static void cleanup_config(void) {
    if (g_cfg.region) ast_free(g_cfg.region);
    if (g_cfg.default_queue_url) ast_free(g_cfg.default_queue_url);
    if (g_cfg.sts_role_arn) ast_free(g_cfg.sts_role_arn);
    if (g_cfg.sts_external_id) ast_free(g_cfg.sts_external_id);
    if (g_cfg.sts_session_name) ast_free(g_cfg.sts_session_name);
    if (g_cfg.access_key) ast_free(g_cfg.access_key);
    if (g_cfg.secret_key) ast_free(g_cfg.secret_key);
    if (g_cfg.session_token) ast_free(g_cfg.session_token);
    memset(&g_cfg, 0, sizeof(g_cfg));
}

static int load_config(void) {
    cleanup_config();
    
    /* Initialize all ast_str fields */
    g_cfg.region = ast_str_create(32);
    g_cfg.default_queue_url = ast_str_create(256);
    g_cfg.sts_role_arn = ast_str_create(256);
    g_cfg.sts_external_id = ast_str_create(128);
    g_cfg.sts_session_name = ast_str_create(64);
    g_cfg.access_key = ast_str_create(128);
    g_cfg.secret_key = ast_str_create(128);
    g_cfg.session_token = ast_str_create(2048);
    
    if (!g_cfg.region || !g_cfg.default_queue_url || !g_cfg.sts_role_arn || !g_cfg.sts_external_id || 
        !g_cfg.sts_session_name || !g_cfg.access_key || !g_cfg.secret_key || !g_cfg.session_token) {
        cleanup_config();
        return -1;
    }
    
    /* Set defaults */
    ast_str_set(&g_cfg.region, 0, "us-east-1");
    g_cfg.refresh_skew = 120;
    g_cfg.http_timeout_ms = 4000;
    g_cfg.debug = 0;

    struct ast_flags config_flags = { 0 };
    struct ast_config *cfg = ast_config_load("res_aws.conf", config_flags);
    if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
        return 0; /* Use defaults */
    }
    
    const char *v;
    v = ast_variable_retrieve(cfg, "general", "region"); 
    if (v) ast_str_set(&g_cfg.region, 0, "%s", v);
    
    v = ast_variable_retrieve(cfg, "general", "default_queue_url"); 
    if (v) ast_str_set(&g_cfg.default_queue_url, 0, "%s", v);
    
    v = ast_variable_retrieve(cfg, "general", "refresh_skew"); 
    if (v) g_cfg.refresh_skew = atoi(v);
    
    v = ast_variable_retrieve(cfg, "general", "http_timeout_ms"); 
    if (v) g_cfg.http_timeout_ms = atoi(v);
    
    v = ast_variable_retrieve(cfg, "general", "debug"); 
    if (v) g_cfg.debug = atoi(v);

    v = ast_variable_retrieve(cfg, "sts", "role_arn"); 
    if (v) ast_str_set(&g_cfg.sts_role_arn, 0, "%s", v);
    
    v = ast_variable_retrieve(cfg, "sts", "external_id"); 
    if (v) ast_str_set(&g_cfg.sts_external_id, 0, "%s", v);
    
    v = ast_variable_retrieve(cfg, "sts", "session_name"); 
    if (v) ast_str_set(&g_cfg.sts_session_name, 0, "%s", v);

    v = ast_variable_retrieve(cfg, "static", "access_key_id"); 
    if (v) ast_str_set(&g_cfg.access_key, 0, "%s", v);
    
    v = ast_variable_retrieve(cfg, "static", "secret_access_key"); 
    if (v) ast_str_set(&g_cfg.secret_key, 0, "%s", v);
    
    v = ast_variable_retrieve(cfg, "static", "session_token"); 
    if (v) ast_str_set(&g_cfg.session_token, 0, "%s", v);

    ast_config_destroy(cfg);
    return 0;
}

/* ----------------------------- Module API ----------------------------- */

static int unload_module(void) {
    ast_cli_unregister_multiple(cli_cmds, ARRAY_LEN(cli_cmds));
    ast_unregister_application(app);
    ast_unregister_application(s3upload_app);
    ast_unregister_application(s3download_app);
    ast_unregister_application(s3delete_app);
    return 0;
}

static int load_module(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    load_config();

    ast_mutex_lock(&g_creds_lock);
    refresh_creds_locked();
    ast_mutex_unlock(&g_creds_lock);

    if (ast_register_application(app, app_exec, "Send a message to AWS SQS", "AwsSqsSend(queue, body[, options])")) {
        ast_log(LOG_ERROR, "Failed to register %s application\n", APPNAME);
        return AST_MODULE_LOAD_DECLINE;
    }
    
    if (ast_register_application(s3upload_app, s3upload_exec, "Upload file to AWS S3", "S3Upload(bucket,key,filepath[,options])")) {
        ast_log(LOG_ERROR, "Failed to register %s application\n", s3upload_app);
        return AST_MODULE_LOAD_DECLINE;
    }
    
    if (ast_register_application(s3download_app, s3download_exec, "Download file from AWS S3", "S3Download(bucket,key,filepath)")) {
        ast_log(LOG_ERROR, "Failed to register %s application\n", s3download_app);
        return AST_MODULE_LOAD_DECLINE;
    }
    
    if (ast_register_application(s3delete_app, s3delete_exec, "Delete object from AWS S3", "S3Delete(bucket,key)")) {
        ast_log(LOG_ERROR, "Failed to register %s application\n", s3delete_app);
        return AST_MODULE_LOAD_DECLINE;
    }
    ast_cli_register_multiple(cli_cmds, ARRAY_LEN(cli_cmds));
    ast_log(LOG_NOTICE, "%s loaded (region=%s)\n", MODNAME, g_cfg.region ? ast_str_buffer(g_cfg.region) : "");
    return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void) {
    load_config();
    ast_mutex_lock(&g_creds_lock); refresh_creds_locked(); ast_mutex_unlock(&g_creds_lock);
    ast_log(LOG_NOTICE, "%s reloaded\n", MODNAME);
    return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "AWS integration resource module",
    .load = load_module,
    .unload = unload_module,
    .reload = reload,
    .support_level = AST_MODULE_SUPPORT_EXTENDED
);
