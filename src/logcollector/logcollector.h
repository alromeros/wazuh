/* Copyright (C) 2015-2020, Wazuh Inc.
 * Copyright (C) 2009 Trend Micro Inc.
 * All right reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */

#ifndef LOGREADER_H
#define LOGREADER_H

#ifndef ARGV0
#define ARGV0 "ossec-logcollector"
#endif

#define N_MIN_INPUT_THREADS 1
#define N_OUPUT_THREADS 1
#define OUTPUT_MIN_QUEUE_SIZE 128
#define WIN32_MAX_FILES 200

///> JSON path wich contains the files position of last read
#ifdef WIN32
#define LOCALFILE_STATUS_PATH   "queue\\logcollector\\file_status.json"
#else
#define LOCALFILE_STATUS        "/queue/logcollector/file_status.json"
#define LOCALFILE_STATUS_PATH   DEFAULTDIR LOCALFILE_STATUS
#endif

///> JSON fields for file_status
#define OS_LOGCOLLECTOR_JSON_FILES      "files"
#define OS_LOGCOLLECTOR_JSON_PATH       "path"
#define OS_LOGCOLLECTOR_JSON_HASH       "hash"
#define OS_LOGCOLLECTOR_JSON_OFFSET     "offset"

#include "shared.h"
#include "config/localfile-config.h"
#include "config/config.h"
#include "os_crypto/sha1/sha1_op.h"

/**/
#ifdef WIN32
typedef int64_t w_offset_t;
#else
typedef long w_offset_t;
#endif

/*** Function prototypes ***/

/* Read logcollector config */
int LogCollectorConfig(const char *cfgfile);

/* Parse read config into JSON format */
cJSON *getLocalfileConfig(void);
cJSON *getSocketConfig(void);
cJSON *getLogcollectorInternalOptions(void);

/* Start log collector daemon */
void LogCollectorStart(void) __attribute__((noreturn));

/* Handle files */
int handle_file(int i, int j, int do_fseek, int do_log);

/* Reload file: open after close, and restore position */
int reload_file(logreader * lf);

/* Close file and save position */
void close_file(logreader * lf);

/* Read syslog file */
void *read_syslog(logreader *lf, int *rc, int drop_it);

#ifdef WIN32
/* Read ucs2 LE file*/
void *read_ucs2_le(logreader *lf, int *rc, int drop_it);

/* Read ucs2 BE file */
void *read_ucs2_be(logreader *lf, int *rc, int drop_it);
#endif

/* Read snort full file */
void *read_snortfull(logreader *lf, int *rc, int drop_it);

/* Read ossec alert file */
void *read_ossecalert(logreader *lf, int *rc, int drop_it);

/* Read nmap grepable format */
void *read_nmapg(logreader *lf, int *rc, int drop_it);

/* Read mysql log format */
void *read_mysql_log(logreader *lf, int *rc, int drop_it);

/* Read mysql log format */
void *read_mssql_log(logreader *lf, int *rc, int drop_it);

/* Read postgresql log format */
void *read_postgresql_log(logreader *lf, int *rc, int drop_it);

/* read multi line logs */
void *read_multiline(logreader *lf, int *rc, int drop_it);

/* read multi line logs with variable lenght */
void *read_multiline_regex(logreader *lf, int *rc, int drop_it);

/* Read DJB multilog format */
/* Initializes multilog */
int init_djbmultilog(logreader *lf);
void *read_djbmultilog(logreader *lf, int *rc, int drop_it);

/* Read events from output of command */
void *read_command(logreader *lf, int *rc, int drop_it);
void *read_fullcommand(logreader *lf, int *rc, int drop_it);

/* Read auditd events */
void *read_audit(logreader *lf, int *rc, int drop_it);

/* Read json events */
void *read_json(logreader *lf, int *rc, int drop_it);

#ifdef WIN32
void win_startel();
void win_readel();
void win_read_vista_sec();
int win_start_event_channel(char *evt_log, char future, char *query, int reconnect_time);
void win_format_event_string(char *string);
#endif

#ifndef WIN32
// Com request thread dispatcher
void * lccom_main(void * arg);
#endif
size_t lccom_dispatch(char * command, char ** output);
size_t lccom_getconfig(const char * section, char ** output);

/*** Global variables ***/
extern int loop_timeout;
extern int logr_queue;
extern int open_file_attempts;
extern logreader *logff;
extern logreader_glob *globs;
extern logsocket *logsk;
extern int vcheck_files;
extern int maximum_lines;
extern logsocket default_agent;
extern int force_reload;
extern int reload_interval;
extern int reload_delay;
extern int free_excluded_files_interval;

typedef enum {
    CONTINUE_IT,
    NEXT_IT,
    LEAVE_IT
} IT_control;

/* Message queue */
typedef struct w_msg_queue_t{
    w_queue_t *msg_queue;
    pthread_mutex_t mutex;
    pthread_cond_t available;
} w_msg_queue_t;


/* Hash table of queues */
extern OSHash * msg_queues_table;

/* Message structure */
typedef struct w_message_t {
    char *file;
    char *buffer;
    char queue_mq;
    unsigned int size;
    logtarget *log_target;
} w_message_t;


/* Input thread range */
typedef struct w_input_range_t{
    int start_i;
    int start_j;
    int end_i;
    int end_j;
} w_input_range_t;

///> Struct to save the position of last line read and the SHA1 hash content
typedef struct file_status {
    long offset;        ///> Position to read
    SHA_CTX context;    ///> It stores the hashed data calculated so far
    os_sha1 hash;       ///> Content file SHA1 hash
} os_file_status_t;

extern w_input_range_t *w_input_threads_range;

/* Init queue hash table */
void w_msg_hash_queues_init();

/* Add entry to queue hash table */
int w_msg_hash_queues_add_entry(const char *key);

/* Push message into the hash queue */
int w_msg_hash_queues_push(const char *str, char *file, unsigned long size, logtarget * targets, char queue_mq);

/* Pop message from the hash queue */
w_message_t * w_msg_hash_queues_pop(const char *key);

/* Push message into the queue */
int w_msg_queue_push(w_msg_queue_t * msg, const char * buffer, char *file, unsigned long size, logtarget * log_target, char queue_mq);

/* Pop message from the queue */
w_message_t * w_msg_queue_pop(w_msg_queue_t * queue);

/* Output processing thread*/
#ifdef WIN32
DWORD WINAPI w_output_thread(void * args);
#else
void * w_output_thread(void * args);
#endif

/* Prepare pool of output threads */
void w_create_output_threads();

/* Input processing thread */
#ifdef WIN32
DWORD WINAPI w_input_thread(__attribute__((unused)) void * t_id);
#else
void * w_input_thread(__attribute__((unused)) void * t_id);
#endif

/* Prepare pool of input threads */
void w_create_input_threads();

/* Set mutexes for each file */
void w_set_file_mutexes();

/* Read stop signal from reader threads */
int can_read();

/**
 * @brief Update the read position in file status hash table
 * @param path the path is the hash key
 * @param pos new read position
 * @param context SHA1 context.
 * @return 0 on succes, otherwise -1
 */
int w_update_file_status(const char * path, w_offset_t pos, SHA_CTX *context);

/**
 * @brief Get SHA1 context or inicilize it
 * @param path the path is the hash key
 * @param context SHA1 context.
 * @param poisition end file position.
 */
void w_get_hash_context (const char * path, SHA_CTX *context, w_offset_t position);

extern int sample_log_length;
extern int lc_debug_level;
extern int accept_remote;
extern int N_INPUT_THREADS;
extern int OUTPUT_QUEUE_SIZE;
#ifndef WIN32
extern rlim_t nofile;
#endif

#endif /* LOGREADER_H */
