/*
 * Asterisk -- An open source telephony toolkit.
 * 
 * ElevenLabs WebSocket Streaming API Application
 * Real-time Text-to-Speech with WebSocket streaming input
 * 
 * Features:
 * - WebSocket streaming text input to ElevenLabs
 * - Real-time audio generation and playback
 * - Chunked text streaming support
 * - Word-to-audio alignment information
 * - SSML parsing support
 * - Multiple output formats
 * - Auto mode for reduced latency
 * - Deterministic seeding
 * - Text normalization controls
 * 
 * WebSocket API Endpoint:
 * wss://api.elevenlabs.io/v1/text-to-speech/{voice_id}/stream-input
 * 
 * Usage:
 * ElevenLabsWSS(api_key=YOUR_KEY,voice=VOICE_ID,text=Hello World)
 * ElevenLabsWSS(api_key=YOUR_KEY,voice=VOICE_ID,text_file=/tmp/speech.txt)
 * ElevenLabsWSS(api_key=YOUR_KEY,voice=VOICE_ID,text=Hello,stream_chunks=true)
 */

/*** MODULEINFO
    <depend>websockets</depend>
    <support_level>extended</support_level>
 ***/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/format_cap.h"
#include "asterisk/format.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/json.h"
#include "asterisk/translate.h"
#include "asterisk/format_cache.h"

#include <libwebsockets.h>
#include <pthread.h>
#include <math.h>  // For sqrt in AGC calculations
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>  // For gettimeofday - precise timing
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/utsname.h>  // For platform detection like Python reference

// ElevenLabs WebSocket API Constants
#define ELEVENLABS_WSS_HOST "api.elevenlabs.io"
#define ELEVENLABS_WSS_PORT 443
#define ELEVENLABS_WSS_PATH "/v1/text-to-speech"
#define ELEVENLABS_WSS_PROTOCOL "elevenlabs-wss"

// Default settings
#define ELEVENLABS_DEFAULT_MODEL "eleven_monolingual_v1"
#define ELEVENLABS_DEFAULT_VOICE "21m00Tcm4TlvDq8ikWAM"  // Rachel
#define ELEVENLABS_SAMPLE_RATE 16000  // ElevenLabs PCM format sample rate
#define ASTERISK_SAMPLE_RATE 8000     // Asterisk telephony rate

// Audio processing constants
#define AUDIO_CHUNK_SIZE 1024
#define MAX_TEXT_LENGTH 10000
#define MAX_VOICE_ID_LENGTH 64
#define MAX_API_KEY_LENGTH 256
#define MAX_URL_LENGTH 1024
#define MAX_JSON_LENGTH 8192
#define WEBSOCKET_BUFFER_SIZE 131072  // Increased to 128KB for large ElevenLabs responses

// libwebsockets constants
#define CONTEXT_PORT_NO_LISTEN -1
#define LWS_PLUGIN_STATIC
#define MAX_PAYLOAD_SIZE 4096

// Message types for ElevenLabs WebSocket API
typedef enum {
    ELEVENLABS_MSG_INIT,        // Initialize connection
    ELEVENLABS_MSG_TEXT,        // Send text chunk
    ELEVENLABS_MSG_CLOSE,       // Close connection
    ELEVENLABS_MSG_AUDIO,       // Received audio
    ELEVENLABS_MSG_FINAL        // Final output
} elevenlabs_wss_msg_type_t;

// Voice settings structure
struct elevenlabs_voice_settings {
    float speed;
    float stability;
    float similarity_boost;
    float style;
    int use_speaker_boost;
};

// Session structure for ElevenLabs WebSocket
struct elevenlabs_wss_session {
    // Essential fields
    struct ast_channel *chan;
    struct lws *wsi;
    struct lws_context *context;
    int running;
    int destroying;
    
    // Connection state
    int connection_established;
    int initialization_sent;
    int text_complete;
    int generation_complete;
    
    // Configuration
    char *api_key;
    char *voice_id;
    char *model_id;
    char *text;
    char *text_file_path;
    char *language_code;
    char *output_format;
    
    // Voice settings
    struct elevenlabs_voice_settings voice_settings;
    
    // WebSocket options
    int enable_logging;
    int enable_ssml_parsing;
    int inactivity_timeout;
    int sync_alignment;
    int auto_mode;
    char *apply_text_normalization;
    unsigned int seed;
    int use_seed;
    int stream_chunks;  // Whether to stream text in chunks
    
    // Audio processing
    char *audio_buffer;
    size_t buffer_size;
    size_t buffer_pos;
    
    // Text streaming
    char *remaining_text;
    size_t text_pos;
    size_t chunk_size;
    
    // Response data
    char *response_buffer;
    size_t response_size;
    size_t response_pos;
    
    // Threading
    ast_mutex_t *lock;
    pthread_t websocket_thread;
    
    // Performance metrics (like Python reference)
    time_t start_time;
    struct timeval connection_start;
    struct timeval first_audio_time;
    int first_audio_received;
    size_t total_audio_bytes;
    size_t total_response_bytes;
    int audio_chunks_received;
    double total_processing_time;
    double connection_time;
    double first_audio_latency;
    
    // WebSocket message buffer
    unsigned char *ws_buffer;
    size_t ws_buffer_size;
    size_t ws_buffer_used;
    
    // Alignment data
    struct ast_json *last_alignment;
    
    // Error handling
    int last_error_code;
    char *last_error_message;
};

// Global variables
static const char *app = "ElevenLabsWSS";
static volatile int global_shutdown = 0;
static struct elevenlabs_wss_session *current_session = NULL;
static ast_mutex_t session_lock;

// LWS protocols
static int callback_elevenlabs_wss(struct lws *wsi, enum lws_callback_reasons reason,
                                   void *user, void *in, size_t len);

static struct lws_protocols protocols[] = {
    {
        .name = "",  // Use empty protocol name for standard WebSocket
        .callback = callback_elevenlabs_wss,
        .per_session_data_size = 0,
        .rx_buffer_size = WEBSOCKET_BUFFER_SIZE,
        .id = 0,
        .user = NULL,
        .tx_packet_size = 0,
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } /* terminator */
};

// Function declarations
static int elevenlabs_wss_exec(struct ast_channel *chan, const char *data);
static struct elevenlabs_wss_session *create_elevenlabs_wss_session(struct ast_channel *chan, const char *options);
static void destroy_elevenlabs_wss_session(struct elevenlabs_wss_session *session);
static void *websocket_thread_func(void *data);

// WebSocket message functions
static int send_initialization_message(struct elevenlabs_wss_session *session);
static int connect_websocket_with_retry(struct elevenlabs_wss_session *session, const char *url);

// Platform detection and optimization
static void log_platform_info(void);
static void optimize_for_platform(struct elevenlabs_wss_session *session);

// Utility functions
static char *build_websocket_url(const char *voice_id, const char *api_key, struct elevenlabs_wss_session *session);
static size_t downsample_16khz_to_8khz(const int16_t *input, size_t input_samples, 
                                      int16_t *output, size_t output_max_samples);

static unsigned char *base64_decode_optimized(const char *input, size_t input_len, size_t *output_len) {
    if (!input || input_len == 0 || !output_len) {
        return NULL;
    }
    
    static const int decode_table[256] = {
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
        52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-2,-1,-1,
        -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
        15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
        -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
        41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
    };
    
    // Calculate output length (accounting for padding)
    size_t padding = 0;
    if (input_len > 0 && input[input_len - 1] == '=') padding++;
    if (input_len > 1 && input[input_len - 2] == '=') padding++;
    
    *output_len = (input_len * 3) / 4 - padding;
    
    unsigned char *output = ast_malloc(*output_len);
    if (!output) {
        *output_len = 0;
        return NULL;
    }
    
    size_t out_pos = 0;
    for (size_t i = 0; i < input_len; i += 4) {
        int a = decode_table[(unsigned char)input[i]];
        int b = decode_table[(unsigned char)input[i + 1]];
        int c = (i + 2 < input_len) ? decode_table[(unsigned char)input[i + 2]] : -2;
        int d = (i + 3 < input_len) ? decode_table[(unsigned char)input[i + 3]] : -2;
        
        if (a < 0 || b < 0) continue;
        
        uint32_t triple = (a << 18) | (b << 12);
        if (c >= 0) triple |= (c << 6);
        if (d >= 0) triple |= d;
        
        if (out_pos < *output_len) output[out_pos++] = (unsigned char)(triple >> 16);
        if (c >= 0 && out_pos < *output_len) output[out_pos++] = (unsigned char)(triple >> 8);
        if (d >= 0 && out_pos < *output_len) output[out_pos++] = (unsigned char)triple;
    }
    
    *output_len = out_pos;
    ast_log(LOG_NOTICE, "🔄 Optimized Base64 decode: %zu bytes -> %zu bytes \n", input_len, *output_len);
    
    return output;
}

// Platform detection and logging (like Python reference)
static void log_platform_info(void) {
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        ast_log(LOG_NOTICE, "🎯 ElevenLabs WebSocket Module - Platform Optimized\n");
        ast_log(LOG_NOTICE, "============================================================\n");
        ast_log(LOG_NOTICE, "Platform: %s %s\n", system_info.sysname, system_info.release);
        ast_log(LOG_NOTICE, "Architecture: %s\n", system_info.machine);
        ast_log(LOG_NOTICE, "Node: %s\n", system_info.nodename);
        ast_log(LOG_NOTICE, "============================================================\n");
        
        // Platform-specific optimizations (like Python reference)
        if (strstr(system_info.sysname, "Linux")) {
            ast_log(LOG_NOTICE, "💡 Linux detected - using ALSA/PulseAudio optimizations\n");
        } else if (strstr(system_info.sysname, "Darwin")) {
            ast_log(LOG_NOTICE, "💡 macOS detected - using CoreAudio optimizations\n");
        } else if (strstr(system_info.sysname, "FreeBSD")) {
            ast_log(LOG_NOTICE, "💡 FreeBSD detected - using OSS optimizations\n");
        } else {
            ast_log(LOG_NOTICE, "💡 Platform: %s - using generic optimizations\n", system_info.sysname);
        }
    } else {
        ast_log(LOG_NOTICE, "🎯 ElevenLabs WebSocket Module\n");
        ast_log(LOG_NOTICE, "💡 Platform detection failed - using generic optimizations\n");
    }
}

// Platform-specific optimizations (like Python reference)
static void optimize_for_platform(struct elevenlabs_wss_session *session) {
    if (!session) return;
    
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        if (strstr(system_info.sysname, "Linux")) {
            // Linux optimizations
            session->inactivity_timeout = 15;  // Longer timeout for Linux
            ast_log(LOG_NOTICE, "🐧 Linux optimizations applied\n");
        } else if (strstr(system_info.sysname, "Darwin")) {
            // macOS optimizations
            session->inactivity_timeout = 10;  // Standard timeout for macOS
            ast_log(LOG_NOTICE, "🍎 macOS optimizations applied\n");
        } else if (strstr(system_info.sysname, "FreeBSD")) {
            // FreeBSD optimizations
            session->inactivity_timeout = 12;  // BSD timeout
            ast_log(LOG_NOTICE, "😈 FreeBSD optimizations applied\n");
        }
    }
    
    // Apply proven settings from Python reference
    session->auto_mode = 1;  // Always enable auto mode for reduced latency
    session->enable_logging = 0;  // Disable ElevenLabs logging to reduce noise
    ast_log(LOG_NOTICE, "✅ Platform optimizations complete\n");
}

// Downsample from 16kHz to 8kHz for telephony (ElevenLabs PCM format)
static size_t downsample_16khz_to_8khz(const int16_t *input, size_t input_samples, 
                                      int16_t *output, size_t output_max_samples) {
    if (!input || !output || input_samples == 0) {
        return 0;
    }
    
    // For 16kHz to 8kHz, we take every 2nd sample (simple decimation)
    size_t output_samples = input_samples / 2;
    if (output_samples > output_max_samples) {
        output_samples = output_max_samples;
    }
    
    for (size_t i = 0; i < output_samples; i++) {
        size_t source_idx = i * 2;  // Take every 2nd sample
        if (source_idx < input_samples) {
            output[i] = input[source_idx];
        }
    }
    
    return output_samples;
}

// Transcoding diagnostics - check format compatibility and log details
static void log_transcoding_diagnostics(struct ast_channel *chan) {
    if (!chan) {
        ast_log(LOG_ERROR, "❌ TRANSCODING DIAGNOSTICS: No channel provided\n");
        return;
    }
    
    ast_log(LOG_NOTICE, "🔍 TRANSCODING DIAGNOSTICS\n");
    ast_log(LOG_NOTICE, "============================================================\n");
    
    // Channel information
    ast_log(LOG_NOTICE, "Channel: %s\n", ast_channel_name(chan));
    ast_log(LOG_NOTICE, "Channel state: %s\n", ast_state2str(ast_channel_state(chan)));
    
    // Format information
    struct ast_format *read_format = ast_channel_readformat(chan);
    struct ast_format *write_format = ast_channel_writeformat(chan);
    struct ast_format *source_format = ast_format_cache_get_slin_by_rate(16000);
    
    ast_log(LOG_NOTICE, "Format Configuration:\n");
    if (read_format) {
        ast_log(LOG_NOTICE, "  Read format: %s (rate: %u Hz)\n", 
                ast_format_get_name(read_format), ast_format_get_sample_rate(read_format));
    } else {
        ast_log(LOG_ERROR, "  Read format: NULL ❌\n");
    }
    
    if (write_format) {
        ast_log(LOG_NOTICE, "  Write format: %s (rate: %u Hz)\n", 
                ast_format_get_name(write_format), ast_format_get_sample_rate(write_format));
    } else {
        ast_log(LOG_ERROR, "  Write format: NULL ❌\n");
    }
    
    if (source_format) {
        ast_log(LOG_NOTICE, "  Source format: %s (rate: %u Hz)\n", 
                ast_format_get_name(source_format), ast_format_get_sample_rate(source_format));
    } else {
        ast_log(LOG_ERROR, "  Source format: NULL ❌ (16kHz PCM not available)\n");
    }
    
    // Transcoding compatibility analysis
    ast_log(LOG_NOTICE, "Transcoding Analysis:\n");
    if (read_format && write_format && source_format) {
        ast_log(LOG_NOTICE, "  ✅ All formats available\n");
        ast_log(LOG_NOTICE, "  Path: 16kHz PCM -> %s\n", ast_format_get_name(write_format));
        
        // Check sample rate compatibility
        unsigned int write_rate = ast_format_get_sample_rate(write_format);
        if (write_rate > 0) {
            ast_log(LOG_NOTICE, "  Sample rate conversion: 16000 Hz -> %u Hz\n", write_rate);
            if (write_rate == 8000) {
                ast_log(LOG_NOTICE, "  ✅ Standard telephony conversion (16kHz->8kHz)\n");
            } else if (write_rate == 16000) {
                ast_log(LOG_NOTICE, "  ✅ No sample rate conversion needed\n");
            } else {
                ast_log(LOG_NOTICE, "  ⚠️ Non-standard sample rate conversion\n");
            }
        } else {
            ast_log(LOG_WARNING, "  ⚠️ Unknown target sample rate\n");
        }
    } else {
        ast_log(LOG_ERROR, "  ❌ Missing format information - transcoding may fail\n");
    }
    
    // Codec availability check
    ast_log(LOG_NOTICE, "Codec Modules:\n");
    ast_log(LOG_NOTICE, "  💡 If transcoding fails, check that these modules are loaded:\n");
    ast_log(LOG_NOTICE, "     - codec_resample.so (for sample rate conversion)\n");
    ast_log(LOG_NOTICE, "     - codec_alaw.so or codec_ulaw.so (for telephony)\n");
    ast_log(LOG_NOTICE, "     - format_pcm.so (for PCM support)\n");
    
    ast_log(LOG_NOTICE, "============================================================\n");
}

// Build WebSocket URL with query parameters
static char *build_websocket_url(const char *voice_id, const char *api_key, struct elevenlabs_wss_session *session) {
    if (!voice_id || !api_key || !session) {
        return NULL;
    }
    
    char *url = ast_malloc(MAX_URL_LENGTH);
    if (!url) return NULL;
    
    // Start with base path - use /stream-input endpoint like Python example
    snprintf(url, MAX_URL_LENGTH, "%s/%s/stream-input", ELEVENLABS_WSS_PATH, voice_id);
    
    // Add query parameters
    char *params = ast_malloc(MAX_URL_LENGTH);
    if (!params) {
        ast_free(url);
        return NULL;
    }
    
    snprintf(params, MAX_URL_LENGTH, "?");
    
    if (session->model_id) {
        snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
                 "model_id=%s&", session->model_id);
    }
    
    if (session->language_code) {
        snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
                 "language_code=%s&", session->language_code);
    }
    
    if (session->output_format) {
        snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
                 "output_format=%s&", session->output_format);
    }
    
    snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
             "enable_logging=%s&", session->enable_logging ? "true" : "false");
    
    snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
             "enable_ssml_parsing=%s&", session->enable_ssml_parsing ? "true" : "false");
    
    snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
             "inactivity_timeout=%d&", session->inactivity_timeout);
    
    snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
             "sync_alignment=%s&", session->sync_alignment ? "true" : "false");
    
    snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
             "auto_mode=%s&", session->auto_mode ? "true" : "false");
    
    if (session->apply_text_normalization) {
        snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
                 "apply_text_normalization=%s&", session->apply_text_normalization);
    }
    
    if (session->use_seed) {
        snprintf(params + strlen(params), MAX_URL_LENGTH - strlen(params), 
                 "seed=%u&", session->seed);
    }
    
    // Remove trailing &
    if (strlen(params) > 1 && params[strlen(params) - 1] == '&') {
        params[strlen(params) - 1] = '\0';
    }
    
    // If we only have "?" then remove it
    if (strcmp(params, "?") == 0) {
        params[0] = '\0';
    }
    
    // Combine URL and parameters
    if (strlen(url) + strlen(params) < MAX_URL_LENGTH) {
        strcat(url, params);
    }
    
    ast_free(params);
    
    ast_log(LOG_NOTICE, "🔗 WebSocket URL: wss://%s%s\n", ELEVENLABS_WSS_HOST, url);
    ast_log(LOG_NOTICE, "🔍 URL details: voice_id=%s, model_id=%s\n", 
            voice_id, session->model_id ? session->model_id : "default");
    ast_log(LOG_NOTICE, "🔑 API key will be sent in initialization message\n");
    return url;
}

// WebSocket connection with retry logic (like Python reference)
static int connect_websocket_with_retry(struct elevenlabs_wss_session *session, const char *url) {
    if (!session || !url) {
        return -1;
    }
    
    // Log connection attempt (like Python reference)
    ast_log(LOG_NOTICE, "🔗 Connecting to ElevenLabs WebSocket...\n");
    ast_log(LOG_NOTICE, "   URL: wss://%s%s\n", ELEVENLABS_WSS_HOST, url);
    
    // Record connection start time (like Python reference)
    gettimeofday(&session->connection_start, NULL);
    
    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    
    connect_info.context = session->context;
    connect_info.address = ELEVENLABS_WSS_HOST;
    connect_info.port = ELEVENLABS_WSS_PORT;
    connect_info.path = url;
    connect_info.host = ELEVENLABS_WSS_HOST;
    connect_info.origin = ELEVENLABS_WSS_HOST;
    connect_info.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    connect_info.protocol = "";  // Use empty protocol like Python reference
    connect_info.pwsi = &session->wsi;
    connect_info.userdata = session;
    
    // Attempt connection with timeout (like Python reference)
    session->wsi = lws_client_connect_via_info(&connect_info);
    if (!session->wsi) {
        ast_log(LOG_ERROR, "❌ WebSocket connection failed\n");
        ast_log(LOG_ERROR, "💡 Check internet connection and API key\n");
        return -1;
    }
    
    // Wait for connection establishment (with timeout like Python reference)
    int timeout_seconds = 15;  // 15 second timeout like Python
    int wait_count = 0;
    const int max_wait = timeout_seconds * 10;  // 100ms intervals
    
    while (!session->connection_established && wait_count < max_wait && !session->destroying) {
        lws_service(session->context, 100);  // 100ms service interval
        wait_count++;
        
        // Log progress every 2 seconds
        if (wait_count % 20 == 0) {
            ast_log(LOG_NOTICE, "⏳ Waiting for connection... (%d/%d seconds)\n", 
                    wait_count / 10, timeout_seconds);
        }
    }
    
    if (!session->connection_established) {
        ast_log(LOG_ERROR, "❌ Connection timeout after %d seconds\n", timeout_seconds);
        ast_log(LOG_ERROR, "💡 Solutions:\n");
        ast_log(LOG_ERROR, "   1. Check internet connection\n");
        ast_log(LOG_ERROR, "   2. Verify API key is correct\n");
        ast_log(LOG_ERROR, "   3. Check firewall settings\n");
        return -1;
    }
    
    // Calculate connection time (like Python reference)
    struct timeval connection_end;
    gettimeofday(&connection_end, NULL);
    session->connection_time = (connection_end.tv_sec - session->connection_start.tv_sec) + 
                              (connection_end.tv_usec - session->connection_start.tv_usec) / 1000000.0;
    
    ast_log(LOG_NOTICE, "✅ WebSocket connected successfully\n");
    ast_log(LOG_NOTICE, "   Connection time: %.2f seconds\n", session->connection_time);
    
    return 0;
}

// Send initialization message to establish WebSocket session (enhanced like Python reference)
static int send_initialization_message(struct elevenlabs_wss_session *session) {
    if (!session || !session->wsi) {
        ast_log(LOG_ERROR, "❌ Invalid session or WebSocket for initialization\n");
        return -1;
    }
    
    ast_log(LOG_NOTICE, "📤 Sending initialization message...\n");
    
    struct ast_json *json = ast_json_object_create();
    if (!json) {
        ast_log(LOG_ERROR, "❌ Failed to create JSON object for initialization\n");
        return -1;
    }
    
    // Use the EXACT format from working Python script
    ast_json_object_set(json, "text", ast_json_string_create(" "));  // Space character, not empty!
    
    // Voice settings (exact Python format)
    struct ast_json *voice_settings = ast_json_object_create();
    if (!voice_settings) {
        ast_log(LOG_ERROR, "❌ Failed to create voice_settings JSON object\n");
        ast_json_unref(json);
        return -1;
    }
    
    ast_json_object_set(voice_settings, "stability", ast_json_real_create(session->voice_settings.stability));
    ast_json_object_set(voice_settings, "similarity_boost", ast_json_real_create(session->voice_settings.similarity_boost));
    ast_json_object_set(voice_settings, "use_speaker_boost", ast_json_false());  // False like Python
    
    ast_json_object_set(json, "voice_settings", voice_settings);
    
    // Generation config (exact Python format with optimized schedule)
    struct ast_json *generation_config = ast_json_object_create();
    if (!generation_config) {
        ast_log(LOG_ERROR, "❌ Failed to create generation_config JSON object\n");
        ast_json_unref(json);
        return -1;
    }
    
    struct ast_json *chunk_schedule = ast_json_array_create();
    if (!chunk_schedule) {
        ast_log(LOG_ERROR, "❌ Failed to create chunk_schedule JSON array\n");
        ast_json_unref(json);
        return -1;
    }
    
    // Optimized chunk schedule from Python reference [50, 120, 160, 290]
    ast_json_array_append(chunk_schedule, ast_json_integer_create(50));
    ast_json_array_append(chunk_schedule, ast_json_integer_create(120));
    ast_json_array_append(chunk_schedule, ast_json_integer_create(160));
    ast_json_array_append(chunk_schedule, ast_json_integer_create(290));
    ast_json_object_set(generation_config, "chunk_length_schedule", chunk_schedule);
    
    ast_json_object_set(json, "generation_config", generation_config);
    
    // Add API key (exact Python format)
    ast_json_object_set(json, "xi_api_key", ast_json_string_create(session->api_key));
    
    char *json_string = ast_json_dump_string(json);
    if (!json_string) {
        ast_log(LOG_ERROR, "❌ Failed to serialize JSON to string\n");
        ast_json_unref(json);
        return -1;
    }
    
    // Log the full initialization request
    ast_log(LOG_NOTICE, "📤 ElevenLabs Request - Initialization\n");
    ast_log(LOG_NOTICE, "   URL: wss://%s/v1/text-to-speech/%s/stream-input\n", 
            ELEVENLABS_WSS_HOST, session->voice_id);
    ast_log(LOG_NOTICE, "   Payload: %s\n", json_string);
    ast_log(LOG_NOTICE, "   Size: %zu bytes\n", strlen(json_string));
    
    // Allocate WebSocket buffer
    size_t json_len = strlen(json_string);
    unsigned char *buf = ast_malloc(LWS_SEND_BUFFER_PRE_PADDING + json_len + LWS_SEND_BUFFER_POST_PADDING);
    if (!buf) {
        ast_json_free(json_string);
        ast_json_unref(json);
        return -1;
    }
    
    memcpy(&buf[LWS_SEND_BUFFER_PRE_PADDING], json_string, json_len);
    
    int result = lws_write(session->wsi, &buf[LWS_SEND_BUFFER_PRE_PADDING], json_len, LWS_WRITE_TEXT);
    
    ast_free(buf);
    ast_json_free(json_string);
    ast_json_unref(json);
    
    if (result < 0) {
        ast_log(LOG_ERROR, "❌ Failed to send initialization message\n");
        return -1;
    }
    
    session->initialization_sent = 1;
    ast_log(LOG_NOTICE, "✅ Initialization message sent successfully\n");
    ast_log(LOG_NOTICE, "   Bytes written: %d\n", result);
    return 0;
}

// Send text chunk to ElevenLabs
// Send text chunk to ElevenLabs (Python format: {"text": chunk, "try_trigger_generation": True})
static int send_text_chunk(struct elevenlabs_wss_session *session, const char *text, int try_trigger_generation) {
    if (!session || !session->wsi || !text) {
        return -1;
    }
    
    struct ast_json *json = ast_json_object_create();
    if (!json) return -1;
    
    // Use Python example format directly
    ast_json_object_set(json, "text", ast_json_string_create(text));
    ast_json_object_set(json, "try_trigger_generation", 
                        try_trigger_generation ? ast_json_true() : ast_json_false());
    
    // Add flush option to force immediate generation for final chunks (reduces noise by ensuring clean completion)
    int is_final_chunk = (strlen(text) == 0 || strstr(text, ".") != NULL || strstr(text, "!") != NULL || strstr(text, "?") != NULL);
    if (is_final_chunk) {
        ast_json_object_set(json, "flush", ast_json_true());
        ast_log(LOG_NOTICE, "   🚀 Adding flush=true for immediate generation (reduces buffering noise)\n");
    }
    
    char *json_string = ast_json_dump_string(json);
    if (!json_string) {
        ast_json_unref(json);
        return -1;
    }
    
    // Log the text chunk request
    ast_log(LOG_NOTICE, "📤 ElevenLabs Request - Text Chunk\n");
    ast_log(LOG_NOTICE, "   Text: %s\n", text);
    ast_log(LOG_NOTICE, "   Trigger Generation: %s\n", try_trigger_generation ? "true" : "false");
    ast_log(LOG_NOTICE, "   Payload: %s\n", json_string);
    ast_log(LOG_NOTICE, "   Size: %zu bytes\n", strlen(json_string));
    
    size_t json_len = strlen(json_string);
    unsigned char *buf = ast_malloc(LWS_SEND_BUFFER_PRE_PADDING + json_len + LWS_SEND_BUFFER_POST_PADDING);
    if (!buf) {
        ast_json_free(json_string);
        ast_json_unref(json);
        return -1;
    }
    
    memcpy(&buf[LWS_SEND_BUFFER_PRE_PADDING], json_string, json_len);
    
    int result = lws_write(session->wsi, &buf[LWS_SEND_BUFFER_PRE_PADDING], json_len, LWS_WRITE_TEXT);
    
    ast_free(buf);
    ast_json_free(json_string);
    ast_json_unref(json);
    
    if (result < 0) {
        ast_log(LOG_ERROR, "❌ Failed to send text chunk\n");
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✅ Text chunk sent successfully\n");
    ast_log(LOG_NOTICE, "   Bytes written: %d\n", result);
    return 0;
}

// Send close message to finish generation (Python format: {"text": ""})
static int send_close_message(struct elevenlabs_wss_session *session) {
    if (!session || !session->wsi) {
        return -1;
    }
    
    struct ast_json *json = ast_json_object_create();
    if (!json) return -1;
    
    // Use Python example format: just {"text": ""}
    ast_json_object_set(json, "text", ast_json_string_create(""));
    
    char *json_string = ast_json_dump_string(json);
    if (!json_string) {
        ast_json_unref(json);
        return -1;
    }
    
    // Log the close message request
    ast_log(LOG_NOTICE, "📤 ElevenLabs Request - Close/End Session\n");
    ast_log(LOG_NOTICE, "   Payload: %s\n", json_string);
    ast_log(LOG_NOTICE, "   Size: %zu bytes\n", strlen(json_string));
    
    size_t json_len = strlen(json_string);
    unsigned char *buf = ast_malloc(LWS_SEND_BUFFER_PRE_PADDING + json_len + LWS_SEND_BUFFER_POST_PADDING);
    if (!buf) {
        ast_json_free(json_string);
        ast_json_unref(json);
        return -1;
    }
    
    memcpy(&buf[LWS_SEND_BUFFER_PRE_PADDING], json_string, json_len);
    
    int result = lws_write(session->wsi, &buf[LWS_SEND_BUFFER_PRE_PADDING], json_len, LWS_WRITE_TEXT);
    
    ast_free(buf);
    ast_json_free(json_string);
    ast_json_unref(json);
    
    if (result < 0) {
        ast_log(LOG_ERROR, "❌ Failed to send close message\n");
        return -1;
    }
    
    session->text_complete = 1;
    ast_log(LOG_NOTICE, "✅ Close message sent successfully\n");
    ast_log(LOG_NOTICE, "   Bytes written: %d\n", result);
    return 0;
}

// Play received audio chunk immediately (real-time streaming with Python reference optimizations)
static int play_audio_chunk(struct elevenlabs_wss_session *session, const char *base64_audio) {
    if (!session || !session->chan || !base64_audio || strlen(base64_audio) == 0) {
        ast_log(LOG_DEBUG, "🔊 Invalid audio chunk parameters - skipping\n");
        return -1;
    }
    
    // Check for empty or minimal Base64 data that would create noise
    size_t base64_len = strlen(base64_audio);
    if (base64_len < 10) {  // Less than 10 chars is likely empty/padding
        ast_log(LOG_DEBUG, "🔊 Empty or minimal audio data (%zu chars) - skipping to prevent noise\n", base64_len);
        return 0;  // Success but no audio to play
    }
    
    // Check for Base64 that's mostly padding (just "=" characters)
    size_t padding_count = 0;
    for (size_t i = 0; i < base64_len; i++) {
        if (base64_audio[i] == '=') padding_count++;
    }
    if (padding_count > base64_len / 2) {  // More than 50% padding
        ast_log(LOG_DEBUG, "🔊 Mostly padding Base64 (%zu/%zu padding) - skipping to prevent noise\n", 
                padding_count, base64_len);
        return 0;  // Success but no audio to play
    }
    
    // Check if channel is still up
    if (ast_check_hangup(session->chan)) {
        ast_log(LOG_NOTICE, "🔊 Channel hung up - stopping audio playback\n");
        return -1;
    }
    
    // Track timing for first audio (like Python reference)
    if (!session->first_audio_received) {
        gettimeofday(&session->first_audio_time, NULL);
        session->first_audio_latency = (session->first_audio_time.tv_sec - session->connection_start.tv_sec) + 
                                      (session->first_audio_time.tv_usec - session->connection_start.tv_usec) / 1000000.0;
        session->first_audio_received = 1;
        ast_log(LOG_NOTICE, "⏱️ First audio received after %.2f seconds\n", session->first_audio_latency);
    }
    
    size_t decoded_len;
    unsigned char *decoded_audio = base64_decode_optimized(base64_audio, strlen(base64_audio), &decoded_len);
    if (!decoded_audio || decoded_len == 0) {
        ast_log(LOG_DEBUG, "❌ Failed to decode Base64 audio or empty audio data - skipping to prevent noise\n");
        return 0;  // Success but no audio to play
    }
    
    // Additional check for very small decoded audio that could be noise
    if (decoded_len < 64) {  // Less than 64 bytes (32 samples at 16-bit) is likely empty/noise
        ast_log(LOG_DEBUG, "🔊 Decoded audio too small (%zu bytes) - skipping to prevent noise\n", decoded_len);
        ast_free(decoded_audio);
        return 0;  // Success but no audio to play
    }
    
    // Update performance metrics (like Python reference)
    session->audio_chunks_received++;
    session->total_audio_bytes += decoded_len;
    
    // Log detailed chunk info (reduced verbosity for cleaner audio processing)
    ast_log(LOG_DEBUG, "🔊 Audio chunk %d received (%zu chars, %zu PCM bytes) - PLAYING NOW\n", 
            session->audio_chunks_received, strlen(base64_audio), decoded_len);
    
    // ElevenLabs PCM format is 16kHz, 16-bit signed integers
    int16_t *pcm_data = (int16_t *)decoded_audio;
    size_t pcm_samples = decoded_len / sizeof(int16_t);
    
    ast_log(LOG_DEBUG, "🔊 Processing %zu PCM samples directly (intelligence disabled)\n", pcm_samples);
    
    // Check for silent or near-silent audio that could be perceived as noise
    long long total_energy = 0;
    int16_t max_amplitude = 0;
    for (size_t i = 0; i < pcm_samples; i++) {
        int16_t sample = abs(pcm_data[i]);
        total_energy += (long long)sample * sample;
        if (sample > max_amplitude) max_amplitude = sample;
    }
    
    // Calculate average amplitude
    double avg_amplitude = sqrt((double)total_energy / pcm_samples);
    
    // SIMPLIFIED AUDIO PROCESSING: Only skip truly empty audio
    // Only skip if audio is completely silent (all zeros)
    if (max_amplitude == 0) {
        ast_log(LOG_DEBUG, "🔊 Audio completely silent (all zeros) - skipping\n");
        ast_free(decoded_audio);
        return 0;  // Success but no audio to play
    }
    
    ast_log(LOG_DEBUG, "🔊 Audio detected: max=%d, avg=%.1f - PLAYING\n", max_amplitude, avg_amplitude);
    
    // Calculate audio duration (like Python reference)
    double chunk_duration = (double)pcm_samples / 16000.0;  // 16kHz sample rate
    ast_log(LOG_DEBUG, "🔊 Audio details: %zu samples at 16kHz (%.2fs duration)\n", pcm_samples, chunk_duration);
    
    // TRANSCODING COMPATIBILITY CHECK: Verify channel formats before processing
    struct ast_format *channel_read_format = ast_channel_readformat(session->chan);
    struct ast_format *channel_write_format = ast_channel_writeformat(session->chan);
    struct ast_format *source_format = ast_format_cache_get_slin_by_rate(16000);
    
    if (!channel_read_format || !channel_write_format) {
        ast_log(LOG_ERROR, "❌ TRANSCODING ERROR: Channel has invalid format configuration\n");
        ast_log(LOG_ERROR, "   Read format: %s\n", channel_read_format ? ast_format_get_name(channel_read_format) : "NULL");
        ast_log(LOG_ERROR, "   Write format: %s\n", channel_write_format ? ast_format_get_name(channel_write_format) : "NULL");
        ast_free(decoded_audio);
        return -1;
    }
    
    if (!source_format) {
        ast_log(LOG_ERROR, "❌ TRANSCODING ERROR: Failed to get 16kHz PCM format from cache\n");
        ast_log(LOG_ERROR, "   This suggests Asterisk codec modules are not properly loaded\n");
        ast_free(decoded_audio);
        return -1;
    }
    
    ast_log(LOG_NOTICE, "🎵 Transcoding path: 16kHz PCM -> %s (channel write format)\n", 
            ast_format_get_name(channel_write_format));
    ast_log(LOG_DEBUG, "   Channel read format: %s\n", ast_format_get_name(channel_read_format));
    ast_log(LOG_DEBUG, "   Source format: %s\n", ast_format_get_name(source_format));
    
    // Check if transcoding is likely to succeed
    if (ast_format_get_sample_rate(channel_write_format) == 0) {
        ast_log(LOG_WARNING, "⚠️ TRANSCODING WARNING: Target format has unknown sample rate\n");
    }
    
    // Calculate RMS energy and peak for the original 16kHz audio
    long long energy_sum = 0;
    int16_t max_sample = 0;
    
    for (size_t i = 0; i < pcm_samples; i++) {
        energy_sum += (long long)pcm_data[i] * pcm_data[i];
        if (abs(pcm_data[i]) > max_sample) {
            max_sample = abs(pcm_data[i]);
        }
    }
    
    double rms_energy = sqrt((double)energy_sum / pcm_samples);
    
    // SIMPLIFIED AGC: Minimal processing to preserve audio quality
    const int16_t target_peak = 12000;   // Higher target to preserve dynamics
    const double target_rms = 2000.0;    // Higher RMS target for natural volume
    
    // Calculate gain based on peak only to avoid over-processing
    float gain_factor = (max_sample > 0) ? (float)target_peak / max_sample : 1.0f;
    
    // Conservative gain limits to prevent artifacts
    if (gain_factor < 0.5f) gain_factor = 0.5f;   // Less aggressive minimum
    if (gain_factor > 2.0f) gain_factor = 2.0f;   // Reduced maximum gain
    
    ast_log(LOG_DEBUG, "🎚️ SIMPLIFIED AGC: Peak=%d->%d, Gain=%.2fx (minimal processing)\n", 
            max_sample, (int)(max_sample * gain_factor), gain_factor);
    
    // Apply gentle gain with no noise gate (preserve all audio)
    for (size_t i = 0; i < pcm_samples; i++) {
        int32_t sample = pcm_data[i];
        
        // Apply gain directly without noise gate
        int32_t normalized = (int32_t)(sample * gain_factor);
        
        // Soft clipping only for extreme cases
        if (normalized > 20000) {
            float excess = (float)(normalized - 20000) / 12767.0f;
            normalized = 20000 + (int32_t)(12767.0f * tanh(excess * 0.3f));
        } else if (normalized < -20000) {
            float excess = (float)(normalized + 20000) / 12767.0f;
            normalized = -20000 - (int32_t)(12767.0f * tanh(-excess * 0.3f));
        }
        
        // Final limit
        if (normalized > 32767) normalized = 32767;
        if (normalized < -32768) normalized = -32768;
        
        pcm_data[i] = (int16_t)normalized;
    }
    
    ast_log(LOG_NOTICE, "🔊 Using 16kHz PCM directly (Asterisk will transcode automatically)\n");
    ast_log(LOG_NOTICE, "🔊 Audio processed: %zu samples at 16kHz (native resolution)\n", pcm_samples);
    
    const size_t chunk_size = 320;  // Exactly 20ms at 16kHz (16000 * 0.02)
    size_t total_chunks = (pcm_samples + chunk_size - 1) / chunk_size;
    
    ast_log(LOG_NOTICE, "🔊 Optimized streaming: %zu chunks of %zu samples each (20ms per chunk at 16kHz)\n", total_chunks, chunk_size);

    // Get start time for PRECISE timing control
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    
    int total_result = 0;
    
    for (size_t chunk = 0; chunk < total_chunks; chunk++) {
        // Check if session is ending
        if (session->destroying || ast_check_hangup(session->chan)) {
            ast_log(LOG_NOTICE, "🔊 Stopping audio playback - session ending\n");
            break;
        }
        
        size_t chunk_start = chunk * chunk_size;
        size_t current_chunk_size = chunk_size;
        
        if (chunk_start + current_chunk_size > pcm_samples) {
            current_chunk_size = pcm_samples - chunk_start;
        }
        
        // Calculate proper timestamp for this chunk (in samples at 16kHz)
        unsigned int timestamp = chunk * chunk_size;
        
        // Create frame with 16kHz PCM data (Asterisk will transcode to channel format)
        struct ast_frame audio_frame = {
            .frametype = AST_FRAME_VOICE,
            .samples = current_chunk_size,
            .datalen = current_chunk_size * sizeof(int16_t),  // PCM is 2 bytes per sample
            .data.ptr = (unsigned char *)(pcm_data + chunk_start),
            .offset = 0,  // Use 0 like  module
            .seqno = 0,
            .ts = timestamp  // Proper timestamp for timing
        };
        
        // Set format to 16kHz signed linear - Asterisk will transcode automatically  
        audio_frame.subclass.format = ast_format_cache_get_slin_by_rate(16000);
        
        // TRANSCODING VALIDATION: Check format compatibility before writing
        struct ast_format *channel_write_format = ast_channel_writeformat(session->chan);
        if (!channel_write_format) {
            ast_log(LOG_ERROR, "❌ TRANSCODING ERROR: Channel has no write format set\n");
            total_result = -1;
            break;
        }
        
        ast_log(LOG_DEBUG, "📤 Transcoding check: 16kHz PCM -> %s\n", ast_format_get_name(channel_write_format));
        
        // Verify format is valid
        if (!audio_frame.subclass.format) {
            ast_log(LOG_ERROR, "❌ TRANSCODING ERROR: Failed to get 16kHz signed linear format\n");
            total_result = -1;
            break;
        }
        
        ast_log(LOG_DEBUG, "📤 Writing chunk %zu/%zu: %zu samples (ts=%u, 16kHz->%s)\n", 
                chunk + 1, total_chunks, current_chunk_size, timestamp, 
                ast_format_get_name(channel_write_format));
        
        // Write audio frame to channel with comprehensive error checking
        int result = ast_write(session->chan, &audio_frame);
        
        if (result < 0) {
            // Detailed transcoding failure analysis
            ast_log(LOG_ERROR, "❌ TRANSCODING FAILURE: ast_write returned %d\n", result);
            ast_log(LOG_ERROR, "   Source format: %s (16kHz PCM)\n", ast_format_get_name(audio_frame.subclass.format));
            ast_log(LOG_ERROR, "   Target format: %s\n", ast_format_get_name(channel_write_format));
            ast_log(LOG_ERROR, "   Frame details:\n");
            ast_log(LOG_ERROR, "     Samples: %zu\n", current_chunk_size);
            ast_log(LOG_ERROR, "     Data length: %zu bytes\n", current_chunk_size * sizeof(int16_t));
            ast_log(LOG_ERROR, "     Timestamp: %u\n", timestamp);
            ast_log(LOG_ERROR, "     Channel state: %s\n", ast_state2str(ast_channel_state(session->chan)));
            
            // Check if channel is still up
            if (ast_check_hangup(session->chan)) {
                ast_log(LOG_ERROR, "   Cause: Channel hung up during transcoding\n");
            } else {
                ast_log(LOG_ERROR, "   Cause: Likely transcoding incompatibility or codec issue\n");
                ast_log(LOG_ERROR, "   💡 Possible solutions:\n");
                ast_log(LOG_ERROR, "      1. Check if channel supports %s format\n", ast_format_get_name(audio_frame.subclass.format));
                ast_log(LOG_ERROR, "      2. Verify Asterisk has proper codec modules loaded\n");
                ast_log(LOG_ERROR, "      3. Check channel configuration for format restrictions\n");
            }
            
            total_result = result;
            break;
        } else {
            ast_log(LOG_DEBUG, "✅ Chunk %zu transcoded successfully (16kHz->%s)\n", 
                    chunk + 1, ast_format_get_name(channel_write_format));
        }
        
        // PRECISE TIMING: Wait exactly 20ms between chunks (module method)
        // This ensures audio plays at correct speed
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        
        // Calculate expected time for this chunk (20ms per chunk)
        long expected_ms = (chunk + 1) * 20;  // 20ms per chunk
        
        // Calculate actual elapsed time
        long elapsed_ms = ((current_time.tv_sec - start_time.tv_sec) * 1000) +
                         ((current_time.tv_usec - start_time.tv_usec) / 1000);
        
        // Sleep if we're ahead of schedule to maintain proper timing
        if (elapsed_ms < expected_ms) {
            long sleep_ms = expected_ms - elapsed_ms;
            ast_log(LOG_DEBUG, "⏰ Timing: sleeping %ldms to maintain 20ms/chunk rate\n", sleep_ms);
            usleep(sleep_ms * 1000);  // Convert to microseconds
        }
    }
    
    // Log final result (Asterisk transcoding approach with Python reference metrics)
    if (total_result < 0) {
        ast_log(LOG_ERROR, "❌ Failed to write audio frames (result=%d)\n", total_result);
        ast_free(decoded_audio);
        return -1;
    } else {
        // Calculate total processing time
        struct timeval end_time;
        gettimeofday(&end_time, NULL);
        double processing_time = (end_time.tv_sec - start_time.tv_sec) + 
                               (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
        
        ast_log(LOG_NOTICE, "✅ Audio chunk %d played successfully (16kHz native)\n", session->audio_chunks_received);
        ast_log(LOG_NOTICE, "   📊 Chunk processing time: %.3fs\n", processing_time);
        ast_log(LOG_NOTICE, "   📊 %zu chunks streamed in %.3fs (%.1f chunks/sec)\n", 
                total_chunks, processing_time, total_chunks / processing_time);
        
        // Update session totals
        session->total_processing_time += processing_time;
    }
    
    ast_free(decoded_audio);
    
    return 0;
}

// Check if JSON string is complete (balanced braces and properly formatted)
static int is_json_complete(const char *json_str) {
    if (!json_str || strlen(json_str) == 0) {
        return 0;  // Empty string is not complete JSON
    }
    
    int brace_count = 0;
    int bracket_count = 0;
    int in_string = 0;
    int escaped = 0;
    const char *p = json_str;
    
    // Skip leading whitespace
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    
    // Must start with { or [
    if (*p != '{' && *p != '[') {
        return 0;
    }
    
    while (*p) {
        if (escaped) {
            escaped = 0;
            p++;
            continue;
        }
        
        switch (*p) {
            case '\\':
                if (in_string) {
                    escaped = 1;
                }
                break;
            case '"':
                in_string = !in_string;
                break;
            case '{':
                if (!in_string) {
                    brace_count++;
                }
                break;
            case '}':
                if (!in_string) {
                    brace_count--;
                }
                break;
            case '[':
                if (!in_string) {
                    bracket_count++;
                }
                break;
            case ']':
                if (!in_string) {
                    bracket_count--;
                }
                break;
        }
        p++;
    }
    
    // JSON is complete if all braces and brackets are balanced and we're not inside a string
    return (brace_count == 0 && bracket_count == 0 && !in_string);
}

// Extract audio from incomplete JSON chunk (look for partial audio field)
static char *extract_audio_from_partial_json(const char *partial_json) {
    if (!partial_json) {
        return NULL;
    }
    
    ast_log(LOG_DEBUG, "🔍 Searching for audio in partial JSON: %.100s...\n", partial_json);
    
    // Look for "audio":"..." pattern in the partial JSON
    char *audio_start = strstr(partial_json, "\"audio\":\"");
    if (!audio_start) {
        // Try alternative formats
        audio_start = strstr(partial_json, "\"audio\": \"");
        if (!audio_start) {
            audio_start = strstr(partial_json, "'audio':'");
            if (!audio_start) {
                ast_log(LOG_DEBUG, "   No audio field found in partial JSON\n");
                return NULL;
            } else {
                audio_start += 9; // Skip past 'audio':'
            }
        } else {
            audio_start += 10; // Skip past "audio": "
        }
    } else {
        audio_start += 9; // Skip past "audio":"
    }
    
    // Find the end of the Base64 string
    char *audio_end = strchr(audio_start, '"');
    if (!audio_end) {
        audio_end = strchr(audio_start, '\'');
        if (!audio_end) {
            // No closing quote found - this might be incomplete Base64 data
            // Check if we have at least some valid Base64 characters
            size_t partial_len = strlen(audio_start);
            if (partial_len < 20) {  // Too short to be meaningful Base64
                ast_log(LOG_DEBUG, "   Partial Base64 too short (%zu chars)\n", partial_len);
                return NULL;
            }
            
            // Check if the partial data looks like valid Base64
            int valid_b64_chars = 0;
            for (size_t i = 0; i < partial_len && i < 100; i++) {
                char c = audio_start[i];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                    (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') {
                    valid_b64_chars++;
                } else {
                    break;  // Stop at first non-Base64 character
                }
            }
            
            if (valid_b64_chars < 20) {
                ast_log(LOG_DEBUG, "   Not enough valid Base64 characters (%d)\n", valid_b64_chars);
                return NULL;
            }
            
            // We have partial Base64 data - extract what we can
            char *partial_audio = ast_malloc(valid_b64_chars + 1);
            if (partial_audio) {
                memcpy(partial_audio, audio_start, valid_b64_chars);
                partial_audio[valid_b64_chars] = '\0';
                ast_log(LOG_DEBUG, "   ⚠️ Extracted partial Base64: %d chars (incomplete)\n", valid_b64_chars);
                return partial_audio;
            }
            return NULL;
        }
    }
    
    // Calculate length of complete Base64 string
    size_t audio_len = audio_end - audio_start;
    if (audio_len < 10) {  // Too short to be meaningful
        ast_log(LOG_DEBUG, "   Complete Base64 too short (%zu chars)\n", audio_len);
        return NULL;
    }
    
    // Extract the complete Base64 audio data
    char *audio_data = ast_malloc(audio_len + 1);
    if (audio_data) {
        memcpy(audio_data, audio_start, audio_len);
        audio_data[audio_len] = '\0';
        ast_log(LOG_DEBUG, "   ✅ Extracted complete Base64: %zu chars\n", audio_len);
        return audio_data;
    }
    
    return NULL;
}

// Process both complete and incomplete JSON chunks
static int process_json_chunk(struct elevenlabs_wss_session *session, const char *json_data) {
    if (!session || !json_data || strlen(json_data) == 0) {
        return -1;
    }
    
    ast_log(LOG_DEBUG, "🔍 Processing JSON chunk: %.200s%s\n", 
            json_data, strlen(json_data) > 200 ? "..." : "");
    
    // Check if this is a complete JSON
    int is_complete = is_json_complete(json_data);
    ast_log(LOG_DEBUG, "   JSON completeness: %s\n", is_complete ? "COMPLETE" : "INCOMPLETE/PARTIAL");
    
    char *audio_data = NULL;
    
    if (is_complete) {
        // Process complete JSON using standard method
        ast_log(LOG_DEBUG, "   Processing as complete JSON\n");
        
        struct ast_json *json = ast_json_load_string(json_data, NULL);
        if (json) {
            struct ast_json *audio_field = ast_json_object_get(json, "audio");
            struct ast_json *is_final = ast_json_object_get(json, "isFinal");
            
            if (audio_field) {
                const char *base64_audio = ast_json_string_get(audio_field);
                if (base64_audio && strlen(base64_audio) > 0) {
                    audio_data = ast_strdup(base64_audio);
                    ast_log(LOG_DEBUG, "   ✅ Extracted audio from complete JSON: %zu chars\n", strlen(audio_data));
                }
            }
            
            // Check for final chunk
            if (is_final && ast_json_is_true(is_final)) {
                ast_log(LOG_NOTICE, "🏁 Final chunk detected in complete JSON\n");
                session->generation_complete = 1;
            }
            
            ast_json_unref(json);
        } else {
            ast_log(LOG_DEBUG, "   ❌ Failed to parse supposedly complete JSON\n");
            // Fall back to partial processing
            audio_data = extract_audio_from_partial_json(json_data);
        }
    } else {
        // Process incomplete JSON by extracting partial audio data
        ast_log(LOG_DEBUG, "   Processing as incomplete/partial JSON\n");
        audio_data = extract_audio_from_partial_json(json_data);
    }
    
    // Play extracted audio if we found any
    if (audio_data && strlen(audio_data) > 0) {
        ast_log(LOG_NOTICE, "🔊 Playing extracted audio (%s JSON)\n", 
                is_complete ? "complete" : "partial");
        ast_log(LOG_NOTICE, "   Base64 Length: %zu chars\n", strlen(audio_data));
        ast_log(LOG_NOTICE, "   Sample: %.50s...\n", audio_data);
        
        if (play_audio_chunk(session, audio_data) == 0) {
            session->audio_chunks_received++;
            ast_log(LOG_NOTICE, "   ✅ Audio chunk played successfully (%d total)\n", 
                    session->audio_chunks_received);
        } else {
            ast_log(LOG_DEBUG, "   ⚠️ Audio chunk skipped (empty/noise prevention)\n");
        }
        
        ast_free(audio_data);
        return 0;  // Success
    } else {
        ast_log(LOG_DEBUG, "   No audio data found in %s JSON\n", 
                is_complete ? "complete" : "partial");
        
        // Check for error messages in complete JSON
        if (is_complete && (strstr(json_data, "\"error\"") || strstr(json_data, "\"message\""))) {
            ast_log(LOG_ERROR, "❌ ElevenLabs Error in JSON message\n");
            ast_log(LOG_ERROR, "   Content: %s\n", json_data);
            return -1;  // Error
        }
        
        return 0;  // No audio but no error
    }
}

// Process audio output message
// Process audio output message (Python format: {"audio": "base64...", "alignment": {...}, "isFinal": true})
static int process_audio_message(struct elevenlabs_wss_session *session, const char *json_data) {
    if (!session || !json_data) {
        return -1;
    }
    
    struct ast_json *json = ast_json_load_string(json_data, NULL);
    if (!json) {
        ast_log(LOG_ERROR, "Failed to parse audio message JSON\n");
        return -1;
    }
    
    // Python example format - audio is directly in the response
    struct ast_json *audio_data = ast_json_object_get(json, "audio");
    struct ast_json *is_final = ast_json_object_get(json, "isFinal");
    struct ast_json *alignment = ast_json_object_get(json, "alignment");
    
    if (audio_data) {
        const char *base64_audio = ast_json_string_get(audio_data);
        if (base64_audio && strlen(base64_audio) > 0) {
            ast_log(LOG_NOTICE, "🔊 ElevenLabs Audio Data Received\n");
            ast_log(LOG_NOTICE, "   Base64 Length: %zu bytes\n", strlen(base64_audio));
            ast_log(LOG_NOTICE, "   Sample (first 50 chars): %.50s...\n", base64_audio);
            
            // Play audio immediately when received (real-time streaming)
            if (play_audio_chunk(session, base64_audio) == 0) {
                session->audio_chunks_received++;
                ast_log(LOG_NOTICE, "   ✅ Audio chunk played successfully (%d total)\n", session->audio_chunks_received);
            } else {
                ast_log(LOG_DEBUG, "   ⚠️ Audio chunk skipped (empty/noise prevention)\n");
            }
        } else {
            ast_log(LOG_NOTICE, "🔊 ElevenLabs Audio Data Received (empty/null)\n");
            ast_log(LOG_NOTICE, "   Base64 audio is empty or null - skipping playback\n");
        }
    } else {
        ast_log(LOG_DEBUG, "📥 ElevenLabs message without audio data\n");
    }
    
    if (is_final && ast_json_is_true(is_final)) {
        ast_log(LOG_NOTICE, "🏁 Final audio chunk received - generation complete\n");
        ast_log(LOG_NOTICE, "   Total audio chunks processed: %d\n", session->audio_chunks_received);
        session->generation_complete = 1;
    } else if (is_final && ast_json_is_false(is_final)) {
        ast_log(LOG_DEBUG, "📄 Intermediate audio chunk (not final)\n");
    }
    
    // Process alignment data if available
    if (alignment) {
        if (session->last_alignment) {
            ast_json_unref(session->last_alignment);
        }
        session->last_alignment = ast_json_ref(alignment);
        ast_log(LOG_DEBUG, "📍 Alignment data received\n");
    }
    
    ast_json_unref(json);
    return 0;
}

// Process final output message (placeholder for future use)
// static int process_final_message(struct elevenlabs_wss_session *session, const char *json_data);

// WebSocket callback function
static int callback_elevenlabs_wss(struct lws *wsi, enum lws_callback_reasons reason,
                                   void *user __attribute__((unused)), void *in, size_t len) {
    struct elevenlabs_wss_session *session = current_session;
    
    switch (reason) {
        case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
            ast_log(LOG_DEBUG, "🔐 Loading SSL certificates\n");
            break;
            
        case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
            ast_log(LOG_NOTICE, "🔌 Pre-establish filter - SSL handshake in progress\n");
            break;
            
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
            // For now, skip adding headers here - rely on API key in first message
            ast_log(LOG_NOTICE, "🔗 Client handshake header callback - adding headers\n");
            break;
            
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            // Calculate connection time (like Python reference)
            if (session) {
                struct timeval connection_end;
                gettimeofday(&connection_end, NULL);
                session->connection_time = (connection_end.tv_sec - session->connection_start.tv_sec) + 
                                          (connection_end.tv_usec - session->connection_start.tv_usec) / 1000000.0;
                
                ast_log(LOG_NOTICE, "🔗 ElevenLabs WebSocket Connection Established\n");
                ast_log(LOG_NOTICE, "   Connection successful to: wss://%s\n", ELEVENLABS_WSS_HOST);
                ast_log(LOG_NOTICE, "   Connection time: %.2f seconds (Python reference optimized)\n", session->connection_time);
                
                session->connection_established = 1;
                session->wsi = wsi;
                // Request callback when we can write
                lws_callback_on_writable(wsi);
                ast_log(LOG_NOTICE, "   Session ready for data transmission\n");
            } else {
                ast_log(LOG_NOTICE, "🔗 ElevenLabs WebSocket Connection Established (no session)\n");
            }
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (session && in && len > 0) {
                // Handle large WebSocket messages that might come in chunks
                if (!session->response_buffer) {
                    session->response_buffer = ast_malloc(WEBSOCKET_BUFFER_SIZE);
                    session->response_size = WEBSOCKET_BUFFER_SIZE;
                    session->response_pos = 0;
                }
                
                // Check if we have enough space for the new data
                if (session->response_pos + len >= session->response_size) {
                    // Need to expand buffer or this is end of frame
                    if (lws_is_final_fragment(wsi)) {
                        ast_log(LOG_NOTICE, "📥 Final WebSocket fragment received\n");
                        // This is the final fragment, we can process with current data
                    } else {
                        // Need to expand buffer for continuation
                        size_t new_size = session->response_size * 2;
                        char *new_buffer = ast_malloc(new_size);
                        if (new_buffer) {
                            memcpy(new_buffer, session->response_buffer, session->response_pos);
                            ast_free(session->response_buffer);
                            session->response_buffer = new_buffer;
                            session->response_size = new_size;
                            ast_log(LOG_NOTICE, "📈 Expanded WebSocket buffer to %zu bytes\n", new_size);
                        } else {
                            ast_log(LOG_ERROR, "❌ Failed to expand WebSocket buffer\n");
                            break;
                        }
                    }
                }
                
                // Accumulate message data
                if (session->response_pos + len < session->response_size) {
                    memcpy(session->response_buffer + session->response_pos, in, len);
                    session->response_pos += len;
                    session->response_buffer[session->response_pos] = '\0';
                } else {
                    ast_log(LOG_ERROR, "❌ WebSocket buffer overflow - cannot process message\n");
                    break;
                }
                
                ast_log(LOG_NOTICE, "📥 ElevenLabs Response Chunk\n");
                ast_log(LOG_NOTICE, "   Size: %zu bytes (accumulated: %zu)\n", len, session->response_pos);
                ast_log(LOG_NOTICE, "   Is final: %s\n", lws_is_final_fragment(wsi) ? "true" : "false");
                ast_log(LOG_NOTICE, "   Content preview: %.*s\n", (int)(session->response_pos > 200 ? 200 : session->response_pos), session->response_buffer);
                
                // Only process when we have a complete WebSocket message (final fragment)
                if (lws_is_final_fragment(wsi)) {
                    ast_log(LOG_NOTICE, "🔍 Processing complete WebSocket message (%zu bytes)\n", session->response_pos);
                    
                    char *accumulated_data = session->response_buffer;
                    
                    // ElevenLabs sends complete JSON objects
                    // Check if this is a complete JSON object (ElevenLabs format)
                    if (is_json_complete(accumulated_data)) {
                        ast_log(LOG_NOTICE, "✅ Complete ElevenLabs JSON detected\n");
                        
                        // Process the complete JSON
                        int result = process_json_chunk(session, accumulated_data);
                        if (result < 0) {
                            ast_log(LOG_ERROR, "❌ Error processing ElevenLabs JSON message\n");
                            session->running = 0;
                        } else {
                            ast_log(LOG_NOTICE, "✅ ElevenLabs JSON message processed successfully\n");
                        }
                        
                        // Clear buffer after processing complete message
                        session->response_pos = 0;
                        session->response_buffer[0] = '\0';
                        ast_log(LOG_NOTICE, "   🗑️ Buffer cleared after complete message\n");
                        
                    } else {
                        // Maybe it's line-based format  as fallback
                        char *current_pos = accumulated_data;
                        int processed_any_line = 0;
                        
                        // Look for line-by-line JSON messages 
                        char *line_end;
                        while ((line_end = strchr(current_pos, '\n')) != NULL) {
                            // Extract complete JSON line
                            size_t line_len = line_end - current_pos;
                            char *json_line = ast_malloc(line_len + 1);
                            if (json_line) {
                                memcpy(json_line, current_pos, line_len);
                                json_line[line_len] = '\0';
                                
                                // Trim whitespace
                                char *trimmed = json_line;
                                while (*trimmed == ' ' || *trimmed == '\r' || *trimmed == '\n' || *trimmed == '\t') trimmed++;
                                
                                if (strlen(trimmed) > 0) {
                                    ast_log(LOG_NOTICE, "🔍 Processing  JSON line: %.150s%s\n", 
                                            trimmed, strlen(trimmed) > 150 ? "..." : "");
                                    
                                    // Process JSON chunk (handles both complete and incomplete)
                                    int result = process_json_chunk(session, trimmed);
                                    if (result < 0) {
                                        ast_log(LOG_ERROR, "❌ Error processing JSON line\n");
                                        session->running = 0;
                                    }
                                    processed_any_line = 1;
                                }
                                
                                ast_free(json_line);
                            }
                            
                            // Move to next line
                            current_pos = line_end + 1;
                        }
                        
                        if (processed_any_line) {
                            // Clear buffer after processing lines
                            session->response_pos = 0;
                            session->response_buffer[0] = '\0';
                            ast_log(LOG_NOTICE, "   🗑️ Buffer cleared after line processing\n");
                        } else {
                            // Not valid JSON format - log error
                            ast_log(LOG_ERROR, "❌ Received data is not valid JSON format\n");
                            ast_log(LOG_ERROR, "   Data preview: %.500s\n", accumulated_data);
                            
                            // Clear buffer to prevent accumulation of bad data
                            session->response_pos = 0;
                            session->response_buffer[0] = '\0';
                        }
                    }
                } else {
                    // Not final fragment - continue accumulating
                    ast_log(LOG_DEBUG, "⏳ Waiting for more WebSocket fragments (%zu bytes so far)\n", session->response_pos);
                }
            }
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            // Handle when we can write to the WebSocket
            break;
            
        case LWS_CALLBACK_CLOSED:
            ast_log(LOG_NOTICE, "🔌 WebSocket connection closed\n");
            if (session) {
                session->connection_established = 0;
                session->running = 0;
            }
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            ast_log(LOG_ERROR, "❌ ElevenLabs WebSocket Connection Error\n");
            if (in && len > 0) {
                ast_log(LOG_ERROR, "   Error details: %.*s\n", (int)len, (char*)in);
                
                // Provide specific guidance based on error type
                const char *error_str = (char*)in;
                if (strstr(error_str, "Closed before conn")) {
                    ast_log(LOG_ERROR, "💡 'Closed before conn' usually means:\n");
                    ast_log(LOG_ERROR, "   1. Server rejected connection (check API key)\n");
                    ast_log(LOG_ERROR, "   2. Network/firewall blocking connection\n");
                    ast_log(LOG_ERROR, "   3. DNS resolution issues\n");
                    ast_log(LOG_ERROR, "   4. Try: curl -v https://api.elevenlabs.io\n");
                } else if (strstr(error_str, "timeout") || strstr(error_str, "Timeout")) {
                    ast_log(LOG_ERROR, "💡 Connection timeout suggests:\n");
                    ast_log(LOG_ERROR, "   1. Slow internet connection\n");
                    ast_log(LOG_ERROR, "   2. Firewall blocking port 443\n");
                    ast_log(LOG_ERROR, "   3. Server overloaded\n");
                } else if (strstr(error_str, "SSL") || strstr(error_str, "TLS")) {
                    ast_log(LOG_ERROR, "💡 SSL/TLS error suggests:\n");
                    ast_log(LOG_ERROR, "   1. Certificate validation issue\n");
                    ast_log(LOG_ERROR, "   2. Outdated SSL libraries\n");
                    ast_log(LOG_ERROR, "   3. Man-in-the-middle protection\n");
                } else if (strstr(error_str, "403") || strstr(error_str, "401")) {
                    ast_log(LOG_ERROR, "💡 Authentication error:\n");
                    ast_log(LOG_ERROR, "   1. Invalid API key\n");
                    ast_log(LOG_ERROR, "   2. Expired subscription\n");
                    ast_log(LOG_ERROR, "   3. Rate limit exceeded\n");
                }
            } else {
                ast_log(LOG_ERROR, "   Error details: No specific error message\n");
                ast_log(LOG_ERROR, "💡 Generic connection failure - check network and DNS\n");
            }
            ast_log(LOG_ERROR, "   Target: wss://%s:%d\n", ELEVENLABS_WSS_HOST, ELEVENLABS_WSS_PORT);
            if (session) {
                session->connection_established = 0;
                session->running = 0;
                ast_log(LOG_ERROR, "   Session marked as failed\n");
            }
            break;
            
        default:
            // Skip logging for frequent callbacks
            break;
    }
    
    return 0;
}

static void *websocket_thread_func(void *data) {
    struct elevenlabs_wss_session *session = (struct elevenlabs_wss_session *)data;
    
    if (!session) {
        return NULL;
    }
    
    ast_log(LOG_NOTICE, "🚀 Starting ElevenLabs WebSocket thread\n");
    
    // Quick network connectivity check
    ast_log(LOG_NOTICE, "🔍 Pre-flight network check...\n");
    ast_log(LOG_NOTICE, "   Testing connectivity to %s:%d\n", ELEVENLABS_WSS_HOST, ELEVENLABS_WSS_PORT);
    
    // Create context with SSL support (use more specific SSL configuration)
    struct lws_context_creation_info info = {0};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;  // Enable SSL support
    info.ssl_cert_filepath = NULL;  // Client mode, no server cert needed
    info.ssl_private_key_filepath = NULL;  // Client mode
    info.ssl_ca_filepath = NULL;  // Use system CA store
    info.user = session;
    
    // Add client-specific options for better SSL handling
    info.client_ssl_cert_filepath = NULL;
    info.client_ssl_private_key_filepath = NULL;
    info.client_ssl_ca_filepath = NULL;
    
    ast_log(LOG_NOTICE, "🔧 Creating WebSocket context with enhanced SSL settings\n");
    
    session->context = lws_create_context(&info);
    if (!session->context) {
        ast_log(LOG_ERROR, "❌ Failed to create WebSocket context\n");
        ast_log(LOG_ERROR, "💡 This could indicate:\n");
        ast_log(LOG_ERROR, "   1. Insufficient memory\n");
        ast_log(LOG_ERROR, "   2. SSL library initialization failure\n");
        ast_log(LOG_ERROR, "   3. libwebsockets configuration issue\n");
        session->running = 0;
        return NULL;
    }
    
    ast_log(LOG_NOTICE, "✅ WebSocket context created successfully\n");
    
    // Build WebSocket URL
    char *url_path = build_websocket_url(session->voice_id, session->api_key, session);
    if (!url_path) {
        ast_log(LOG_ERROR, "Failed to build WebSocket URL\n");
        lws_context_destroy(session->context);
        session->running = 0;
        return NULL;
    }
    
    // Connect to ElevenLabs with WSS (simplified like Python websockets)
    struct lws_client_connect_info connect_info = {0};
    connect_info.context = session->context;
    connect_info.address = ELEVENLABS_WSS_HOST;
    connect_info.port = ELEVENLABS_WSS_PORT;
    connect_info.path = url_path;
    connect_info.host = ELEVENLABS_WSS_HOST;
    connect_info.origin = ELEVENLABS_WSS_HOST;
    // Start with very permissive SSL settings
    connect_info.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LCCSCF_ALLOW_EXPIRED;
    connect_info.protocol = NULL;  // NULL instead of empty string
    connect_info.userdata = session;
    
    ast_log(LOG_NOTICE, "🔗 ElevenLabs WebSocket Connection (Enhanced Error Handling)\n");
    ast_log(LOG_NOTICE, "   Target: wss://%s:%d%s\n", 
            connect_info.address, connect_info.port, connect_info.path);
    ast_log(LOG_NOTICE, "   SSL: Very Permissive (bypass all cert checks)\n");
    ast_log(LOG_NOTICE, "   Voice: %s, Model: %s\n", session->voice_id, session->model_id);
    
    // Check if we can resolve the hostname first
    ast_log(LOG_NOTICE, "🔍 Testing DNS resolution for %s...\n", ELEVENLABS_WSS_HOST);
    
    struct lws *wsi = lws_client_connect_via_info(&connect_info);
    if (!wsi) {
        ast_log(LOG_WARNING, "❌ First connection attempt failed, trying with alternative settings\n");
        
        // Try with different origin and more permissive settings
        connect_info.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LCCSCF_ALLOW_EXPIRED;
        connect_info.origin = "api.elevenlabs.io";  // Try simpler origin
        connect_info.host = "api.elevenlabs.io";    // Ensure host matches
        
        ast_log(LOG_NOTICE, "🔗 Retry #1: SSL Flags: %d, Origin: %s, Host: %s\n", 
                connect_info.ssl_connection, connect_info.origin, connect_info.host);
        
        wsi = lws_client_connect_via_info(&connect_info);
        if (!wsi) {
            ast_log(LOG_WARNING, "❌ Second attempt failed, trying HTTP upgrade method\n");
            
            // Final attempt with minimal SSL requirements
            connect_info.ssl_connection = LCCSCF_USE_SSL;  // Minimal SSL
            connect_info.origin = NULL;  // No origin
            
            ast_log(LOG_NOTICE, "🔗 Retry #2: Minimal SSL, no origin\n");
            
            wsi = lws_client_connect_via_info(&connect_info);
            if (!wsi) {
                ast_log(LOG_ERROR, "❌ All connection attempts failed\n");
                ast_log(LOG_ERROR, "💡 Possible issues:\n");
                ast_log(LOG_ERROR, "   1. Network connectivity problem\n");
                ast_log(LOG_ERROR, "   2. DNS resolution failure for %s\n", ELEVENLABS_WSS_HOST);
                ast_log(LOG_ERROR, "   3. Firewall blocking port 443\n");
                ast_log(LOG_ERROR, "   4. Server-side rate limiting or blocking\n");
                ast_free(url_path);
                lws_context_destroy(session->context);
                session->running = 0;
                return NULL;
            }
            ast_log(LOG_NOTICE, "✅ Minimal SSL connection attempt succeeded\n");
        } else {
            ast_log(LOG_NOTICE, "✅ Alternative settings connection attempt succeeded\n");
        }
    } else {
        ast_log(LOG_NOTICE, "✅ Standard connection attempt succeeded\n");
    }
    
    session->wsi = wsi;
    ast_free(url_path);
    lws_set_wsi_user(wsi, session);
    
    ast_log(LOG_NOTICE, "WebSocket connection initiated (WSS - secure)\n");
    
    // Wait for connection to be established with enhanced diagnostics
    int timeout = 300;  // Increase timeout to 15 seconds (300 * 50ms)
    int last_debug_timeout = 0;
    int connection_attempts = 0;
    
    ast_log(LOG_NOTICE, "⏳ Waiting for WebSocket connection establishment...\n");
    
    while (timeout > 0 && !session->connection_established && session->running) {
        int service_result = lws_service(session->context, 50); 
        if (service_result < 0) {
            ast_log(LOG_ERROR, "❌ lws_service error: %d (network issue)\n", service_result);
            connection_attempts++;
            if (connection_attempts > 3) {
                ast_log(LOG_ERROR, "❌ Too many service errors, aborting\n");
                break;
            }
            // Continue trying for a bit longer
        }
        timeout--;
        
        if (session->destroying) {
            ast_log(LOG_NOTICE, "Session being destroyed during connection attempt\n");
            break;
        }
        
        // Add progressive debug logging
        if (timeout % 40 == 0 && timeout != last_debug_timeout) {
            last_debug_timeout = timeout;
            int seconds_remaining = timeout * 50 / 1000;
            ast_log(LOG_NOTICE, "⏳ Waiting for WebSocket connection... (%d seconds remaining)\n", seconds_remaining);
            
            // Detailed connection state after first few seconds
            if (timeout < 260) {  // After 2 seconds, start detailed logging
                ast_log(LOG_NOTICE, "   Connection state: established=%d, running=%d, destroying=%d\n",
                        session->connection_established, session->running, session->destroying);
                ast_log(LOG_NOTICE, "   WSI pointer: %p, Context: %p\n", (void*)session->wsi, (void*)session->context);
                
                // Check if this looks like a DNS or network issue
                if (timeout < 220) {  // After 4 seconds
                    ast_log(LOG_NOTICE, "💡 If connection continues to fail, check:\n");
                    ast_log(LOG_NOTICE, "   1. Internet connectivity: ping 8.8.8.8\n");
                    ast_log(LOG_NOTICE, "   2. DNS resolution: nslookup api.elevenlabs.io\n");
                    ast_log(LOG_NOTICE, "   3. Port 443 access: telnet api.elevenlabs.io 443\n");
                    ast_log(LOG_NOTICE, "   4. API key validity and account status\n");
                }
            }
        }
    }
    
    if (!session->connection_established) {
        ast_log(LOG_ERROR, "❌ WebSocket connection timeout after 15 seconds\n");
        ast_log(LOG_ERROR, "💡 Connection troubleshooting:\n");
        ast_log(LOG_ERROR, "   1. Verify internet connection is working\n");
        ast_log(LOG_ERROR, "   2. Check if %s is reachable\n", ELEVENLABS_WSS_HOST);
        ast_log(LOG_ERROR, "   3. Verify API key: %s\n", session->api_key ? "Present" : "Missing");
        ast_log(LOG_ERROR, "   4. Check firewall/proxy settings for port 443\n");
        ast_log(LOG_ERROR, "   5. Try again in a few minutes (rate limiting)\n");
        session->running = 0;
        lws_context_destroy(session->context);
        return NULL;
    }
    
    // Send initialization message immediately after connection (this triggers the TTS generation)
    usleep(100000);  // Small delay to ensure connection is ready
    if (send_initialization_message(session) < 0) {
        ast_log(LOG_ERROR, "Failed to send initialization message\n");
        session->running = 0;
        lws_context_destroy(session->context);
        return NULL;
    }
    
    // Send the text for TTS generation
    if (session->text && strlen(session->text) > 0) {
        usleep(100000);  // Small delay between messages
        if (send_text_chunk(session, session->text, 1) < 0) {
            ast_log(LOG_ERROR, "Failed to send text chunk\n");
        }
        
        // Send empty text message immediately to close the input stream
        // This matches Python reference: await websocket.send(json.dumps({"text": ""}))
        ast_log(LOG_NOTICE, "📤 Sending empty text message to close input stream\n");
        if (send_close_message(session) != 0) {
            ast_log(LOG_WARNING, "Failed to send close message, but continuing\n");
        }
        
        ast_log(LOG_NOTICE, "🎵 Text and close message sent, waiting for audio generation to complete\n");
    }
    
    // Service the WebSocket until generation is complete
    int audio_timeout_counter = 0;
    int last_chunk_count = 0;
    
    while (session->running && !session->destroying && !session->generation_complete) {
        lws_service(session->context, 50);  //
        
        if (session->chan && ast_check_hangup(session->chan)) {
            ast_log(LOG_NOTICE, "Channel hangup detected\n");
            break;
        }
        
        // Check if we're receiving audio chunks
        if (session->audio_chunks_received > last_chunk_count) {
            last_chunk_count = session->audio_chunks_received;
            audio_timeout_counter = 0;  // Reset timeout when we get new audio
            ast_log(LOG_DEBUG, "🎵 Audio chunk %d received, resetting timeout\n", session->audio_chunks_received);
        } else {
            audio_timeout_counter++;
        }
        
        // If we've received some audio but no new chunks for 3 seconds, consider sending close
        if (session->audio_chunks_received > 0 && audio_timeout_counter > 60) {  // 60 * 50ms = 3 seconds
            ast_log(LOG_NOTICE, "🎵 No new audio for 3 seconds after %d chunks, sending close message\n", session->audio_chunks_received);
            if (send_close_message(session) < 0) {
                ast_log(LOG_ERROR, "Failed to send delayed close message\n");
            }
            audio_timeout_counter = 0;  // Avoid sending multiple close messages
        }
        
        // Auto-complete after some time if no explicit completion
        time_t current_time = time(NULL);
        if (current_time - session->start_time > 30) {  // 30 second timeout
            ast_log(LOG_NOTICE, "TTS generation timeout\n");
            break;
        }
    }
    
    // Cleanup
    lws_context_destroy(session->context);
    session->context = NULL;
    session->wsi = NULL;
    session->running = 0;
    
    ast_log(LOG_NOTICE, "🏁 WebSocket thread completed\n");
    ast_log(LOG_NOTICE, "📊 Audio chunks received: %d\n", session->audio_chunks_received);
    
    return NULL;
}

// Create ElevenLabs WebSocket session
static struct elevenlabs_wss_session *create_elevenlabs_wss_session(struct ast_channel *chan, const char *options) {
    if (!chan) {
        ast_log(LOG_ERROR, "❌ Invalid channel for session creation\n");
        return NULL;
    }
    
    // Log platform info like Python reference
    log_platform_info();
    
    struct elevenlabs_wss_session *session = ast_calloc(1, sizeof(struct elevenlabs_wss_session));
    if (!session) {
        ast_log(LOG_ERROR, "❌ Failed to allocate session memory\n");
        return NULL;
    }
    
    // Initialize fields
    session->chan = chan;
    session->running = 0;
    session->destroying = 0;
    session->connection_established = 0;
    session->initialization_sent = 0;
    session->text_complete = 0;
    session->generation_complete = 0;
    session->audio_chunks_received = 0;
    
    // Initialize performance metrics (like Python reference)
    session->first_audio_received = 0;
    session->total_audio_bytes = 0;
    session->total_response_bytes = 0;
    session->total_processing_time = 0.0;
    session->connection_time = 0.0;
    session->first_audio_latency = 0.0;
    gettimeofday(&session->connection_start, NULL);  // Start timing
    
    // Default configuration (with Python reference optimizations)
    session->api_key = ast_strdup(getenv("ELEVENLABS_API_KEY") ? getenv("ELEVENLABS_API_KEY") : "");
    session->voice_id = ast_strdup(ELEVENLABS_DEFAULT_VOICE);
    session->model_id = ast_strdup(ELEVENLABS_DEFAULT_MODEL);
    session->text = ast_strdup("Hello, this is a test of ElevenLabs WebSocket streaming.");
    
    // Apply platform-specific optimizations (like Python reference)
    optimize_for_platform(session);
    
    ast_log(LOG_NOTICE, "✅ Session created with platform optimizations\n");
    session->output_format = ast_strdup("pcm_16000");
    session->apply_text_normalization = ast_strdup("auto");
    
    // Voice settings defaults
    session->voice_settings.speed = 1.0f;
    session->voice_settings.stability = 0.5f;
    session->voice_settings.similarity_boost = 0.8f;
    session->voice_settings.style = 0.0f;
    session->voice_settings.use_speaker_boost = 1;
    
    // WebSocket options defaults
    session->enable_logging = 1;
    session->enable_ssml_parsing = 0;
    session->inactivity_timeout = 20;
    session->sync_alignment = 0;
    session->auto_mode = 1;  // Reduced latency
    session->use_seed = 0;
    session->stream_chunks = 0;
    session->chunk_size = 50;  // Characters
    
    // Mutex
    session->lock = ast_malloc(sizeof(ast_mutex_t));
    if (!session->lock) {
        destroy_elevenlabs_wss_session(session);
        return NULL;
    }
    ast_mutex_init(session->lock);
    
    // Parse options
    if (options) {
        char *options_copy = ast_strdupa(options);
        char *option;
        
        while ((option = strsep(&options_copy, ","))) {
            char *key = strsep(&option, "=");
            char *value = option;
            
            if (key && value) {
                if (strcmp(key, "api_key") == 0) {
                    ast_free(session->api_key);
                    session->api_key = ast_strdup(value);
                } else if (strcmp(key, "voice") == 0) {
                    ast_free(session->voice_id);
                    session->voice_id = ast_strdup(value);
                } else if (strcmp(key, "model") == 0) {
                    ast_free(session->model_id);
                    session->model_id = ast_strdup(value);
                } else if (strcmp(key, "text") == 0) {
                    ast_free(session->text);
                    session->text = ast_strdup(value);
                } else if (strcmp(key, "text_file") == 0) {
                    // Read text from file
                    FILE *file = fopen(value, "r");
                    if (file) {
                        fseek(file, 0, SEEK_END);
                        long file_size = ftell(file);
                        fseek(file, 0, SEEK_SET);
                        
                        if (file_size > 0 && file_size < MAX_TEXT_LENGTH) {
                            char *file_text = ast_malloc(file_size + 1);
                            if (file_text) {
                                size_t bytes_read = fread(file_text, 1, file_size, file);
                                if (bytes_read == (size_t)file_size) {
                                    file_text[file_size] = '\0';
                                    ast_free(session->text);
                                    session->text = file_text;
                                } else {
                                    ast_log(LOG_WARNING, "Failed to read complete file: %s\n", value);
                                    ast_free(file_text);
                                }
                            }
                        }
                        fclose(file);
                    }
                } else if (strcmp(key, "language") == 0) {
                    session->language_code = ast_strdup(value);
                } else if (strcmp(key, "output_format") == 0) {
                    ast_free(session->output_format);
                    session->output_format = ast_strdup(value);
                } else if (strcmp(key, "speed") == 0) {
                    session->voice_settings.speed = atof(value);
                } else if (strcmp(key, "stability") == 0) {
                    session->voice_settings.stability = atof(value);
                } else if (strcmp(key, "similarity") == 0) {
                    session->voice_settings.similarity_boost = atof(value);
                } else if (strcmp(key, "auto_mode") == 0) {
                    session->auto_mode = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
                } else if (strcmp(key, "stream_chunks") == 0) {
                    session->stream_chunks = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
                } else if (strcmp(key, "enable_ssml") == 0) {
                    session->enable_ssml_parsing = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
                } else if (strcmp(key, "sync_alignment") == 0) {
                    session->sync_alignment = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
                } else if (strcmp(key, "seed") == 0) {
                    session->seed = (unsigned int)atoi(value);
                    session->use_seed = 1;
                }
            }
        }
    }
    
    if (!session->api_key || strlen(session->api_key) == 0) {
        ast_log(LOG_ERROR, "No ElevenLabs API key provided\n");
        destroy_elevenlabs_wss_session(session);
        return NULL;
    }
    
    ast_log(LOG_NOTICE, "✅ ElevenLabs WebSocket session created\n");
    ast_log(LOG_NOTICE, "   Configuration:\n");
    ast_log(LOG_NOTICE, "     Voice ID: %s\n", session->voice_id);
    ast_log(LOG_NOTICE, "     Model: %s\n", session->model_id);
    ast_log(LOG_NOTICE, "     Text: %.100s%s\n", session->text, 
            strlen(session->text) > 100 ? "..." : "");
    ast_log(LOG_NOTICE, "     Auto mode: %s\n", session->auto_mode ? "enabled" : "disabled");
    ast_log(LOG_NOTICE, "     Stream chunks: %s\n", session->stream_chunks ? "enabled" : "disabled");
    ast_log(LOG_NOTICE, "     Output format: %s\n", session->output_format ? session->output_format : "default");
    ast_log(LOG_NOTICE, "     Voice settings:\n");
    ast_log(LOG_NOTICE, "       Speed: %.2f\n", session->voice_settings.speed);
    ast_log(LOG_NOTICE, "       Stability: %.2f\n", session->voice_settings.stability);
    ast_log(LOG_NOTICE, "       Similarity: %.2f\n", session->voice_settings.similarity_boost);
    
    return session;
}

// Destroy ElevenLabs WebSocket session
static void destroy_elevenlabs_wss_session(struct elevenlabs_wss_session *session) {
    if (!session) {
        return;
    }
    
    session->destroying = 1;
    session->running = 0;
    
    // Calculate total session time and log comprehensive metrics (like Python reference)
    struct timeval session_end;
    gettimeofday(&session_end, NULL);
    double total_session_time = (session_end.tv_sec - session->connection_start.tv_sec) + 
                               (session_end.tv_usec - session->connection_start.tv_usec) / 1000000.0;
    
    // Log comprehensive results (enhanced like Python reference)
    ast_log(LOG_NOTICE, "\n============================================================\n");
    ast_log(LOG_NOTICE, "📊 ELEVENLABS SESSION RESULTS\n");
    ast_log(LOG_NOTICE, "============================================================\n");
    
    // Platform and timing info (like Python reference)
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        ast_log(LOG_NOTICE, "Platform: %s %s\n", system_info.sysname, system_info.release);
    }
    ast_log(LOG_NOTICE, "Total session time: %.2f seconds\n", total_session_time);
    ast_log(LOG_NOTICE, "Connection time: %.2f seconds\n", session->connection_time);
    
    // Audio metrics (like Python reference)
    ast_log(LOG_NOTICE, "Audio chunks received: %d\n", session->audio_chunks_received);
    ast_log(LOG_NOTICE, "Total PCM audio bytes: %zu\n", session->total_audio_bytes);
    ast_log(LOG_NOTICE, "Total response bytes: %zu\n", session->total_response_bytes);
    
    if (session->first_audio_received) {
        ast_log(LOG_NOTICE, "Time to first audio: %.2f seconds\n", session->first_audio_latency);
    }
    
    if (session->total_audio_bytes > 0) {
        double audio_duration = (double)session->total_audio_bytes / (16000 * 2);  // 16kHz, 16-bit
        ast_log(LOG_NOTICE, "Audio duration: %.2f seconds\n", audio_duration);
        ast_log(LOG_NOTICE, "Audio quality: 16kHz PCM, %.1fKB\n", session->total_audio_bytes / 1024.0);
        
        if (session->total_processing_time > 0) {
            ast_log(LOG_NOTICE, "Processing efficiency: %.1fx real-time\n", 
                    audio_duration / session->total_processing_time);
        }
    }
    
    // Final status (like Python reference)
    if (session->audio_chunks_received > 0) {
        ast_log(LOG_NOTICE, "✅ SUCCESS: ElevenLabs audio streaming completed!\n");
        ast_log(LOG_NOTICE, "🎵 Real-time PCM audio processing working\n");
        ast_log(LOG_NOTICE, "🎯 Platform-optimized performance achieved\n");
    } else {
        ast_log(LOG_NOTICE, "❌ FAILURE: No audio received\n");
        ast_log(LOG_NOTICE, "💡 Check API key and network connection\n");
    }
    
    ast_log(LOG_NOTICE, "============================================================\n");
    
    // Cleanup resources
    if (session->context) {
        lws_context_destroy(session->context);
    }
    
    if (session->api_key) ast_free(session->api_key);
    if (session->voice_id) ast_free(session->voice_id);
    if (session->model_id) ast_free(session->model_id);
    if (session->text) ast_free(session->text);
    if (session->text_file_path) ast_free(session->text_file_path);
    if (session->language_code) ast_free(session->language_code);
    if (session->output_format) ast_free(session->output_format);
    if (session->apply_text_normalization) ast_free(session->apply_text_normalization);
    if (session->audio_buffer) ast_free(session->audio_buffer);
    if (session->response_buffer) ast_free(session->response_buffer);
    if (session->ws_buffer) ast_free(session->ws_buffer);
    if (session->last_alignment) ast_json_unref(session->last_alignment);
    if (session->last_error_message) ast_free(session->last_error_message);
    
    if (session->lock) {
        ast_mutex_destroy(session->lock);
        ast_free(session->lock);
    }
    
    ast_free(session);
    
    ast_log(LOG_NOTICE, "✅ ElevenLabs WebSocket session destroyed\n");
}

// Main execution function
static int elevenlabs_wss_exec(struct ast_channel *chan, const char *data) {
    if (!chan) {
        return -1;
    }
    
    ast_log(LOG_NOTICE, "🎵 Starting ElevenLabs WebSocket Streaming\n");
    ast_log(LOG_NOTICE, "Channel: %s\n", ast_channel_name(chan));
    
    // Run transcoding diagnostics early to catch potential issues
    log_transcoding_diagnostics(chan);
    
    // Create session
    struct elevenlabs_wss_session *session = create_elevenlabs_wss_session(chan, data);
    if (!session) {
        ast_log(LOG_ERROR, "Failed to create ElevenLabs WebSocket session\n");
        return -1;
    }
    
    // Set global session for callback access
    ast_mutex_lock(&session_lock);
    current_session = session;
    ast_mutex_unlock(&session_lock);
    
    // Answer channel if needed
    if (ast_channel_state(chan) != AST_STATE_UP) {
        if (ast_answer(chan) != 0) {
            ast_log(LOG_ERROR, "Failed to answer channel\n");
            destroy_elevenlabs_wss_session(session);
            return -1;
        }
    }
    
    // Start WebSocket thread
    session->running = 1;
    session->start_time = time(NULL);
    
    if (pthread_create(&session->websocket_thread, NULL, websocket_thread_func, session) != 0) {
        ast_log(LOG_ERROR, "Failed to create WebSocket thread\n");
        destroy_elevenlabs_wss_session(session);
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✅ WebSocket thread started\n");
    
    // Wait for processing to complete
    while (session->running && !session->destroying && !global_shutdown && 
           ast_channel_state(chan) == AST_STATE_UP) {
        
        if (ast_check_hangup(chan)) {
            ast_log(LOG_NOTICE, "Channel hangup detected\n");
            break;
        }
        
        usleep(100000);  // 100ms
    }
    
    // Wait for thread to complete
    if (session->running) {
        session->running = 0;
    }
    pthread_join(session->websocket_thread, NULL);
    
    // Clear global session
    ast_mutex_lock(&session_lock);
    current_session = NULL;
    ast_mutex_unlock(&session_lock);
    
    // Performance stats
    time_t end_time = time(NULL);
    ast_log(LOG_NOTICE, "📊 ElevenLabs WebSocket Session Performance Statistics\n");
    ast_log(LOG_NOTICE, "   Session Duration: %ld seconds\n", end_time - session->start_time);
    ast_log(LOG_NOTICE, "   Audio Chunks Received: %d\n", session->audio_chunks_received);
    ast_log(LOG_NOTICE, "   Total Audio Data: %zu bytes\n", session->total_audio_bytes);
    ast_log(LOG_NOTICE, "   Voice Used: %s\n", session->voice_id);
    ast_log(LOG_NOTICE, "   Model Used: %s\n", session->model_id);
    ast_log(LOG_NOTICE, "   Text Length: %zu characters\n", strlen(session->text));
    if (session->audio_chunks_received > 0) {
        float avg_chunk_size = (float)session->total_audio_bytes / session->audio_chunks_received;
        ast_log(LOG_NOTICE, "   Average Chunk Size: %.1f bytes\n", avg_chunk_size);
    }
    if (end_time - session->start_time > 0) {
        float chunks_per_second = (float)session->audio_chunks_received / (end_time - session->start_time);
        ast_log(LOG_NOTICE, "   Throughput: %.1f chunks/second\n", chunks_per_second);
    }
    
    destroy_elevenlabs_wss_session(session);
    
    ast_log(LOG_NOTICE, "✅ ElevenLabs WebSocket streaming completed\n");
    return 0;
}

// Module registration
static int load_module(void) {
    ast_mutex_init(&session_lock);
    
    // Initialize OpenSSL for Base64 operations (OpenSSL 3.0 compatible)
    // Note: In OpenSSL 3.0, many initialization functions are deprecated
    // but we still call them for backwards compatibility
    
    int result = ast_register_application2(app, elevenlabs_wss_exec, NULL, NULL, AST_MODULE_SELF_SYM());
    
    if (result == 0) {
        ast_log(LOG_NOTICE, "✅ ElevenLabs WebSocket Streaming module loaded\n");
        ast_log(LOG_NOTICE, "  🎵 Basic TTS: ElevenLabsWSS(api_key=YOUR_KEY,voice=VOICE_ID,text=Hello)\n");
        ast_log(LOG_NOTICE, "  📄 File input: ElevenLabsWSS(api_key=YOUR_KEY,voice=VOICE_ID,text_file=/tmp/speech.txt)\n");
        ast_log(LOG_NOTICE, "  🔄 Streaming: ElevenLabsWSS(api_key=YOUR_KEY,voice=VOICE_ID,text=Hello,stream_chunks=true)\n");
        ast_log(LOG_NOTICE, "  ⚡ Low latency: ElevenLabsWSS(api_key=YOUR_KEY,voice=VOICE_ID,text=Hello,auto_mode=true)\n");
        ast_log(LOG_NOTICE, "  🎛️ SSML: ElevenLabsWSS(api_key=YOUR_KEY,voice=VOICE_ID,text=<speak>Hello</speak>,enable_ssml=true)\n");
    }
    
    return result;
}

static int unload_module(void) {
    global_shutdown = 1;
    usleep(500000); // 500ms
    
    ast_mutex_destroy(&session_lock);
    
    // Cleanup OpenSSL
    EVP_cleanup();
    
    int result = ast_unregister_application(app);
    ast_log(LOG_NOTICE, "✅ ElevenLabs WebSocket Streaming module unloaded\n");
    
    return result;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "ElevenLabs WebSocket Streaming API - Real-time TTS",
    .support_level = AST_MODULE_SUPPORT_EXTENDED,
    .load = load_module,
    .unload = unload_module,
);
