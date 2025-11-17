/*
 * Asterisk -- An open source telephony toolkit.
 * 
 * OpenAI Realtime API Application - OPTIMIZED VERSION
 * Based on Python test performance improvements
 * 
 * Key optimizations:
 * - 16kHz audio (not 24kHz) - matches Python test
 * - Simple fast upsampling (8kHz->16kHz)
 * - 320-sample frames (20ms at 16kHz)
 * - Minimal buffering and latenc    ast_log(LOG_NOTICE, "Sending session config message (%zu bytes):\n%s\n", json_len, config_json);
    
    int result = lws_write(session->wsi, buf + LWS_PRE, json_len, LWS_WRITE_TEXT);
    ast_free(buf);
    
    if (result < 0) {
        ast_log(LOG_ERROR, "Failed to send session config\n");
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✓ Session config sent successfully (%d bytes)\n", result);timized session configuration
 */

/*** MODULEINFO
    <depend>websockets</depend>
    <support_level>extended</support_level>
 ***/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Enable AST JSON support since we're including asterisk/json.h */
#define WITH_AST_JSON 1

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
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"

#include <libwebsockets.h>
#include <pthread.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>  // For gettimeofday
#include <time.h>
#include <stdbool.h>
#include <unistd.h>  // For usleep

// OPTIMIZED CONSTANTS - Based on Python test performance
#define OPENAI_SAMPLE_RATE_16K 16000        // 16kHz for OpenAI (not 24kHz)
#define ASTERISK_SAMPLE_RATE_8K 8000        // Asterisk 8kHz μ-law
#define UPSAMPLING_RATIO 2                  // 8kHz -> 16kHz = 2x

// OPTIMIZED FRAME SIZES - 20ms at 16kHz (matches Python test)
#define OPENAI_FRAME_SAMPLES_16K 320        // 20ms at 16kHz = 320 samples
#define OPENAI_FRAME_BYTES_16K 640          // 320 samples * 2 bytes = 640 bytes
#define ASTERISK_FRAME_SAMPLES_8K 160       // 20ms at 8kHz = 160 samples

// MINIMAL BUFFERING - Send frames immediately
#define AUDIO_BUFFER_SIZE (OPENAI_FRAME_SAMPLES_16K * 4)  // Buffer 4 frames max
#define MAX_WEBSOCKET_FRAME_SIZE 65536

// libwebsockets constants
#define CONTEXT_PORT_NO_LISTEN -1

// Session structure - simplified and optimized
struct openai_session_optimized {
    // Essential fields only
    struct ast_channel *chan;
    struct lws *wsi;
    int running;
    int destroying;
    
    // Session state
    int session_configured;
    char session_id[64];
    
    // Audio buffer - minimal size
    int16_t *audio_buffer;
    size_t buffer_pos;
    size_t buffer_size;
    
    // Configuration
    char *api_key;
    char *voice;
    char *instructions;
    char *language;
    int send_welcome;  // Whether to send welcome request automatically
    
    // Threading
    ast_mutex_t *lock;
    pthread_t websocket_thread;
    
    // Fragment buffer for WebSocket
    char *fragment_buffer;
    size_t fragment_buffer_size;
    size_t fragment_buffer_used;
    
    // Performance metrics
    time_t last_audio_send_time;
    int frames_sent;
    
    // Speech detection state
    int speech_detected;
    time_t last_speech_time;
    int silence_frames;
    int consecutive_quiet_frames;
    
    // Audio response buffer for accumulating deltas
    char *response_audio_buffer;
    size_t response_buffer_size;
    size_t response_buffer_used;
    int response_in_progress;
    
    // Audio playback control
    int playback_active;
    int stop_playback;  // Flag to interrupt ongoing playback
};

// Global variables
static const char *app = "OpenAIRealtimeOptimized";
static volatile int global_shutdown = 0;
static struct openai_session_optimized *current_session = NULL;
static ast_mutex_t session_lock;

// Function declarations
static int openai_realtime_callback_optimized(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
static int openai_send_session_config_optimized(struct openai_session_optimized *session);
static int openai_send_audio_chunk_optimized(struct openai_session_optimized *session, const int16_t *audio_data, size_t samples);
static int openai_handle_message_optimized(struct openai_session_optimized *session, const char *message, size_t len);
static void *websocket_thread_optimized(void *data);
static struct openai_session_optimized *create_openai_session_optimized(struct ast_channel *chan, const char *options);
static void destroy_openai_session_optimized(struct openai_session_optimized *session);
static int process_audio_frame_optimized(struct openai_session_optimized *session, struct ast_frame *frame);
static int play_openai_audio_response(struct openai_session_optimized *session, const char *base64_audio);
static int accumulate_audio_delta(struct openai_session_optimized *session, const char *base64_delta);
static int flush_accumulated_audio(struct openai_session_optimized *session);
static int openai_send_welcome_request(struct openai_session_optimized *session);
static void stop_audio_playback(struct openai_session_optimized *session);
static int openai_send_welcome_request(struct openai_session_optimized *session);

// OPTIMIZED UPSAMPLING - Simple and fast (8kHz -> 16kHz)
static size_t upsample_8khz_to_16khz_fast(const int16_t *input, size_t input_samples, int16_t *output, size_t output_max_samples) {
    if (!input || !output || input_samples == 0) {
        return 0;
    }
    
    size_t output_samples = input_samples * 2;
    if (output_samples > output_max_samples) {
        output_samples = output_max_samples;
    }
    
    // METHOD 1: Simple duplication (fastest)
    // This matches what PyAudio does internally for simple upsampling
    for (size_t i = 0; i < input_samples && (i * 2 + 1) < output_samples; i++) {
        output[i * 2] = input[i];       // Original sample
        output[i * 2 + 1] = input[i];   // Duplicate (simple but effective)
    }
    
    return output_samples;
}

// DOWNSAMPLING - 24kHz -> 8kHz (for OpenAI audio responses)
// Better quality downsampling with anti-aliasing filter
static size_t downsample_24khz_to_8khz_filtered(const int16_t *input, size_t input_samples, int16_t *output, size_t output_max_samples) {
    if (!input || !output || input_samples == 0) {
        return 0;
    }
    
    size_t output_samples = input_samples / 3;
    if (output_samples > output_max_samples) {
        output_samples = output_max_samples;
    }
    
    // Simple 3-point average filter before decimation (basic anti-aliasing)
    for (size_t i = 0; i < output_samples; i++) {
        size_t base_idx = i * 3;
        
        if (base_idx + 2 < input_samples) {
            // Average 3 samples to reduce aliasing
            int32_t sum = (int32_t)input[base_idx] + 
                         (int32_t)input[base_idx + 1] + 
                         (int32_t)input[base_idx + 2];
            output[i] = (int16_t)(sum / 3);
        } else {
            // Near end of buffer, just take the available sample
            output[i] = input[base_idx];
        }
    }
    
    ast_log(LOG_NOTICE, "Filtered downsampled %zu -> %zu samples (24kHz -> 8kHz)\n", input_samples, output_samples);
    return output_samples;
}

// Base64 encoding - simple implementation
static char *encode_audio_to_base64_simple(const char *data, size_t len) {
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t output_len = 4 * ((len + 2) / 3);
    char *output = ast_malloc(output_len + 1);
    if (!output) return NULL;
    
    size_t i, j;
    for (i = 0, j = 0; i < len;) {
        uint32_t a = i < len ? (unsigned char)data[i++] : 0;
        uint32_t b = i < len ? (unsigned char)data[i++] : 0;
        uint32_t c = i < len ? (unsigned char)data[i++] : 0;
        
        uint32_t triple = (a << 16) + (b << 8) + c;
        
        output[j++] = base64_chars[(triple >> 18) & 63];
        output[j++] = base64_chars[(triple >> 12) & 63];
        output[j++] = base64_chars[(triple >> 6) & 63];
        output[j++] = base64_chars[triple & 63];
    }
    
    // Add padding
    for (i = 0; i < (3 - len % 3) % 3; i++) {
        output[output_len - 1 - i] = '=';
    }
    
    output[output_len] = '\0';
    return output;
}

// Base64 decoding - for OpenAI audio responses
static char *decode_base64_audio(const char *input, size_t input_len, size_t *output_len) {
    if (!input || input_len == 0 || !output_len) {
        return NULL;
    }
    
    // Base64 decode table
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
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
    };
    
    // Calculate output length
    size_t padding = 0;
    if (input_len > 0 && input[input_len - 1] == '=') padding++;
    if (input_len > 1 && input[input_len - 2] == '=') padding++;
    
    *output_len = (input_len * 3) / 4 - padding;
    
    char *output = ast_malloc(*output_len);
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
        
        if (out_pos < *output_len) output[out_pos++] = (char)(triple >> 16);
        if (c >= 0 && out_pos < *output_len) output[out_pos++] = (char)(triple >> 8);
        if (d >= 0 && out_pos < *output_len) output[out_pos++] = (char)triple;
    }
    
    *output_len = out_pos;
    ast_log(LOG_NOTICE, "Decoded base64 audio: %zu bytes -> %zu bytes\n", input_len, *output_len);
    
    return output;
}

// WebSocket protocols
static const struct lws_protocols protocols_optimized[] = {
    {
        "",
        openai_realtime_callback_optimized,
        0,
        MAX_WEBSOCKET_FRAME_SIZE,
        0, NULL, 0
    },
    LWS_PROTOCOL_LIST_TERM
};

// OPTIMIZED WebSocket callback
static int openai_realtime_callback_optimized(struct lws *wsi, enum lws_callback_reasons reason,
                                            void *user __attribute__((unused)), void *in, size_t len) {
    struct openai_session_optimized *session = (struct openai_session_optimized *)lws_wsi_user(wsi);
    
    if (global_shutdown) {
        return 0;
    }
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
            if (!session || !session->api_key) {
                ast_log(LOG_ERROR, "No API key for handshake\n");
                return -1;
            }
            
            unsigned char **p = (unsigned char **)in;
            unsigned char *end = (*p) + len;
            
            char auth_header[512];
            snprintf(auth_header, sizeof(auth_header), "Bearer %s", session->api_key);
            
            if (lws_add_http_header_by_name(wsi, (const unsigned char *)"Authorization",
                                          (const unsigned char *)auth_header, strlen(auth_header), p, end) < 0) {
                return -1;
            }
            
            if (lws_add_http_header_by_name(wsi, (const unsigned char *)"OpenAI-Beta",
                                          (const unsigned char *)"realtime=v1", 11, p, end) < 0) {
                return -1;
            }
            
            ast_log(LOG_NOTICE, "Headers added to handshake\n");
            break;
        }
        
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            if (session) {
                session->running = 1;
                session->wsi = wsi;
                ast_log(LOG_NOTICE, "✓ WebSocket connection established\n");
            }
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            if (!session || !in || len == 0) {
                break;
            }
            
            ast_log(LOG_NOTICE, "Received message: %zu bytes\n", len);
            
            // Log the full message content (truncated if too long)
            char log_buffer[1024];
            size_t log_len = len < sizeof(log_buffer) - 1 ? len : sizeof(log_buffer) - 1;
            memcpy(log_buffer, in, log_len);
            log_buffer[log_len] = '\0';
            
            ast_log(LOG_NOTICE, "Message content: %s%s\n", log_buffer, len > log_len ? "...(truncated)" : "");
            
            openai_handle_message_optimized(session, (char *)in, len);
            break;
        }
        
        case LWS_CALLBACK_CLIENT_CLOSED:
            ast_log(LOG_WARNING, "WebSocket connection closed\n");
            if (session) {
                session->running = 0;
                session->wsi = NULL;
            }
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            ast_log(LOG_ERROR, "WebSocket connection error: %s\n", in ? (char *)in : "Unknown");
            if (session) {
                session->running = 0;
                session->wsi = NULL;
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

// OPTIMIZED session configuration - matches Python test
static int openai_send_session_config_optimized(struct openai_session_optimized *session) {
    if (!session || !session->wsi) {
        return -1;
    }
    
    ast_log(LOG_NOTICE, "Sending optimized session config (16kHz)\n");
    
    // Create minimal, optimized configuration (matches Python test)
    char config_json[2048];
    snprintf(config_json, sizeof(config_json),
        "{"
        "\"type\":\"session.update\","
        "\"session\":{"
            "\"modalities\":[\"text\",\"audio\"],"
            "\"instructions\":\"%s\","
            "\"voice\":\"%s\","
            "\"input_audio_format\":\"pcm16\","
            "\"output_audio_format\":\"pcm16\","
            "\"input_audio_transcription\":{\"model\":\"whisper-1\",\"language\":\"%s\"},"
            "\"turn_detection\":{"
                "\"type\":\"server_vad\","
                "\"threshold\":0.3,"
                "\"prefix_padding_ms\":200,"
                "\"silence_duration_ms\":400"
            "},"
            "\"temperature\":0.7"
        "}"
        "}",
        session->instructions ? session->instructions : "You are testing 16kHz audio. Respond briefly.",
        session->voice ? session->voice : "alloy",
        session->language ? session->language : "en"
    );
    
    // Log the full message being sent
    ast_log(LOG_NOTICE, "Sending session config message: %zu bytes\n", strlen(config_json));
    ast_log(LOG_NOTICE, "Session config JSON: %s\n", config_json);
    
    // Send via WebSocket
    size_t json_len = strlen(config_json);
    unsigned char *buf = ast_malloc(LWS_PRE + json_len);
    if (!buf) {
        return -1;
    }
    
    memcpy(buf + LWS_PRE, config_json, json_len);
    
    int result = lws_write(session->wsi, buf + LWS_PRE, json_len, LWS_WRITE_TEXT);
    ast_free(buf);
    
    if (result < 0) {
        ast_log(LOG_ERROR, "Failed to send session config\n");
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✓ Session config sent successfully (%d bytes)\n", result);
    return 0;
}

// OPTIMIZED audio sending - immediate transmission like Python test
static int openai_send_audio_chunk_optimized(struct openai_session_optimized *session, const int16_t *audio_data, size_t samples) {
    if (!session || !session->wsi || !audio_data || samples == 0) {
        return -1;
    }
    
    // Convert to bytes
    size_t bytes = samples * sizeof(int16_t);
    
    // Encode to base64
    char *base64_audio = encode_audio_to_base64_simple((char *)audio_data, bytes);
    if (!base64_audio) {
        ast_log(LOG_ERROR, "Base64 encoding failed\n");
        return -1;
    }
    
    // Create JSON message
    char *json_msg = ast_malloc(strlen(base64_audio) + 256);
    if (!json_msg) {
        ast_free(base64_audio);
        return -1;
    }
    
    sprintf(json_msg, "{\"type\":\"input_audio_buffer.append\",\"audio\":\"%s\"}", base64_audio);
    
    // Send immediately (no buffering)
    size_t json_len = strlen(json_msg);
    
    ast_log(LOG_NOTICE, "Sending audio append message (%zu bytes JSON, %zu bytes audio, %zu samples 16kHz)\n", 
            json_len, bytes, samples);
    
    unsigned char *buf = ast_malloc(LWS_PRE + json_len);
    if (!buf) {
        ast_free(base64_audio);
        ast_free(json_msg);
        return -1;
    }
    
    memcpy(buf + LWS_PRE, json_msg, json_len);
    
    int result = lws_write(session->wsi, buf + LWS_PRE, json_len, LWS_WRITE_TEXT);
    
    // Cleanup
    ast_free(buf);
    ast_free(json_msg);
    ast_free(base64_audio);
    
    if (result < 0) {
        ast_log(LOG_ERROR, "Failed to send audio chunk\n");
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✓ Audio chunk sent successfully (%d bytes)\n", result);
    
    // Update metrics
    session->frames_sent++;
    session->last_audio_send_time = time(NULL);
    
    ast_log(LOG_NOTICE, "✓ Audio chunk sent: %zu samples (%zu bytes), frame #%d\n", 
            samples, bytes, session->frames_sent);
    
    return 0;
}

// Send welcome request to OpenAI to start conversation
static int openai_send_welcome_request(struct openai_session_optimized *session) {
    if (!session || !session->wsi) {
        return -1;
    }
    
    ast_log(LOG_NOTICE, "Sending welcome request to OpenAI\n");
    
    // Declare variables
    size_t json_len;
    unsigned char *buf;
    int result;
    
    // Now send response.create to trigger OpenAI to respond
    char response_create_json[1024];
    snprintf(response_create_json, sizeof(response_create_json),
        "{"
        "\"type\":\"response.create\","
        "\"response\":{"
            "\"modalities\":[\"text\",\"audio\"],"
            "\"instructions\":\"%s\""
        "}"
        "}",
        session->instructions ? session->instructions : "Please respond with audio. Keep your response brief and friendly.");
    
    json_len = strlen(response_create_json);
    buf = ast_malloc(LWS_PRE + json_len);
    if (!buf) {
        return -1;
    }
    
    memcpy(buf + LWS_PRE, response_create_json, json_len);
    
    ast_log(LOG_NOTICE, "Response create JSON: %s\n", response_create_json);
    
    result = lws_write(session->wsi, buf + LWS_PRE, json_len, LWS_WRITE_TEXT);
    ast_free(buf);
    
    if (result < 0) {
        ast_log(LOG_ERROR, "Failed to send response create request\n");
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✓ Response create request sent successfully (%d bytes)\n", result);
    return 0;
}

// Audio playbook function for OpenAI responses (handles accumulated base64 audio)
static int play_openai_audio_response(struct openai_session_optimized *session, const char *base64_audio) {
    if (!session || !session->chan || !base64_audio) {
        ast_log(LOG_ERROR, "❌ Invalid parameters for audio playback\n");
        return -1;
    }
    
    size_t input_len = strlen(base64_audio);
    if (input_len == 0) {
        ast_log(LOG_NOTICE, "⚠️ Empty audio response, skipping playback\n");
        return 0;
    }
    
    ast_log(LOG_NOTICE, "🔊 STARTING audio playback process (%zu base64 chars)\n", input_len);
    ast_log(LOG_NOTICE, "📊 Channel info: name=%s, state=%s\n", 
            ast_channel_name(session->chan), 
            ast_state2str(ast_channel_state(session->chan)));
    
    // Check if playback should be stopped due to user speech
    if (session->stop_playback || session->speech_detected) {
        ast_log(LOG_NOTICE, "🛑 Playback cancelled - user is speaking\n");
        return 0;
    }
    
    // Mark playback as active
    session->playback_active = 1;
    
    // Log first 100 chars of base64 for debugging
    char base64_preview[101];
    size_t preview_len = input_len < 100 ? input_len : 100;
    strncpy(base64_preview, base64_audio, preview_len);
    base64_preview[preview_len] = '\0';
    ast_log(LOG_NOTICE, "🔍 Base64 preview: %s%s\n", base64_preview, input_len > 100 ? "..." : "");
    
    // Decode base64 audio
    size_t decoded_len = 0;
    char *decoded_audio = decode_base64_audio(base64_audio, strlen(base64_audio), &decoded_len);
    if (!decoded_audio || decoded_len == 0) {
        ast_log(LOG_ERROR, "❌ Failed to decode base64 audio (len=%zu)\n", decoded_len);
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✅ Base64 decoded successfully: %zu bytes\n", decoded_len);
    
    // Convert bytes to samples (16-bit PCM)
    size_t decoded_samples = decoded_len / 2;
    int16_t *audio_samples = (int16_t *)decoded_audio;
    
    ast_log(LOG_NOTICE, "📊 Audio format: %zu bytes, %zu samples (assumed 24kHz PCM16)\n", 
            decoded_len, decoded_samples);
    
    // Calculate audio energy to verify it's not silent
    long long energy_sum = 0;
    int16_t max_sample = 0;
    double rms_energy = 0.0;
    
    // Log first few samples for debugging
    if (decoded_samples > 0) {
        ast_log(LOG_NOTICE, "🎵 First audio samples: %d, %d, %d, %d\n", 
                decoded_samples > 0 ? audio_samples[0] : 0,
                decoded_samples > 1 ? audio_samples[1] : 0,
                decoded_samples > 2 ? audio_samples[2] : 0,
                decoded_samples > 3 ? audio_samples[3] : 0);
        
        for (size_t i = 0; i < decoded_samples && i < 1000; i++) {
            energy_sum += (long long)audio_samples[i] * audio_samples[i];
            if (abs(audio_samples[i]) > max_sample) {
                max_sample = abs(audio_samples[i]);
            }
        }
        rms_energy = sqrt((double)energy_sum / (decoded_samples < 1000 ? decoded_samples : 1000));
        ast_log(LOG_NOTICE, "🔊 Audio energy: RMS=%.1f, Peak=%d\n", rms_energy, max_sample);
        
        if (max_sample < 100) {
            ast_log(LOG_WARNING, "⚠️ Audio seems very quiet (peak=%d)\n", max_sample);
        }
    }
    
    // Downsample from 24kHz (OpenAI) to 8kHz (Asterisk)
    size_t downsampled_max = decoded_samples / 3 + 100; // Extra buffer
    int16_t *downsampled_audio = ast_malloc(downsampled_max * sizeof(int16_t));
    if (!downsampled_audio) {
        ast_log(LOG_ERROR, "❌ Failed to allocate downsampling buffer (%zu samples)\n", downsampled_max);
        ast_free(decoded_audio);
        return -1;
    }
    
    ast_log(LOG_NOTICE, "🔄 Starting downsampling: %zu -> %zu samples (24kHz -> 8kHz)\n", 
            decoded_samples, downsampled_max);
    
    // Use filtered downsampling for better quality
    size_t downsampled_samples = downsample_24khz_to_8khz_filtered(audio_samples, decoded_samples, 
                                                                  downsampled_audio, downsampled_max);
    
    ast_log(LOG_NOTICE, "✅ Downsampling complete: %zu samples at 8kHz\n", downsampled_samples);
    
    if (downsampled_samples == 0) {
        ast_log(LOG_ERROR, "❌ Downsampling produced zero samples!\n");
        ast_free(downsampled_audio);
        ast_free(decoded_audio);
        return -1;
    }
    
    // Log first few downsampled samples
    if (downsampled_samples > 0) {
        ast_log(LOG_NOTICE, "🎵 Downsampled samples: %d, %d, %d, %d\n", 
                downsampled_samples > 0 ? downsampled_audio[0] : 0,
                downsampled_samples > 1 ? downsampled_audio[1] : 0,
                downsampled_samples > 2 ? downsampled_audio[2] : 0,
                downsampled_samples > 3 ? downsampled_audio[3] : 0);
    }
    
    // Convert to μ-law format for Asterisk
    unsigned char *ulaw_data = ast_malloc(downsampled_samples);
    if (!ulaw_data) {
        ast_log(LOG_ERROR, "❌ Failed to allocate μ-law buffer (%zu bytes)\n", downsampled_samples);
        ast_free(downsampled_audio);
        ast_free(decoded_audio);
        return -1;
    }
    
    // AUTOMATIC GAIN CONTROL (AGC) - Normalize volume consistently
    // Calculate RMS for the downsampled audio
    long long ds_energy_sum = 0;
    int16_t ds_max_sample = 0;
    
    for (size_t i = 0; i < downsampled_samples; i++) {
        ds_energy_sum += (long long)downsampled_audio[i] * downsampled_audio[i];
        if (abs(downsampled_audio[i]) > ds_max_sample) {
            ds_max_sample = abs(downsampled_audio[i]);
        }
    }
    
    double ds_rms_energy = sqrt((double)ds_energy_sum / downsampled_samples);
    
    // Target levels for consistent volume
    const int16_t target_peak = 8000;    // Target peak level (about 25% of max)
    const double target_rms = 2000.0;    // Target RMS level
    
    // Calculate gain based on both peak and RMS
    float peak_gain = (ds_max_sample > 0) ? (float)target_peak / ds_max_sample : 1.0f;
    float rms_gain = (ds_rms_energy > 0) ? (float)target_rms / ds_rms_energy : 1.0f;
    
    // Use the smaller gain to avoid clipping, but ensure minimum volume
    float gain_factor = (peak_gain < rms_gain) ? peak_gain : rms_gain;
    
    // Apply minimum gain for very quiet audio
    if (gain_factor < 0.5f) gain_factor = 0.5f;
    
    // Cap maximum gain to prevent distortion
    if (gain_factor > 6.0f) gain_factor = 6.0f;
    
    ast_log(LOG_NOTICE, "🎚️ AGC: Peak=%d->%d, RMS=%.1f->%.1f, Gain=%.2fx\n", 
            ds_max_sample, (int)(ds_max_sample * gain_factor),
            ds_rms_energy, ds_rms_energy * gain_factor, gain_factor);
    
    // Apply gain normalization to all samples
    for (size_t i = 0; i < downsampled_samples; i++) {
        int32_t normalized = (int32_t)(downsampled_audio[i] * gain_factor);
        
        // Soft clipping to prevent harsh distortion
        if (normalized > 32767) {
            normalized = 32767;
        } else if (normalized < -32768) {
            normalized = -32768;
        }
        
        downsampled_audio[i] = (int16_t)normalized;
    }
    
    ast_log(LOG_NOTICE, "🔄 Converting normalized audio to μ-law format...\n");
    
    // Convert normalized linear PCM to μ-law
    for (size_t i = 0; i < downsampled_samples; i++) {
        ulaw_data[i] = AST_LIN2MU(downsampled_audio[i]);
    }
    
    ast_log(LOG_NOTICE, "✅ AGC and μ-law conversion complete\n");
    ast_log(LOG_NOTICE, "🎵 Normalized μ-law samples: %02x, %02x, %02x, %02x\n", 
            downsampled_samples > 0 ? ulaw_data[0] : 0,
            downsampled_samples > 1 ? ulaw_data[1] : 0,
            downsampled_samples > 2 ? ulaw_data[2] : 0,
            downsampled_samples > 3 ? ulaw_data[3] : 0);
    
    // Get channel format info
    struct ast_format *chan_format = ast_channel_readformat(session->chan);
    const char *format_name = chan_format ? ast_format_get_name(chan_format) : "unknown";
    ast_log(LOG_NOTICE, "📊 Channel read format: %s\n", format_name);
    
    // SEND AUDIO IN PROPER TIMING (160 samples = 20ms at 8kHz)
    const size_t chunk_size = 160;  // 20ms frames at 8kHz
    size_t total_chunks = (downsampled_samples + chunk_size - 1) / chunk_size;
    
    ast_log(LOG_NOTICE, "📤 Sending audio in %zu chunks of %zu samples each (proper timing)\n", 
            total_chunks, chunk_size);
    
    int total_result = 0;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    
    for (size_t chunk = 0; chunk < total_chunks; chunk++) {
        // Check if we should stop playback due to user speech
        if (session->stop_playback || session->speech_detected) {
            ast_log(LOG_NOTICE, "🛑 Playback interrupted at chunk %zu/%zu - user speaking\n", 
                    chunk + 1, total_chunks);
            total_result = 0;  // Consider it successful interruption
            break;
        }
        
        size_t chunk_start = chunk * chunk_size;
        size_t current_chunk_size = chunk_size;
        
        // Adjust last chunk size if needed
        if (chunk_start + current_chunk_size > downsampled_samples) {
            current_chunk_size = downsampled_samples - chunk_start;
        }
        
        // Calculate proper timestamp for this chunk (in samples at 8kHz)
        unsigned int timestamp = chunk * chunk_size;
        
        // Create frame for this chunk with proper timing
        struct ast_frame audio_frame = {
            .frametype = AST_FRAME_VOICE,
            .samples = current_chunk_size,
            .datalen = current_chunk_size,
            .data.ptr = ulaw_data + chunk_start,
            .offset = 0,  // Use 0 instead of AST_FRIENDLY_OFFSET
            .seqno = 0,
            .ts = timestamp  // Proper timestamp for timing
        };
        
        // Set format to μ-law
        audio_frame.subclass.format = chan_format;
        
        ast_log(LOG_NOTICE, "📤 Writing chunk %zu/%zu: %zu samples (ts=%u)\n", 
                chunk + 1, total_chunks, current_chunk_size, timestamp);
        
        // Write audio frame to channel
        int result = ast_write(session->chan, &audio_frame);
        
        if (result < 0) {
            ast_log(LOG_ERROR, "❌ Failed to write chunk %zu (result=%d)\n", chunk + 1, result);
            total_result = result;
            break;
        } else {
            ast_log(LOG_NOTICE, "✅ Chunk %zu written successfully\n", chunk + 1);
        }
        
        // PROPER TIMING: Wait exactly 20ms between chunks (real-time playback)
        // This ensures audio plays at correct speed
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        
        // Calculate expected time for this chunk (20ms per chunk)
        long expected_ms = (chunk + 1) * 20;  // 20ms per chunk
        
        // Calculate actual elapsed time
        long elapsed_ms = ((current_time.tv_sec - start_time.tv_sec) * 1000) +
                         ((current_time.tv_usec - start_time.tv_usec) / 1000);
        
        // Sleep if we're ahead of schedule
        if (elapsed_ms < expected_ms) {
            long sleep_ms = expected_ms - elapsed_ms;
            ast_log(LOG_NOTICE, "⏰ Timing: sleeping %ldms to maintain 20ms/chunk rate\n", sleep_ms);
            usleep(sleep_ms * 1000);  // Convert to microseconds
        }
    }
    
    ast_log(LOG_NOTICE, "📤 All chunks sent with result: %d\n", total_result);
    
    // Cleanup
    ast_free(ulaw_data);
    ast_free(downsampled_audio);
    ast_free(decoded_audio);
    
    // Mark playback as completed/stopped
    session->playback_active = 0;
    
    if (total_result < 0) {
        ast_log(LOG_ERROR, "❌ Failed to write audio frame to channel (result=%d)\n", total_result);
        return -1;
    }
    
    if (session->stop_playback || session->speech_detected) {
        ast_log(LOG_NOTICE, "✅ Audio playback interrupted successfully (user speaking)\n");
    } else {
        ast_log(LOG_NOTICE, "✅ Audio response played successfully (%zu samples in %zu chunks)\n", 
                downsampled_samples, total_chunks);
    }
    
    ast_log(LOG_NOTICE, "🎉 AUDIO PLAYBACK COMPLETED\n");
    
    return 0;
}

// OPTIMIZED message handling
static int openai_handle_message_optimized(struct openai_session_optimized *session, const char *message, size_t len) {
    if (!session || !message || len == 0) {
        return -1;
    }
    
    // Parse JSON message type
    char msg_type[64] = {0};
    
    // Simple JSON parsing for message type
    char *type_start = strstr(message, "\"type\":\"");
    if (type_start) {
        type_start += 8; // Skip "type":"
        char *type_end = strchr(type_start, '"');
        if (type_end) {
            size_t type_len = type_end - type_start;
            if (type_len < sizeof(msg_type)) {
                strncpy(msg_type, type_start, type_len);
                msg_type[type_len] = '\0';
            }
        }
    }
    
    ast_log(LOG_NOTICE, "Received message type: %s\n", msg_type);
    
    // Handle different message types
    if (strcmp(msg_type, "session.created") == 0) {
        ast_log(LOG_NOTICE, "✓ Session created\n");
        return openai_send_session_config_optimized(session);
        
    } else if (strcmp(msg_type, "session.updated") == 0) {
        ast_log(LOG_NOTICE, "✓ Session configured - ready for 16kHz audio\n");
        session->session_configured = 1;
        
        // Send welcome request to start the conversation (if enabled)
        if (session->send_welcome) {
            ast_log(LOG_NOTICE, "Sending welcome request to start conversation...\n");
            openai_send_welcome_request(session);
        } else {
            ast_log(LOG_NOTICE, "Welcome request disabled - waiting for user audio\n");
        }
        
    } else if (strcmp(msg_type, "input_audio_buffer.speech_started") == 0) {
        ast_log(LOG_NOTICE, "🎤 Speech detected - STOPPING any active playback\n");
        
        // STOP PLAYBACK IMMEDIATELY when user starts speaking
        session->stop_playback = 1;
        session->speech_detected = 1;
        session->last_speech_time = time(NULL);
        
        // Clear any pending audio response to avoid playing over user
        if (session->response_in_progress && session->response_buffer_used > 0) {
            ast_log(LOG_NOTICE, "🗑️ Clearing pending audio response (%zu bytes) due to speech\n", 
                    session->response_buffer_used);
            session->response_buffer_used = 0;
            session->response_in_progress = 0;
            if (session->response_audio_buffer) {
                session->response_audio_buffer[0] = '\0';
            }
        }
        
    } else if (strcmp(msg_type, "input_audio_buffer.speech_stopped") == 0) {
        ast_log(LOG_NOTICE, "🔇 Speech ended - playback can resume\n");
        session->speech_detected = 0;
        session->stop_playback = 0;  // Allow playback to resume
        
    } else if (strcmp(msg_type, "conversation.item.input_audio_transcription.completed") == 0) {
        // Extract transcript
        char *transcript_start = strstr(message, "\"transcript\":\"");
        if (transcript_start) {
            transcript_start += 14; // Skip "transcript":"
            char *transcript_end = strchr(transcript_start, '"');
            if (transcript_end) {
                size_t transcript_len = transcript_end - transcript_start;
                char transcript[512] = {0};
                if (transcript_len < sizeof(transcript)) {
                    strncpy(transcript, transcript_start, transcript_len);
                    ast_log(LOG_NOTICE, "📝 got Transcript: \"%s\"\n", transcript);
                }
            }
        }
        
    } else if (strcmp(msg_type, "response.audio_transcript.delta") == 0) {
        ast_log(LOG_NOTICE, "� Audio transcript delta\n");
        
    } else if (strcmp(msg_type, "response.audio.delta") == 0) {
        ast_log(LOG_NOTICE, "🔊 Audio response delta received\n");
        
        // Check if user is speaking - if so, don't accumulate more audio
        if (session->stop_playback || session->speech_detected) {
            ast_log(LOG_NOTICE, "🛑 Ignoring audio delta - user is speaking\n");
            return 0;
        }
        
        // Mark response as in progress
        session->response_in_progress = 1;
        
        // Extract base64 audio from the delta message
        char *audio_start = strstr(message, "\"delta\":\"");
        if (audio_start) {
            audio_start += 9; // Skip "delta":"
            char *audio_end = strchr(audio_start, '"');
            if (audio_end) {
                size_t audio_len = audio_end - audio_start;
                char *base64_audio = ast_malloc(audio_len + 1);
                if (base64_audio) {
                    strncpy(base64_audio, audio_start, audio_len);
                    base64_audio[audio_len] = '\0';
                    
                    // Accumulate the audio delta instead of playing immediately
                    accumulate_audio_delta(session, base64_audio);
                    ast_free(base64_audio);
                }
            }
        }
        
    } else if (strcmp(msg_type, "response.audio.done") == 0) {
        ast_log(LOG_NOTICE, "🔊 Audio response complete\n");
        
        // Only play if user is not speaking
        if (!session->stop_playback && !session->speech_detected) {
            ast_log(LOG_NOTICE, "🎵 Playing accumulated audio (user not speaking)\n");
            flush_accumulated_audio(session);
        } else {
            ast_log(LOG_NOTICE, "🛑 Skipping audio playback - user is speaking\n");
            // Clear the buffer instead of playing
            session->response_buffer_used = 0;
            session->response_in_progress = 0;
            if (session->response_audio_buffer) {
                session->response_audio_buffer[0] = '\0';
            }
        }
        
    } else if (strcmp(msg_type, "response.done") == 0) {
        ast_log(LOG_NOTICE, "✅ Complete response finished\n");
        // Ensure any remaining audio is played only if user is not speaking
        if (session->response_in_progress) {
            if (!session->stop_playback && !session->speech_detected) {
                flush_accumulated_audio(session);
            } else {
                ast_log(LOG_NOTICE, "🛑 Clearing remaining audio - user is speaking\n");
                session->response_buffer_used = 0;
                session->response_in_progress = 0;
                if (session->response_audio_buffer) {
                    session->response_audio_buffer[0] = '\0';
                }
            }
        }
        
    } else if (strcmp(msg_type, "error") == 0) {
        ast_log(LOG_ERROR, "❌ OpenAI error: %.*s\n", (int)len, message);
        
    } else {
        ast_log(LOG_NOTICE, "ℹ️ Message: %s\n", msg_type);
    }
    
    return 0;
}

// OPTIMIZED audio processing - immediate 16kHz upsampling and transmission
static int process_audio_frame_optimized(struct openai_session_optimized *session, struct ast_frame *frame) {
    if (!session || !frame || frame->datalen == 0) {
        return -1;
    }
    
    if (!session->session_configured) {
        return 0; // Wait for session to be ready
    }
    
    // Get format name
    const char *format_name = frame->subclass.format ? 
        ast_format_get_name(frame->subclass.format) : "unknown";
    
    // Convert audio to signed linear 16-bit
    int16_t *converted_audio = ast_malloc(frame->samples * sizeof(int16_t));
    if (!converted_audio) {
        ast_log(LOG_ERROR, "Failed to allocate conversion buffer\n");
        return -1;
    }
    
    int converted_samples = 0;
    
    // Convert from μ-law/A-law to linear PCM
    if (strstr(format_name, "ulaw") != NULL) {
        unsigned char *ulaw_data = (unsigned char *)frame->data.ptr;
        for (int i = 0; i < frame->samples; i++) {
            converted_audio[i] = AST_MULAW(ulaw_data[i]);
        }
        converted_samples = frame->samples;
        
    } else if (strstr(format_name, "alaw") != NULL) {
        unsigned char *alaw_data = (unsigned char *)frame->data.ptr;
        for (int i = 0; i < frame->samples; i++) {
            converted_audio[i] = AST_ALAW(alaw_data[i]);
        }
        converted_samples = frame->samples;
        
    } else if (strstr(format_name, "slin") != NULL || frame->datalen == frame->samples * 2) {
        // Already linear PCM
        memcpy(converted_audio, frame->data.ptr, frame->samples * sizeof(int16_t));
        converted_samples = frame->samples;
        
    } else {
        ast_log(LOG_WARNING, "Unsupported audio format: %s\n", format_name);
        ast_free(converted_audio);
        return -1;
    }
    
    // Simple voice activity detection (same as Python test)
    long long energy_sum = 0;
    int16_t max_sample = 0;
    
    for (int i = 0; i < converted_samples; i++) {
        energy_sum += (long long)converted_audio[i] * converted_audio[i];
        if (abs(converted_audio[i]) > max_sample) {
            max_sample = abs(converted_audio[i]);
        }
    }
    
    double rms_energy = sqrt((double)energy_sum / converted_samples);
    
    // PYTHON-LIKE BEHAVIOR: Send all audio without filtering (let OpenAI handle VAD)
    // Just log the audio levels for debugging
    ast_log(LOG_NOTICE, "Audio frame: RMS=%.1f, Peak=%d (sending all like Python)\n", rms_energy, max_sample);
    
    // Update speech tracking for logging only
    time_t current_time = time(NULL);
    session->last_speech_time = current_time;
    
    // UPSAMPLE 8kHz -> 16kHz (fast method)
    size_t upsampled_samples = converted_samples * 2;
    int16_t *upsampled_audio = ast_malloc(upsampled_samples * sizeof(int16_t));
    if (!upsampled_audio) {
        ast_log(LOG_ERROR, "Failed to allocate upsampling buffer\n");
        ast_free(converted_audio);
        return -1;
    }
    
    // Use fast upsampling (matches Python test simplicity)
    size_t actual_upsampled = upsample_8khz_to_16khz_fast(converted_audio, converted_samples, 
                                                         upsampled_audio, upsampled_samples);
    
    ast_log(LOG_NOTICE, "Upsampled %d -> %zu samples (8kHz -> 16kHz)\n", 
            converted_samples, actual_upsampled);
    
    // Add to buffer
    ast_mutex_lock(session->lock);
    
    size_t space_available = session->buffer_size - session->buffer_pos;
    size_t samples_to_add = (actual_upsampled < space_available) ? actual_upsampled : space_available;
    
    if (samples_to_add > 0) {
        memcpy(session->audio_buffer + session->buffer_pos, upsampled_audio, 
               samples_to_add * sizeof(int16_t));
        session->buffer_pos += samples_to_add;
    }
    
    // Send complete 320-sample frames immediately (like Python test)
    while (session->buffer_pos >= OPENAI_FRAME_SAMPLES_16K) {
        int result = openai_send_audio_chunk_optimized(session, session->audio_buffer, 
                                                     OPENAI_FRAME_SAMPLES_16K);
        
        if (result == 0) {
            // Shift buffer
            session->buffer_pos -= OPENAI_FRAME_SAMPLES_16K;
            if (session->buffer_pos > 0) {
                memmove(session->audio_buffer, 
                       session->audio_buffer + OPENAI_FRAME_SAMPLES_16K,
                       session->buffer_pos * sizeof(int16_t));
            }
        } else {
            ast_log(LOG_ERROR, "Failed to send audio frame\n");
            break;
        }
    }
    
    ast_mutex_unlock(session->lock);
    
    // Cleanup
    ast_free(converted_audio);
    ast_free(upsampled_audio);
    
    return 0;
}

// Stop any active audio playback immediately
static void stop_audio_playback(struct openai_session_optimized *session) {
    if (!session) {
        return;
    }
    
    ast_log(LOG_NOTICE, "🛑 STOPPING audio playback immediately\n");
    
    // Set stop flags
    session->stop_playback = 1;
    session->playback_active = 0;
    
    // Clear any accumulated audio response
    if (session->response_in_progress && session->response_buffer_used > 0) {
        ast_log(LOG_NOTICE, "🗑️ Clearing accumulated audio (%zu bytes)\n", 
                session->response_buffer_used);
        session->response_buffer_used = 0;
        session->response_in_progress = 0;
        if (session->response_audio_buffer) {
            session->response_audio_buffer[0] = '\0';
        }
    }
    
    ast_log(LOG_NOTICE, "✅ Audio playback stopped and buffers cleared\n");
}

// Audio response buffering functions
static int accumulate_audio_delta(struct openai_session_optimized *session, const char *base64_delta) {
    if (!session || !base64_delta) {
        ast_log(LOG_ERROR, "❌ Invalid parameters for accumulate_audio_delta\n");
        return -1;
    }
    
    size_t delta_len = strlen(base64_delta);
    ast_log(LOG_NOTICE, "📥 Accumulating audio delta: %zu bytes\n", delta_len);
    
    // Ensure buffer has enough space
    size_t needed_space = session->response_buffer_used + delta_len + 1;
    if (needed_space > session->response_buffer_size) {
        size_t new_size = session->response_buffer_size * 2;
        while (new_size < needed_space) {
            new_size *= 2;
        }
        
        char *new_buffer = ast_realloc(session->response_audio_buffer, new_size);
        if (!new_buffer) {
            ast_log(LOG_ERROR, "❌ Failed to expand response audio buffer\n");
            return -1;
        }
        
        session->response_audio_buffer = new_buffer;
        session->response_buffer_size = new_size;
        ast_log(LOG_NOTICE, "📈 Expanded audio response buffer to %zu bytes\n", new_size);
    }
    
    // Append the delta
    memcpy(session->response_audio_buffer + session->response_buffer_used, base64_delta, delta_len);
    session->response_buffer_used += delta_len;
    session->response_audio_buffer[session->response_buffer_used] = '\0';
    
    ast_log(LOG_NOTICE, "✅ Audio delta accumulated: +%zu bytes (total: %zu bytes)\n", 
            delta_len, session->response_buffer_used);
    
    return 0;
}

static int flush_accumulated_audio(struct openai_session_optimized *session) {
    if (!session) {
        ast_log(LOG_ERROR, "❌ Invalid session for flush_accumulated_audio\n");
        return -1;
    }
    
    if (!session->response_audio_buffer || session->response_buffer_used == 0) {
        ast_log(LOG_NOTICE, "⚠️ No accumulated audio to flush\n");
        return 0;
    }
    
    ast_log(LOG_NOTICE, "� FLUSHING accumulated audio response (%zu bytes base64)\n", 
            session->response_buffer_used);
    
    // Play the accumulated audio
    int result = play_openai_audio_response(session, session->response_audio_buffer);
    
    // Clear the buffer
    session->response_buffer_used = 0;
    session->response_in_progress = 0;
    
    if (session->response_audio_buffer) {
        session->response_audio_buffer[0] = '\0';
        ast_log(LOG_NOTICE, "🧹 Audio response buffer cleared\n");
    }
    
    ast_log(LOG_NOTICE, "✅ Audio flush completed with result: %d\n", result);
    return result;
}

// OPTIMIZED WebSocket thread
static void *websocket_thread_optimized(void *data) {
    struct openai_session_optimized *session = (struct openai_session_optimized *)data;
    
    if (!session) {
        return NULL;
    }
    
    ast_log(LOG_NOTICE, "Starting optimized WebSocket thread\n");
    
    // Create context with SSL support
    struct lws_context_creation_info info = {0};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols_optimized;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;  // Enable SSL support
    info.user = session;
    
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        ast_log(LOG_ERROR, "Failed to create WebSocket context\n");
        session->running = 0;
        return NULL;
    }
    
    // Connect to OpenAI with WSS (required for Realtime API)
    struct lws_client_connect_info connect_info = {0};
    connect_info.context = context;
    connect_info.address = "api.openai.com";
    connect_info.port = 443;  // HTTPS port
    connect_info.path = "/v1/realtime?model=gpt-4o-realtime-preview-2024-10-01";
    connect_info.host = "api.openai.com";
    connect_info.origin = "https://api.openai.com";  // HTTPS required
    connect_info.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;  // Enable SSL
    connect_info.protocol = "";
    connect_info.userdata = session;
    
    struct lws *wsi = lws_client_connect_via_info(&connect_info);
    if (!wsi) {
        ast_log(LOG_ERROR, "Failed to create WebSocket connection\n");
        lws_context_destroy(context);
        session->running = 0;
        return NULL;
    }
    
    session->wsi = wsi;
    lws_set_wsi_user(wsi, session);
    
    ast_log(LOG_NOTICE, "WebSocket connection initiated (WSS - secure)\n");
    
    // Event loop
    while (session->running && !session->destroying && !global_shutdown) {
        int service_result = lws_service(context, 50); // 50ms timeout
        
        if (service_result < 0) {
            ast_log(LOG_ERROR, "lws_service failed: %d\n", service_result);
            break;
        }
    }
    
    // Cleanup
    if (session->wsi) {
        session->wsi = NULL;
    }
    
    if (context) {
        lws_context_destroy(context);
    }
    
    ast_log(LOG_NOTICE, "Optimized WebSocket thread terminated\n");
    return NULL;
}

// OPTIMIZED session creation
static struct openai_session_optimized *create_openai_session_optimized(struct ast_channel *chan, const char *options) {
    if (!chan) {
        return NULL;
    }
    
    struct openai_session_optimized *session = ast_calloc(1, sizeof(struct openai_session_optimized));
    if (!session) {
        return NULL;
    }
    
    // Initialize essential fields
    session->chan = chan;
    session->running = 0;
    session->destroying = 0;
    session->wsi = NULL;
    session->session_configured = 0;
    session->frames_sent = 0;
    
    // Speech detection state
    session->speech_detected = 0;
    session->last_speech_time = 0;
    session->silence_frames = 0;
    session->consecutive_quiet_frames = 0;
    
    // Playback control state
    session->playback_active = 0;
    session->stop_playback = 0;
    
    // Audio buffer - minimal size
    session->buffer_size = AUDIO_BUFFER_SIZE;
    session->buffer_pos = 0;
    session->audio_buffer = ast_calloc(session->buffer_size, sizeof(int16_t));
    if (!session->audio_buffer) {
        ast_free(session);
        return NULL;
    }
    
    // Fragment buffer
    session->fragment_buffer_size = 32768;
    session->fragment_buffer_used = 0;
    session->fragment_buffer = ast_malloc(session->fragment_buffer_size);
    if (!session->fragment_buffer) {
        ast_free(session->audio_buffer);
        ast_free(session);
        return NULL;
    }
    
    // Mutex
    session->lock = ast_malloc(sizeof(ast_mutex_t));
    if (!session->lock) {
        ast_free(session->fragment_buffer);
        ast_free(session->audio_buffer);
        ast_free(session);
        return NULL;
    }
    ast_mutex_init(session->lock);
    
    // Audio response buffer
    session->response_buffer_size = 65536; // Start with 64KB
    session->response_buffer_used = 0;
    session->response_in_progress = 0;
    session->response_audio_buffer = ast_malloc(session->response_buffer_size);
    if (!session->response_audio_buffer) {
        ast_free(session->fragment_buffer);
        ast_free(session->audio_buffer);
        ast_free(session->lock);
        ast_free(session);
        return NULL;
    }
    session->response_audio_buffer[0] = '\0';
    
    // Default configuration (optimized)
    session->api_key = ast_strdup(getenv("OPENAI_API_KEY") ? getenv("OPENAI_API_KEY") : "");
    session->voice = ast_strdup("alloy");
    session->instructions = ast_strdup("You are testing 16kHz audio quality. Respond briefly and clearly.");
    session->language = ast_strdup("en");  // Default to English
    session->send_welcome = 1;  // Send welcome by default
    
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
                    ast_free(session->voice);
                    session->voice = ast_strdup(value);
                } else if (strcmp(key, "instructions") == 0) {
                    ast_free(session->instructions);
                    session->instructions = ast_strdup(value);
                } else if (strcmp(key, "language") == 0) {
                    ast_free(session->language);
                    session->language = ast_strdup(value);
                } else if (strcmp(key, "send_welcome") == 0) {
                    session->send_welcome = (strcmp(value, "1") == 0 || 
                                           strcmp(value, "yes") == 0 || 
                                           strcmp(value, "true") == 0) ? 1 : 0;
                }
            }
        }
    }
    
    if (!session->api_key || strlen(session->api_key) == 0) {
        ast_log(LOG_ERROR, "No API key provided\n");
        destroy_openai_session_optimized(session);
        return NULL;
    }
    
    ast_log(LOG_NOTICE, "✓ Optimized session created (16kHz)\n");
    ast_log(LOG_NOTICE, "  Voice: %s\n", session->voice);
    ast_log(LOG_NOTICE, "  Instructions: %s\n", session->instructions);
    ast_log(LOG_NOTICE, "  Language: %s\n", session->language);
    
    return session;
}

// Session cleanup
static void destroy_openai_session_optimized(struct openai_session_optimized *session) {
    if (!session) {
        return;
    }
    
    session->destroying = 1;
    session->running = 0;
    
    if (session->wsi) {
        session->wsi = NULL;
    }
    
    if (session->audio_buffer) {
        ast_free(session->audio_buffer);
    }
    
    if (session->fragment_buffer) {
        ast_free(session->fragment_buffer);
    }
    
    if (session->response_audio_buffer) {
        ast_free(session->response_audio_buffer);
    }
    
    if (session->api_key) {
        ast_free(session->api_key);
    }
    
    if (session->voice) {
        ast_free(session->voice);
    }
    
    if (session->instructions) {
        ast_free(session->instructions);
    }
    
    if (session->language) {
        ast_free(session->language);
    }
    
    if (session->lock) {
        ast_mutex_destroy(session->lock);
        ast_free(session->lock);
    }
    
    ast_free(session);
    
    ast_log(LOG_NOTICE, "✓ Optimized session destroyed\n");
}

// OPTIMIZED main execution function
static int openai_exec_optimized(struct ast_channel *chan, const char *data) {
    if (!chan) {
        return -1;
    }
    
    ast_log(LOG_NOTICE, "🚀 Starting OpenAI Realtime (OPTIMIZED - 16kHz)\n");
    ast_log(LOG_NOTICE, "Channel: %s\n", ast_channel_name(chan));
    
    // Create optimized session
    struct openai_session_optimized *session = create_openai_session_optimized(chan, data);
    if (!session) {
        ast_log(LOG_ERROR, "Failed to create optimized session\n");
        return -1;
    }
    
    // Set global session
    ast_mutex_lock(&session_lock);
    current_session = session;
    ast_mutex_unlock(&session_lock);
    
    // Start WebSocket thread
    session->running = 1;
    if (pthread_create(&session->websocket_thread, NULL, websocket_thread_optimized, session) != 0) {
        ast_log(LOG_ERROR, "Failed to create WebSocket thread\n");
        current_session = NULL;
        destroy_openai_session_optimized(session);
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✓ WebSocket thread started\n");
    
    // Wait for session to be configured
    int timeout = 20; // 10 seconds
    while (timeout > 0 && !session->session_configured && session->running) {
        usleep(500000); // 500ms
        timeout--;
    }
    
    if (!session->session_configured) {
        ast_log(LOG_ERROR, "Session configuration timeout\n");
        session->running = 0;
        pthread_join(session->websocket_thread, NULL);
        current_session = NULL;
        destroy_openai_session_optimized(session);
        return -1;
    }
    
    ast_log(LOG_NOTICE, "✅ Ready for 16kHz audio processing!\n");
    
    // Answer channel
    if (ast_channel_state(chan) != AST_STATE_UP) {
        if (ast_answer(chan) != 0) {
            ast_log(LOG_ERROR, "Failed to answer channel\n");
            session->running = 0;
            pthread_join(session->websocket_thread, NULL);
            current_session = NULL;
            destroy_openai_session_optimized(session);
            return -1;
        }
    }
    
    // Send welcome request to OpenAI only if enabled
    if (session->send_welcome) {
        ast_log(LOG_NOTICE, "📢 Sending welcome request (enabled in config)...\n");
        openai_send_welcome_request(session);
    } else {
        ast_log(LOG_NOTICE, "🔇 Welcome request disabled - waiting for user speech\n");
    }
    
    // Main audio processing loop
    ast_log(LOG_NOTICE, "🎤 Processing audio frames...\n");
    
    while (session->running && ast_channel_state(chan) == AST_STATE_UP && !global_shutdown) {
        // Check for hangup
        if (ast_check_hangup(chan)) {
            ast_log(LOG_NOTICE, "Hangup detected\n");
            break;
        }
        
        // Wait for frame
        int waitms = ast_waitfor(chan, 50);
        if (waitms < 0) {
            break;
        } else if (waitms == 0) {
            continue;
        }
        
        // Read frame
        struct ast_frame *f = ast_read(chan);
        if (!f) {
            continue;
        }
        
        // Process voice frames
        if (f->frametype == AST_FRAME_VOICE) {
            process_audio_frame_optimized(session, f);
        } else if (f->frametype == AST_FRAME_CONTROL) {
            if (f->subclass.integer == AST_CONTROL_HANGUP) {
                ast_log(LOG_NOTICE, "Hangup control frame received\n");
                ast_frfree(f);
                break;
            }
        }
        
        ast_frfree(f);
    }
    
    ast_log(LOG_NOTICE, "📊 Performance stats:\n");
    ast_log(LOG_NOTICE, "  Frames sent: %d\n", session->frames_sent);
    ast_log(LOG_NOTICE, "  Last transmission: %ld\n", session->last_audio_send_time);
    
    // Cleanup
    session->running = 0;
    pthread_join(session->websocket_thread, NULL);
    
    ast_mutex_lock(&session_lock);
    current_session = NULL;
    ast_mutex_unlock(&session_lock);
    
    destroy_openai_session_optimized(session);
    
    ast_log(LOG_NOTICE, "✓ OpenAI Realtime (OPTIMIZED) completed\n");
    return 0;
}

// Module registration
static int load_module(void) {
    ast_mutex_init(&session_lock);
    
    int result = ast_register_application2(app, openai_exec_optimized, NULL, NULL, AST_MODULE_SELF_SYM());
    
    if (result == 0) {
        ast_log(LOG_NOTICE, "✓ OpenAI Realtime OPTIMIZED module loaded\n");
        ast_log(LOG_NOTICE, "  Features: 16kHz audio, fast upsampling, minimal latency\n");
        ast_log(LOG_NOTICE, "  Usage: same(n,OpenAIRealtimeOptimized(api_key=YOUR_KEY,language=en,voice=alloy))\n");
    }
    
    return result;
}

static int unload_module(void) {
    global_shutdown = 1;
    usleep(500000); // 500ms
    
    ast_mutex_destroy(&session_lock);
    
    int result = ast_unregister_application(app);
    ast_log(LOG_NOTICE, "✓ OpenAI Realtime OPTIMIZED module unloaded\n");
    
    return result;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "OpenAI Realtime API - Optimized 16kHz",
    .support_level = AST_MODULE_SUPPORT_EXTENDED,
    .load = load_module,
    .unload = unload_module,
);
