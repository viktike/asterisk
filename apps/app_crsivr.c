/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 * \author James Dabbs <jdabbs@criticalresponse.com>
 * \brief CRS Sparkgap IVR Support
 *
 */

/*! \li \ref app_crsivr.c uses configuration file \ref crsivr.conf
 * \addtogroup configuration_file Configuration Files
 */

/*** MODULEINFO
	<defaultenabled>yes</defaultenabled>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"

#include "asterisk/paths.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/adsi.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/dsp.h"
#include "asterisk/localtime.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/stringfields.h"
#include "asterisk/strings.h"
#include "asterisk/smdi.h"
#include "asterisk/astobj2.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/test.h"
#include "asterisk/format_cache.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
//#include <dirent.h>
#include <poll.h>

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/wait.h>
#endif

#define IVR_CONFIG				"crsivr.conf"	// configuration file
#define IVR_SERVER_SEC			5 				// server transaction timeout
#define IVR_CONNECT_SEC			5 				// interval between connection attempts
#define IVR_PING_SEC			30 				// interval between pings
#define IVR_CHANNELS			4				// number of IVR channels

#define FUNC_SENDMSG			"CRS_SendMessage"
#define FUNC_VERIFYRECIPIENT	"CRS_VerifyRecipient"

typedef union
{
	uint64_t			raw[4];
	struct
	{
		unsigned int 	index;
		int 			id;
		volatile int 	state;
		int 			pipe_response_fd[2];
	};
} ivr_channel_t;

#define IVR_CHANNEL_STATE_CLOSED		0
#define IVR_CHANNEL_STATE_OPENING		1
#define IVR_CHANNEL_STATE_OPEN			2
#define IVR_CHANNEL_STATE_CLOSING		3

typedef union
{
	uint64_t		raw[16];
	
	struct	
	{
		uint32_t 	code;
		uint32_t	index;

		union
		{
			char	param[4][30];

			struct
			{
				struct sockaddr_in address[2]; 
				int valid[2];
				char client_id[20];
			};
		};
	};
} ivr_request_t;

#define IVR_REQUEST_VERIFYRECIPIENT				'v'
#define IVR_REQUEST_SENDMESSAGE					's'
#define IVR_REQUEST_QUERYMESSAGE				'q'
#define IVR_REQUEST_PING						'p'

#define IVR_REQUEST_STOP						0
#define IVR_REQUEST_CONFIG						1

#define IVR_RESPONSE_SUCCESS					'0'
#define IVR_RESPONSE_SUCCESS_MESSAGEQUEUED		'a'
#define IVR_RESPONSE_SUCCESS_MESSAGEDELIVERED	'b'
#define IVR_RESPONSE_SUCCESS_MESSAGEREAD		'c'
#define IVR_RESPONSE_FAIL_RECIPIENTNOTFOUND		'1'
#define IVR_RESPONSE_FAIL_RECIPIENTDISABLED		'2'
#define IVR_RESPONSE_FAIL_SYSTEMNOTAVAILABLE	'3'
#define IVR_RESPONSE_FAIL_INVALIDCLIENT			'4'
#define IVR_RESPONSE_FAIL_UNKNOWNREQUEST		'5'

#define IVR_RESPONSE_FAIL_INTERNAL				'8'
#define IVR_RESPONSE_FAIL_HANGUP				'9'

typedef struct
{
//
// Worker thread
//
	struct pollfd			sock_fd;
	struct pollfd			pipe_fd;
	char					client_id[20];
	struct sockaddr_in * 	address[2]; 
	struct sockaddr_in 		a[2]; 

	time_t 					time_connectattempt;	// last connection attempt
	time_t 					time_transaction;		// last server transaction


	int 					flag_connect_notify;	// 1 = a connection failure has been logged
//
// Shared
//
	char					tagBase[30];
	pthread_t				thread;
	volatile int			initialized;
	int						pipe_request_fd[2];
	ivr_channel_t			channel[IVR_CHANNELS];
//
// Channel threads
//
	int						config_ready;
	ivr_request_t			config_request;

} ivr_context_t;


//
// Function Prototypes
//

static void ivr_worker_gc(ivr_context_t * ivr);
static void ivr_worker_connect_ip(ivr_context_t * ivr, struct sockaddr_in * address);
static void ivr_worker_connect(ivr_context_t * ivr);
static void ivr_worker_disconnect(ivr_context_t * ivr);
static void ivr_worker_transact_server(ivr_context_t * ivr, const ivr_request_t * request);
static void ivr_worker_ping_server(ivr_context_t * ivr);
static void * ivr_worker_task(void *arg);

static ivr_channel_t * ivr_channel_acquire(void);
static void ivr_channel_release(ivr_channel_t * ivr_chan);
static int ivr_load(void);
static void ivr_unload(void);
static int ivr_wait(struct ast_channel *c, int fd);
static void ivr_datastore_destroy(void *data);
static ivr_channel_t * ivr_get_channel(struct ast_channel * chan);
static int ivr_sendmessage(struct ast_channel * chan, const char * recipient, const char *caller, const char *request);
static int ivr_verifyrecipient(struct ast_channel * chan, const char * recipient);
static int ivr_setresponse(struct ast_channel * chan, int response);
static int sendmsg_exec(struct ast_channel *chan, const char *data);
static int verifyrecipient_exec(struct ast_channel *chan, const char *data);
static int load_config(ivr_context_t * ivr, int reload);
static int load_module(void);
static int unload_module(void);
static int reload(void);

static ivr_context_t ivr_context = {.initialized = 0};

AST_MUTEX_DEFINE_STATIC(ivr_mutex);

static const struct ast_datastore_info ivr_datastore =
{
	.type = "crs_ivr",
	.destroy = ivr_datastore_destroy
};


static ivr_channel_t * ivr_channel_acquire()
{
	int i;
	ivr_context_t * ivr = &ivr_context;
	ivr_channel_t * ivr_chan = 0;
	ast_mutex_lock(&ivr_mutex);

	for (i = 0; i != IVR_CHANNELS; ++i)
	{
		if (ivr->channel[i].state == IVR_CHANNEL_STATE_CLOSED)
		{
			ivr_chan = &ivr->channel[i];
			ivr_chan->state = IVR_CHANNEL_STATE_OPENING;
			break;
		}
	}

	ast_mutex_unlock(&ivr_mutex);

	if (ivr_chan == 0)
	{
		return 0;
	}

	ivr_chan->pipe_response_fd[0] = -1;
	ivr_chan->pipe_response_fd[1] = -1;

	if (0 != pipe(ivr_chan->pipe_response_fd))
	{
		ast_log(LOG_ERROR, "Unable to create response pipe.\n");
		ivr_chan->state = IVR_CHANNEL_STATE_CLOSED;
		return 0;
	}

	return ivr_chan;
}

static void ivr_channel_release(ivr_channel_t * ivr_chan)
{
	ivr_chan->state = IVR_CHANNEL_STATE_CLOSING;
}

static int ivr_load()
{
	ivr_context_t * ivr = &ivr_context;
	int i;

	if (__sync_bool_compare_and_swap(&(ivr->initialized), 0, 1))
	{
		for (i = 0; i != IVR_CHANNELS; ++i)
		{
			ivr->channel[i].state = IVR_CHANNEL_STATE_CLOSED;
			ivr->channel[i].index = i;
			ivr->channel[i].pipe_response_fd[0] = -1;
			ivr->channel[i].pipe_response_fd[1] = -1;
		}

		snprintf(ivr->tagBase, sizeof(ivr->tagBase), "m%02x-%08x-", getpid() % 256, (unsigned int)time(0));

		ivr->pipe_request_fd[0] = -1;
		ivr->pipe_request_fd[1] = -1;
		ivr->thread = -1;

		if (0 != pipe(ivr->pipe_request_fd))
		{
			ast_log(LOG_ERROR, "Unable to create IVR request pipe.\n");
			return 0;
		}

		if (pthread_create(&ivr->thread, NULL, ivr_worker_task, ivr))
		{
			ast_log(LOG_ERROR, "Unable to create IVR worker thread.\n");
			return 0;
		}

		ast_log(LOG_NOTICE, "IVR subsystem loaded (%u channels).\n", IVR_CHANNELS);
		
		return 1;
	}

	return 0;
}

static void ivr_unload()
{
	ivr_context_t * ivr = &ivr_context;
	int i;

	const ivr_request_t ivr_request_stop = 
		{.code = IVR_REQUEST_STOP};

	if (__sync_bool_compare_and_swap(&(ivr->initialized), 1, 0))
	{
		if (ivr->thread != -1)
		{
			if (sizeof(ivr_request_stop) != write(ivr->pipe_request_fd[1], &ivr_request_stop, sizeof(ivr_request_stop)))
			{
				return;
			}

			if (pthread_join(ivr->thread, 0))
			{
				return;
			}

			ast_log(LOG_NOTICE, "IVR worker thread stopped.\n");

			ivr->thread = -1;
		}

		for (i = 0; i != IVR_CHANNELS; ++i)
		{
			if (ivr->channel[i].pipe_response_fd[0] != -1)
			{
				close(ivr->channel[i].pipe_response_fd[0]);
			}

			if (ivr->channel[i].pipe_response_fd[1] != -1)
			{
				close(ivr->channel[i].pipe_response_fd[1]);
			}
		}

		if (ivr->pipe_request_fd[0] != -1)
		{
			close(ivr->pipe_request_fd[0]);
			ivr->pipe_request_fd[0] = -1;
		}

		if (ivr->pipe_request_fd[1] != -1)
		{
			close(ivr->pipe_request_fd[1]);
			ivr->pipe_request_fd[1] = -1;
		}

		ast_log(LOG_NOTICE, "IVR subsystem unloaded.\n");
	}
}

static void ivr_worker_gc(ivr_context_t * ivr)
{
	int i;
	ivr_channel_t * ivr_chan = 0;

	for (i = 0; i != IVR_CHANNELS; ++i)
	{
		ivr_chan = &ivr->channel[i];

		if (ivr_chan->state == IVR_CHANNEL_STATE_CLOSING)
		{
			if (ivr_chan->pipe_response_fd[0] != -1)
			{
				close(ivr_chan->pipe_response_fd[0]);
			}

			if (ivr_chan->pipe_response_fd[1] != -1)
			{
				close(ivr_chan->pipe_response_fd[0]);
			}

			ivr_chan->pipe_response_fd[0] = -1;
			ivr_chan->pipe_response_fd[1] = -1;

			ivr_chan->index += 0x00000100;
			ivr_chan->state = IVR_CHANNEL_STATE_CLOSED;
		}
	}
}

static void ivr_worker_connect_ip(ivr_context_t * ivr, struct sockaddr_in * address)
{
	char text[50];

	if (address == 0)
	{
		return;
	}

	ivr->sock_fd.fd = socket(AF_INET, SOCK_STREAM, 0);

	if (ivr->sock_fd.fd < 0)
	{
		return;
	}

	if (connect(ivr->sock_fd.fd, (struct sockaddr *)address, sizeof(*address)) != 0)
	{
		if (ivr->flag_connect_notify == 0)
		{
			ivr->flag_connect_notify = 1;

			if (0 != inet_ntop(AF_INET, &(address->sin_addr), text, sizeof(text)))
			{
				ast_log(LOG_NOTICE, "unable to connect to IVR server %s:%u.\n", text, ntohs(address->sin_port));
			}
			else
			{
				ast_log(LOG_NOTICE, "unable to connect to IVR server.\n");
			}
		}
	
		close(ivr->sock_fd.fd);
		ivr->sock_fd.fd = -1;
	}
	else
	{
		ivr->flag_connect_notify = 0;

		if (0 != inet_ntop(AF_INET, &(address->sin_addr), text, sizeof(text)))
		{
			ast_log(LOG_NOTICE, "connected to IVR server %s:%u.\n", text, ntohs(address->sin_port));
		}
		else
		{
			ast_log(LOG_NOTICE, "connected to IVR server.\n");
		}
	}
}

static void ivr_worker_connect(ivr_context_t * ivr)
{
	time_t now;

	time(&now);

	if ((now - ivr->time_connectattempt) < IVR_CONNECT_SEC)
	{
		return;
	}

	if (ivr->sock_fd.fd < 0)
	{
		ivr_worker_connect_ip(ivr, ivr->address[0]);
	}

	if (ivr->sock_fd.fd < 0)
	{
		ivr_worker_connect_ip(ivr, ivr->address[1]);
	}

	if (ivr->sock_fd.fd < 0)
	{
		ivr->time_connectattempt = now;
	}
}

static void ivr_worker_disconnect(ivr_context_t * ivr)
{
	if (ivr->sock_fd.fd >= 0)
	{
		close(ivr->sock_fd.fd);
		ivr->sock_fd.fd = -1;
	}
}

static void ivr_worker_ping_server(ivr_context_t * ivr)
{
	const struct timespec wait_time = {.tv_sec = IVR_SERVER_SEC, .tv_nsec = 0};
	uint8_t response;
	char server_request[128];
	int server_request_length;

	time_t now;

	time(&now);

	if ((now - ivr->time_transaction) < IVR_PING_SEC)
	{
		return;
	}

	if (ivr->sock_fd.fd < 0)
	{
		return;
	}

	server_request_length = sprintf
	(
		server_request,
		"[p:%s]",
		ivr->client_id
	);

	if (write(ivr->sock_fd.fd, server_request, server_request_length) != server_request_length)
	{
		ivr->time_transaction = now;
		ivr_worker_disconnect(ivr);
		return;
	}	

	ivr->time_transaction = now;

	if (ppoll(&ivr->sock_fd, 1, &wait_time, 0) > 0)
	{
		if (sizeof(response) != read(ivr->sock_fd.fd, &response, sizeof(response)))
		{
			ivr_worker_disconnect(ivr);
		}
	}
	else
	{
		ivr_worker_disconnect(ivr);
	}
}

static void ivr_worker_transact_server(ivr_context_t * ivr, const ivr_request_t * request)
{
	time_t now;
	static uint8_t error_unknown = IVR_RESPONSE_FAIL_UNKNOWNREQUEST;
	static uint8_t error_noserver = IVR_RESPONSE_FAIL_SYSTEMNOTAVAILABLE;
	const struct timespec wait_time = {.tv_sec = IVR_SERVER_SEC, .tv_nsec = 0};

	ivr_channel_t * ivr_chan = &ivr->channel[request->index & 0xff];

	char server_request[128];
	int server_request_length;
	uint8_t response;

	if (ivr_chan->index != request->index)
	{
		return;
	}

	if (ivr->sock_fd.fd < 0)
	{
		if (sizeof(error_noserver) != write(ivr_chan->pipe_response_fd[1], &error_noserver, sizeof(error_noserver)))
		{
			ast_log(LOG_ERROR, "Unable to write to response pipe (1).\n");
		}

		return;
	}

	if (request->code == IVR_REQUEST_VERIFYRECIPIENT)
	{
		server_request_length = sprintf
		(
			server_request,
			"[%c:%s,%s]",
			request->code,
			ivr->client_id,
			request->param[0]
		);
	}

	else if (request->code == IVR_REQUEST_SENDMESSAGE)
	{
		server_request_length = sprintf
		(
			server_request,
			"[%c:%s,%s%08x,%s,%s,%s]",
			request->code,
			ivr->client_id,
			ivr->tagBase,
			ivr_chan->index,
			request->param[0],
			request->param[1],
			request->param[2]
		);
	}

	else
	{
		if (sizeof(error_unknown) != write(ivr_chan->pipe_response_fd[1], &error_unknown, sizeof(error_unknown)))
		{
			ast_log(LOG_ERROR, "Unable to write to response pipe (2).\n");
		}

		return;
	}

	if (write(ivr->sock_fd.fd, server_request, server_request_length) < 0)
	{
		if (sizeof(error_noserver) != write(ivr_chan->pipe_response_fd[1], &error_noserver, sizeof(error_noserver)))
		{
			ast_log(LOG_ERROR, "Unable to write to response pipe (3).\n");
		}

		ivr_worker_disconnect(ivr);
		return;
	}

	time(&now);
	ivr->time_transaction = now;

    if (ppoll(&ivr->sock_fd, 1, &wait_time, 0) > 0)
	{
		if (sizeof(response) == read(ivr->sock_fd.fd, &response, sizeof(response)))
		{
			if (sizeof(response) != write(ivr_chan->pipe_response_fd[1], &response, sizeof(response)))
			{
				ast_log(LOG_ERROR, "Unable to write to response pipe (4).\n");
			}
		}
		else
		{
			if (sizeof(error_noserver) != write(ivr_chan->pipe_response_fd[1], &error_noserver, sizeof(error_noserver)))
			{
				ast_log(LOG_ERROR, "Unable to write to response pipe (5).\n");
			}

			ivr_worker_disconnect(ivr);
		}
	}
	else
	{
		if (sizeof(error_noserver) != write(ivr_chan->pipe_response_fd[1], &error_noserver, sizeof(error_noserver)))
		{
			ast_log(LOG_ERROR, "Unable to write to response pipe (6).\n");
		}

		ivr_worker_disconnect(ivr);
	}
}

static void * ivr_worker_task(void *arg)
{
	ivr_context_t * ivr = (ivr_context_t *)arg;
	int i;
	ivr_request_t request[8];
	ivr_request_t * prequest;
	int readlen;

	const struct timespec wait_time = {.tv_sec = 2, .tv_nsec = 0};

	ast_log(LOG_NOTICE, "IVR worker thread started.\n");

	ivr->pipe_fd.fd = ivr->pipe_request_fd[0];
	ivr->pipe_fd.events = POLLIN | POLLPRI;

	ivr->sock_fd.fd = -1;
	ivr->sock_fd.events = POLLIN | POLLPRI;

	while (1)
	{
		ivr_worker_gc(ivr);
		ivr_worker_connect(ivr);
		ivr_worker_ping_server(ivr);

    	if (ppoll(&ivr->pipe_fd, 1, &wait_time, 0) > 0)
		{
			if (0 != (ivr->pipe_fd.revents & POLLIN))
			{
				readlen = read(ivr->pipe_request_fd[0], request, sizeof(request));

				for (i = 0; i != readlen/sizeof(ivr_request_t); ++i)
				{
					prequest = &request[i];

					if (prequest->code == IVR_REQUEST_STOP)
					{
						ast_log(LOG_NOTICE, "worker thread stopped.\n");
						return 0;
					}

					else if (prequest->code == IVR_REQUEST_CONFIG)
					{
						ast_copy_string(ivr->client_id, prequest->client_id, sizeof(ivr->client_id));

						ivr->a[0] = prequest->address[0];
						ivr->address[0] = &ivr->a[0];

						if (prequest->valid[1])
						{
							ivr->a[1] = prequest->address[1];
							ivr->address[1] = &ivr->a[1];
						}
						else
						{
							ivr->address[1] = 0;
						}

						ast_log(LOG_NOTICE, "worker thread applied configuration.\n");
					}

					else
					{
						ivr_worker_transact_server(ivr, prequest);
					}
				}
			}
			else if (0 != (ivr->pipe_fd.revents & (POLLERR | POLLHUP)))
			{
				ast_log(LOG_ERROR, "Error reading from request pipe.\n");
			}
		}
	}
}

int ivr_wait(struct ast_channel *c, int fd)
{
	uint8_t response;
	struct ast_frame *f;
	struct ast_channel *rchan;
	int outfd;
	int ms = (IVR_SERVER_SEC * 1000) + 500;

	// Stop if we're a zombie or need a soft hangup
	if (ast_test_flag(ast_channel_flags(c), AST_FLAG_ZOMBIE) || ast_check_hangup(c)) 
	{
		return IVR_RESPONSE_FAIL_HANGUP;
	}

	while(ms)
	{
		rchan = ast_waitfor_nandfds(&c, 1, &fd, 1, NULL, &outfd, &ms);

		if ((!rchan) && (outfd < 0) && (ms))
		{ 
			ast_log(LOG_WARNING, "ivr_wait failed (%s)\n", strerror(errno));
			return -1;
		}

		else if (outfd > -1)
		{
			if (sizeof(response) != read(outfd, &response, sizeof(response)))
			{
				return -1;
			}
			else
			{
				return response;
			}
		}

		else if (rchan)
		{
			f = ast_read(c);

			if (f == 0)
			{	
				return IVR_RESPONSE_FAIL_INTERNAL;
          	}

			if (f->frametype == AST_FRAME_CONTROL)
			{
				switch(f->subclass.integer)
				{
					case AST_CONTROL_HANGUP:
						ast_frfree(f);
						return IVR_RESPONSE_FAIL_HANGUP;

					case AST_CONTROL_RINGING:
					case AST_CONTROL_ANSWER:
						break;

					default:
						ast_log(LOG_WARNING, "Unexpected control subclass '%d'\n", f->subclass.integer);
						break;
				}
			}

			ast_frfree(f);
		}
	}

	return IVR_RESPONSE_FAIL_INTERNAL; // Time is up
}

static void ivr_datastore_destroy(void *data)
{
	ivr_channel_t * ivr_chan = (ivr_channel_t *)data;

	if (ivr_chan != 0)
	{
		ivr_channel_release(ivr_chan);
	}
}

static ivr_channel_t * ivr_get_channel(struct ast_channel * chan)
{
	ivr_channel_t * ivr_chan;
	struct ast_datastore * datastore;

	datastore = ast_channel_datastore_find(chan, &ivr_datastore, 0);
	
	if (datastore == 0)
	{
		ivr_chan = ivr_channel_acquire();
		datastore = ast_datastore_alloc(&ivr_datastore, 0);
		datastore->data = ivr_chan;
		ast_channel_datastore_add(chan, datastore);
		return ivr_chan;
	}
	else
	{
		return (ivr_channel_t *)datastore->data;
	}
}

static int ivr_sendmessage(struct ast_channel * chan, const char * recipient, const char *message, const char * caller)
{
	ivr_context_t * ivr = &ivr_context;
	ivr_channel_t * ivr_chan = ivr_get_channel(chan);
	ivr_request_t request;

	if (ivr_chan == 0)
	{
		return IVR_RESPONSE_FAIL_SYSTEMNOTAVAILABLE;
	}

	request.code = IVR_REQUEST_SENDMESSAGE;
	request.index = ivr_chan->index;

	if ((recipient == 0) || (recipient[0] == 0))
	{
		return IVR_RESPONSE_FAIL_INTERNAL;
	}
	else
	{
		ast_copy_string(request.param[0], recipient, sizeof(request.param[0]));
	}

	if ((message == 0) || (message[0] == 0))
	{
		ast_copy_string(request.param[1], "no message", sizeof(request.param[1]));
	}
	else
	{
		ast_copy_string(request.param[1], message, sizeof(request.param[1]));
	}

	if ((caller == 0) || (caller[0] == 0))
	{
		ast_copy_string(request.param[2], "unknown caller", sizeof(request.param[2]));
	}
	else
	{
		ast_copy_string(request.param[2], caller, sizeof(request.param[2]));
	}

	request.param[3][0] = 0;

	if (write(ivr->pipe_request_fd[1], &request, sizeof(request)) != sizeof(request))
	{
		return IVR_RESPONSE_FAIL_INTERNAL;
	}

	return ivr_wait(chan, ivr_chan->pipe_response_fd[0]);
} 

static int ivr_verifyrecipient(struct ast_channel * chan, const char * recipient)
{
	ivr_context_t * ivr = &ivr_context;
	ivr_channel_t * ivr_chan = ivr_get_channel(chan);
	ivr_request_t request;

	if (ivr_chan == 0)
	{
		return IVR_RESPONSE_FAIL_SYSTEMNOTAVAILABLE;
	}

	request.code = IVR_REQUEST_VERIFYRECIPIENT;
	request.index = ivr_chan->index;
	ast_copy_string(request.param[0], recipient, sizeof(request.param[0]));
	request.param[1][0] = 0;
	request.param[2][0] = 0;
	request.param[3][0] = 0;

	if (write(ivr->pipe_request_fd[1], &request, sizeof(request)) != sizeof(request))
	{
		return IVR_RESPONSE_FAIL_INTERNAL;
	}

	return ivr_wait(chan, ivr_chan->pipe_response_fd[0]);
} 

static int ivr_setresponse(struct ast_channel * chan, int response)
{
	switch(response)
	{
		case IVR_RESPONSE_SUCCESS:
		{
			pbx_builtin_setvar_helper(chan, "CRS_RESPONSE", "OK");
			return 0;
		}

		case IVR_RESPONSE_FAIL_UNKNOWNREQUEST:
		{
			pbx_builtin_setvar_helper(chan, "CRS_RESPONSE", "UNKNOWN_REQ");
			return 0;
		}

		case IVR_RESPONSE_FAIL_RECIPIENTNOTFOUND:
		{
			pbx_builtin_setvar_helper(chan, "CRS_RESPONSE", "RECIPIENT_INVALID");
			return 0;
		}

		case IVR_RESPONSE_FAIL_RECIPIENTDISABLED:
		{
			pbx_builtin_setvar_helper(chan, "CRS_RESPONSE", "RECIPIENT_DISABLED");
			return 0;
		}

		case IVR_RESPONSE_FAIL_SYSTEMNOTAVAILABLE:
		{
			pbx_builtin_setvar_helper(chan, "CRS_RESPONSE", "SYSTEM_UNAVAIL");
			return 0;
		}

		case IVR_RESPONSE_FAIL_HANGUP:
		{
			pbx_builtin_setvar_helper(chan, "CRS_RESPONSE", "HANGUP");
			return 0;
		}

		default:
		case IVR_RESPONSE_FAIL_INTERNAL:
		{
			pbx_builtin_setvar_helper(chan, "CRS_RESPONSE", "ERROR_INTERNAL");
			return 0;
		}
	}
}

static const char * sendmsg_name =
	FUNC_SENDMSG;

static const char * sendmsg_synopsis =
	"Send a text message to the server.";

static const char sendmsg_description[] =
	FUNC_SENDMSG "(<recipient>,<message>[,<caller>])\n"
	"  Sends a message to the server, specifying the recipient, the\n"
	"  message, and an optional caller.\n";

static int sendmsg_exec(struct ast_channel *chan, const char *data)
{
	char * parse;
	int response;

	AST_DECLARE_APP_ARGS
	(
		args,
		AST_APP_ARG(recipient);
		AST_APP_ARG(message);
		AST_APP_ARG(caller);
	);

	if (ast_strlen_zero(data))
	{
		ast_log( LOG_WARNING, FUNC_SENDMSG " requires two or three arguments (<recipient>,<message>[,<caller>])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 2)
	{
		ast_log(LOG_WARNING, FUNC_SENDMSG " requires two or three arguments (<recipient>,<message>[,<caller>])\n");
		return -1;
	}

	response = ivr_sendmessage(chan, args.recipient, args.message, args.caller);
	return ivr_setresponse(chan, response);
}

static const char * verifyrecipient_name =
	FUNC_VERIFYRECIPIENT;

static const char * verifyrecipient_synopsis =
	"Verify a recipient";

static const char verifyrecipient_description[] =
	FUNC_VERIFYRECIPIENT "(<recipient>)\n"
	"  Verify a recipient with the server.\n";

static int verifyrecipient_exec(struct ast_channel *chan, const char *data)
{
	char * parse;
	int response;

	AST_DECLARE_APP_ARGS
	(
		args,
		AST_APP_ARG(recipient);
	);

	if (ast_strlen_zero(data))
	{
		ast_log(LOG_WARNING, FUNC_VERIFYRECIPIENT " requires one argument (<recipient>)\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc != 1)
	{
		ast_log(LOG_WARNING, FUNC_VERIFYRECIPIENT " requires one argument (<recipient>)\n");
		return -1;
	}

	response = ivr_verifyrecipient(chan, args.recipient);
	return ivr_setresponse(chan, response);
}

static int load_address(struct sockaddr_in * address, const char *ip, uint16_t port)
{
	address->sin_family = AF_INET;
	address->sin_port = htons(port);

	if ((ip == 0) || (ip[0] == 0))
	{
		return 0;
	}

  	if (inet_pton(AF_INET, ip, &address->sin_addr) <= 0)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

static int load_config(ivr_context_t * ivr, int reload)
{
	ivr_request_t * m = &ivr->config_request;
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *val;
	unsigned long port;
	uint16_t port16;

	ivr_context.config_ready = 0;

	cfg = ast_config_load(IVR_CONFIG, config_flags);

	if (cfg == 0)
	{
		ast_log(LOG_ERROR, "Config file " IVR_CONFIG " not found.  Aborting.\n");
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEUNCHANGED)
	{
		return 0;
	}

	else if (cfg == CONFIG_STATUS_FILEINVALID)
	{
		ast_log(LOG_ERROR, "Config file " IVR_CONFIG " is in an invalid format.  Aborting.\n");
		return 0;
	}
	else
	{
		val = ast_variable_retrieve(cfg, "server", "client_id");

		if (val == 0)
		{
			val = "default";
		}

		ast_copy_string(m->client_id, val, sizeof(m->client_id));


		port = ULONG_MAX;

		val = ast_variable_retrieve(cfg, "server", "port");

		if (val != 0)
		{
			port = strtoul(val, 0, 0);
		}

		port16 = (port == ULONG_MAX) ? 55001 : (uint16_t)port;

		val = ast_variable_retrieve(cfg, "server", "primary_ip");
		m->valid[0] = load_address(&m->address[0], val, port16);

		val = ast_variable_retrieve(cfg, "server", "secondary_ip");
		m->valid[1] = load_address(&m->address[1], val, port16);

		m->code = IVR_REQUEST_CONFIG;

		if (m->valid[0] == 0)
		{
			ast_log(LOG_WARNING, "Config file " IVR_CONFIG " contains invalid primary server address.\n");
		}
		else
		{
			ivr->config_ready = 1;
		}

		ast_config_destroy(cfg);
		cfg = 0;

		return 1;
	}
}


static int reload(void)
{
	ivr_context_t * ivr = &ivr_context;
	ivr_request_t * m = &ivr->config_request;

	if (load_config(ivr, 1))
	{
		if (ivr->config_ready != 0)
		{
			if (sizeof(*m) != write(ivr->pipe_request_fd[1], m, sizeof(*m)))
			{
				ast_log(LOG_ERROR, "Unable to reconfigure worker thread.\n");
				return 0;
			}

			ast_log(LOG_NOTICE, "Sent reconfiguration to worker thread.\n");
		}
	}

	return 1;
}

static int load_module(void)
{
	ivr_context_t * ivr = &ivr_context;
	ivr_request_t * m = &ivr->config_request;
	int res;

	res = load_config(ivr, 0);

	if (res == 0)
	{
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (0 == ivr_load())
	{
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ivr->config_ready)
	{
		if (sizeof(*m) != write(ivr->pipe_request_fd[1], m, sizeof(*m)))
		{
			ast_log(LOG_ERROR, "Unable to configure worker thread.\n");
			unload_module();
			return AST_MODULE_LOAD_FAILURE;
		}

		ast_log(LOG_NOTICE, "Sent configuration to worker thread.\n");
	}

	res = ast_register_application(sendmsg_name, sendmsg_exec, sendmsg_synopsis, sendmsg_description);
	res |= ast_register_application(verifyrecipient_name, verifyrecipient_exec, verifyrecipient_synopsis, verifyrecipient_description);

	if (res)
	{
		ast_log(LOG_ERROR, "Failure registering applications, functions or tests\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(sendmsg_name);
	res |= ast_unregister_application(verifyrecipient_name);

	ivr_unload();

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "CRS IVR Support Functions");

