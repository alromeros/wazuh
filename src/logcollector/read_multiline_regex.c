/* Copyright (C) 2015-2020, Wazuh Inc.
 * Copyright (C) 2009 Trend Micro Inc.
 * All right reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */

#include "shared.h"
#include "logcollector.h"

#ifdef WAZUH_UNIT_TESTING
// Remove STATIC qualifier from tests
#define STATIC
#else
#define STATIC static
#endif

/* Timeout functions */
STATIC bool multiline_ctxt_restore(char * buffer, int * readed_lines, w_multiline_ctxt_t * ctxt);
STATIC void multiline_ctxt_backup(char * buffer, int readed_lines, w_multiline_ctxt_t ** ctxt);
STATIC void multiline_ctxt_free(w_multiline_ctxt_t ** ctxt);
STATIC bool multiline_ctxt_is_expired(unsigned int timeout, w_multiline_ctxt_t * ctxt);
/**
 * @brief Get log from file with multiline log support.
 *
 * @param buffer readed log output
 * @param length max lenth
 * @param stream log file
 * @param ml_cfg multiline configuration
 * @return  if = 0 indicates no more logs available.
 *          if > 0 indicate log's lines count.
 */
STATIC int multiline_getlog(char * buffer, int length, FILE * stream, w_multiline_config_t * ml_cfg);

/**
 * @brief Get log from file with multiline log support using \ref ML_MATCH_START
 *
 * @param buffer readed log output
 * @param length max lenth
 * @param stream log file
 * @param ml_cfg multiline configuration
 * @return  if = 0 indicates no more logs available.
 *          if > 0 indicate log's lines count.
 */
STATIC int multiline_getlog_start(char * buffer, int length, FILE * stream, w_multiline_config_t * ml_cfg);

/**
 * @brief Get log from file with multiline log support using \ref ML_MATCH_END
 *
 * @param buffer readed log output
 * @param length max lenth
 * @param stream log file
 * @param ml_cfg multiline configuration
 * @return  if = 0 indicates no more logs available.
 *          if > 0 indicate log's lines count.
 */
STATIC int multiline_getlog_end(char * buffer, int length, FILE * stream, w_multiline_config_t * ml_cfg);

/**
 * @brief Get log from file with multiline log support using \ref ML_MATCH_ALL
 *
 * @param buffer readed log output
 * @param length max lenth
 * @param stream log file
 * @param ml_cfg multiline configuration
 * @return  if = 0 indicates no more logs available.
 *          if > 0 indicate log's lines count.
 */
STATIC int multiline_getlog_all(char * buffer, int length, FILE * stream, w_multiline_config_t * ml_cfg);

/* Misc functions */

/**
 * @brief If the last character of the string is an end of line, replace it.
 *
 * Replace the last character of `str` (only if it is an end of line) according to` type`.
 * if type is ML_REPLACE_NO_REPLACE does not replace the end of the line
 * if type is ML_REPLACE_NONE remove the end of the line
 * if type is ML_REPLACE_WSPACE replace the end of line with a blank space ' '
 * if type is ML_REPLACE_TAB replace the end of line with a tab character '\t'
 * @param str String to replace character.
 * @param type Replacement type
 */
STATIC void multiline_replace(char * str, w_multiline_replace_type_t type);

void * read_multiline_regex(logreader * lf, int * rc, int drop_it) {
    char read_buffer[OS_MAXSTR + 1];
    int count_lines = 0;
    int rlines;
    const int max_line_len = OS_MAXSTR - OS_LOG_HEADER;

    read_buffer[OS_MAXSTR] = '\0';
    *rc = 0;

    while (rlines = multiline_getlog(read_buffer, max_line_len, lf->fp, lf->multiline),
           rlines > 0 && (!maximum_lines || count_lines < maximum_lines)) {

        if (drop_it == 0) {
            w_msg_hash_queues_push(read_buffer, lf->file, strlen(read_buffer), lf->log_target, LOCALFILE_MQ);
        }
        count_lines += rlines;
    }

    return NULL;
}

STATIC int multiline_getlog(char * buffer, int length, FILE * stream, w_multiline_config_t * ml_cfg) {

    int readed_lines = 0;

    switch (ml_cfg->match_type) {
    case ML_MATCH_START:
        readed_lines = multiline_getlog_start(buffer, length, stream, ml_cfg);
        break;

    case ML_MATCH_END:
        readed_lines = multiline_getlog_end(buffer, length, stream, ml_cfg);
        break;

    case ML_MATCH_ALL:
        readed_lines = multiline_getlog_all(buffer, length, stream, ml_cfg);
        break;

    default:
        *buffer = '\0';
        break;
    }

    return readed_lines;
}

STATIC int multiline_getlog_start(char * buffer, int length, FILE * stream, w_multiline_config_t * ml_cfg) {

    char * str = buffer;
    char * retstr = NULL;
    int offset = 0;
    int chunk_sz = 0;
    bool collecting_lines = false;
    int readed_lines = 0;
    int c = 0;
    long pos = w_ftell(stream);
    *str = '\0';

    /* Check if a context restore is needed */
    if (ml_cfg->ctxt) {
        multiline_ctxt_restore(str, &readed_lines, ml_cfg->ctxt);
        offset = strlen(str);
        str += offset;
        collecting_lines = true;
        /* If the context it's expired then free it and return log */
        if (multiline_ctxt_is_expired(ml_cfg->timeout, ml_cfg->ctxt)) {
            multiline_ctxt_free(&ml_cfg->ctxt);
            /* delete last end-of-line character (LF / CR LF) */
            multiline_replace(buffer, ML_REPLACE_NONE);
            return readed_lines;
        }
    }

    while (can_read() && (retstr = fgets(str, length - offset, stream)) != NULL) {

        /* Check if current line match start regex */
        if (collecting_lines && w_expression_match(ml_cfg->regex, str, NULL, NULL)) {
            /* Rewind. This line dont belong to last log */
            buffer[offset] = '\0';
            multiline_replace(buffer, ML_REPLACE_NONE);
            fseek(stream, pos, SEEK_SET);
            break;
        }

        multiline_replace(str, ml_cfg->replace_type);
        chunk_sz = strlen(str);
        offset += chunk_sz;
        str += chunk_sz;
        readed_lines++;
        /* Save current posistion in case we have to rewind */
        pos = w_ftell(stream);
        collecting_lines = true;
        /* Allow save new content in the context in case can_read() fail */
        retstr = NULL;
        /* Avoid fgets infinite loop behauvior when size parameter is 1 */
        if (offset == length - 1) {
            break;
        }
    }

    /* Check if we have to save/create context in case
       Multiline log found but MAYBE not finished yet */
    if (collecting_lines && !retstr && length > offset + 1) {
        multiline_ctxt_backup(buffer, readed_lines, &ml_cfg->ctxt);
        readed_lines = 0;
    } else if (length == offset + 1) {
        // Discard the rest of the log, moving the pointer to the next end of line
        while (true) {
            c = fgetc(stream);
            if (c == '\n' || c == '\0' || c == EOF) {
                break;
            }
        }
    }

    /* If the lastest line complete the multiline log, free the context */
    if (ml_cfg->ctxt && readed_lines > 0) {
        multiline_ctxt_free(&ml_cfg->ctxt);
    }

    return readed_lines;
}

STATIC int multiline_getlog_end(char * buffer, int length, FILE * stream, w_multiline_config_t * ml_cfg) {
    char * str = buffer;
    char * retstr = NULL;
    int offset = 0;
    int chunk_sz = 0;
    bool collecting_lines = false;
    int readed_lines = 0;
    int c = 0;
    *str = '\0';

    /* Check if a context restore is needed */
    if (ml_cfg->ctxt) {
        multiline_ctxt_restore(str, &readed_lines, ml_cfg->ctxt);
        offset = strlen(str);
        str += offset;
        collecting_lines = true;
        /* If the context it's expired then free it and return log */
        if (multiline_ctxt_is_expired(ml_cfg->timeout, ml_cfg->ctxt)) {
            multiline_ctxt_free(&ml_cfg->ctxt);
            /* delete last end-of-line character (LF / CR LF) */
            multiline_replace(buffer, ML_REPLACE_NONE);
            return readed_lines;
        }
    }

    while (can_read() && (retstr = fgets(str, length - offset, stream)) != NULL) {

        readed_lines++;
        if (w_expression_match(ml_cfg->regex, str, NULL, NULL)) {
            multiline_replace(buffer, ML_REPLACE_NONE);
            collecting_lines = false;
            break;
        }
        multiline_replace(str, ml_cfg->replace_type);
        chunk_sz = strlen(str);
        offset += chunk_sz;
        str += chunk_sz;
        collecting_lines = true;
        /* Allow save new content in the context in case can_read() fail */
        retstr = NULL;
        /* Avoid fgets infinite loop behauvior when size parameter is 1 */
        if (offset == length - 1) {
            break;
        }
    }

    /* Check if we have to save/create context in case
       Multiline log found but not finished yet */
    if (collecting_lines && !retstr && length > offset + 1) {
        multiline_ctxt_backup(buffer, readed_lines, &ml_cfg->ctxt);
        readed_lines = 0;
    } else if (length == offset + 1) {
        // Discard the rest of the log, moving the pointer to the next end of line
        while (true) {
            c = fgetc(stream);
            if (c == '\n' || c == '\0' || c == EOF) {
                break;
            }
        }
    }

    /* If the lastest line complete the multiline log, free the context */
    if (ml_cfg->ctxt && readed_lines > 0) {
        multiline_ctxt_free(&ml_cfg->ctxt);
    }

    return readed_lines;
}

STATIC int multiline_getlog_all(char * buffer, int length, FILE * stream, w_multiline_config_t * ml_cfg) {

    char * str = buffer;
    *str = '\0';
    int offset = 0;
    int chunk_sz = 0;
    int readed_lines = 0;
    int c = 0;
    bool collecting_lines = false;
    char * retstr = NULL;

    /* Check if a context restore is needed */
    if (ml_cfg->ctxt) {
        multiline_ctxt_restore(str, &readed_lines, ml_cfg->ctxt);
        offset = strlen(str);
        str += offset;
        collecting_lines = true;
        /* If the context it's expired then free it and return log */
        if (multiline_ctxt_is_expired(ml_cfg->timeout, ml_cfg->ctxt)) {
            multiline_ctxt_free(&ml_cfg->ctxt);
            /* delete last end-of-line character (LF / CR LF) */
            multiline_replace(buffer, ML_REPLACE_NONE);
            return readed_lines;
        }
    }

    while (can_read() && (retstr = fgets(str, length - offset, stream)) != NULL) {

        readed_lines++;
        if (w_expression_match(ml_cfg->regex, buffer, NULL, NULL)) {
            multiline_replace(buffer, ML_REPLACE_NONE);
            collecting_lines = false;
            break;
        }

        multiline_replace(str, ml_cfg->replace_type);
        chunk_sz = strlen(str);
        offset += chunk_sz;
        str += chunk_sz;
        collecting_lines = true;
        /* Allow save new content in the context in case can_read() fail */
        retstr = NULL;
        /* Avoid fgets infinite loop behauvior when size parameter is 1 */
        if (offset == length - 1) {
            break;
        }
    }

    /* Check if we have to save/create context in case
       Multiline log found but not finished yet */
    if (collecting_lines && !retstr && length > offset + 1) {
        multiline_ctxt_backup(buffer, readed_lines, &ml_cfg->ctxt);
        readed_lines = 0;
    } else if (length == offset + 1) {
        // Discard the rest of the log, moving the pointer to the next end of line
        while (true) {
            c = fgetc(stream);
            if (c == '\n' || c == '\0' || c == EOF) {
                break;
            }
        }
    }

    /* If the lastest line complete the multiline log, free the context */
    if (ml_cfg->ctxt && readed_lines > 0) {
        multiline_ctxt_free(&ml_cfg->ctxt);
    }

    return readed_lines;
}

STATIC void multiline_replace(char * str, w_multiline_replace_type_t type) {

    const char newline = '\n';
    const char creturn = '\r';
    const char tab = '\t';
    const char wspace = ' ';
    char * pos_newline;
    char * pos_creturn;

    if (!str || str[0] == '\0') {
        return;
    }

    if (pos_newline = (str + strlen(str) - 1), *pos_newline != newline) {
        return;
    }

    pos_creturn = (strlen(str) > 1) && (*(pos_newline - 1) == creturn) ? (pos_newline - 1) : NULL;

    switch (type) {
    case ML_REPLACE_WSPACE:
        if (pos_creturn) {
            *pos_creturn = wspace;
            *pos_newline = '\0';
        } else {
            *pos_newline = wspace;
        }

        break;

    case ML_REPLACE_TAB:
        if (pos_creturn) {
            *pos_creturn = tab;
            *pos_newline = '\0';
        } else {
            *pos_newline = tab;
        }

        break;

    case ML_REPLACE_NONE:
        if (pos_creturn) {
            *pos_creturn = '\0';
        } else {
            *pos_newline = '\0';
        }
        break;

    default:
    case ML_REPLACE_NO_REPLACE:
        break;
    }
}

STATIC void multiline_ctxt_backup(char * buffer, int readed_lines, w_multiline_ctxt_t ** ctxt) {

    size_t current_bsize = strlen(buffer);

    if (*ctxt && (strlen((*ctxt)->buffer) == current_bsize)) {
        return;
    }

    if (*ctxt) {
        size_t old_size = strlen((*ctxt)->buffer);
        os_realloc((*ctxt)->buffer, sizeof(char) * (current_bsize + 1), (*ctxt)->buffer);
        strcpy((*ctxt)->buffer + old_size, buffer + old_size);

    } else {
        os_calloc(1, sizeof(w_multiline_ctxt_t), *ctxt);
        os_calloc(current_bsize + 1, sizeof(char), (*ctxt)->buffer);
        strcpy((*ctxt)->buffer, buffer);
    }

    (*ctxt)->lines_count = readed_lines;
    (*ctxt)->timestamp = time(NULL);
}

STATIC void multiline_ctxt_free(w_multiline_ctxt_t ** ctxt) {

    if (!(*ctxt)) {
        return;
    }
    if ((*ctxt)->buffer) {
        os_free((*ctxt)->buffer);
    }

    os_free(*ctxt);
}

STATIC bool multiline_ctxt_restore(char * buffer, int * readed_lines, w_multiline_ctxt_t * ctxt) {

    if (!ctxt) {
        return false;
    }
    strcpy(buffer, ctxt->buffer);
    *readed_lines = ctxt->lines_count;
    return true;
}

STATIC bool multiline_ctxt_is_expired(unsigned int timeout, w_multiline_ctxt_t * ctxt) {

    if (!ctxt) {
        return true;
    }

    if (time(NULL) - ctxt->timestamp > timeout) {
        return true;
    }

    return false;
}
