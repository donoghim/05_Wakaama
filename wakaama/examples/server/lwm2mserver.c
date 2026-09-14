/*******************************************************************************
 *
 * Copyright (c) 2013, 2014 Intel Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v20.html
 * The Eclipse Distribution License is available at
 *    http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    David Navarro, Intel Corporation - initial API and implementation
 *    domedambrosio - Please refer to git log
 *    Simon Bernard - Please refer to git log
 *    Toby Jaffey - Please refer to git log
 *    Julien Vermillard - Please refer to git log
 *    Bosch Software Innovations GmbH - Please refer to git log
 *    Christian Renz - Please refer to git log
 *    Scott Bertin, AMETEK, Inc. - Please refer to git log
 *
 *******************************************************************************/

/*
 Copyright (c) 2013, 2014 Intel Corporation

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

     * Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
     * Neither the name of Intel Corporation nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 THE POSSIBILITY OF SUCH DAMAGE.

 David Navarro <david.navarro@intel.com>

*/


#include "liblwm2m.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <inttypes.h>
#include <termios.h>

#include "er-coap-13/er-coap-13.h"

#include "commandline.h"
#include "connection.h"

#define MAX_PACKET_SIZE 2048
#define PRV_CMDLINE_MAX_LEN 512
#define PRV_CMDLINE_HISTORY_MAX 32
#define PRV_CONTROL_SOCKET_DEFAULT "/tmp/lwm2mserver-control.sock"
#define PRV_DFOTA_FW_ROOT_DEFAULT "dfota_fw"
#define PRV_DFOTA_URI_PREFIX_DEFAULT "/dfota_fw"
#define PRV_DFOTA_HOST_DEFAULT "115.90.109.11"

static const char * g_dfotaFwRoot = PRV_DFOTA_FW_ROOT_DEFAULT;
static const char * g_dfotaUriPrefix = PRV_DFOTA_URI_PREFIX_DEFAULT;
static const char * g_dfotaHost = PRV_DFOTA_HOST_DEFAULT;
static const char * g_localPort = LWM2M_STANDARD_PORT_STR;

typedef enum
{
    PRV_DFOTA_IDLE,
    PRV_DFOTA_WAIT_DOWNLOAD,
    PRV_DFOTA_WAIT_RESULT,
    PRV_DFOTA_WAIT_EXECUTE
} prv_dfota_state_t;

static prv_dfota_state_t g_dfotaState = PRV_DFOTA_IDLE;
static uint16_t g_dfotaClientId = LWM2M_MAX_ID;

static int g_quit = 0;
static struct termios g_old_termios;
static int g_raw_input = 0;
/* Set once at startup: whether stdin is an interactive terminal.
 * When stdin is not a TTY (e.g. run under nohup/systemd/redirected from
 * /dev/null), select() on it always reports "readable" (EOF), which would
 * otherwise turn the main loop into a CPU-spinning busy loop. In that case
 * we simply do not watch stdin at all and rely on the select() timeout. */
static int g_stdin_is_tty = 0;

typedef enum
{
    PRV_INPUT_NONE,
    PRV_INPUT_READY,
    PRV_INPUT_ERROR
} prv_input_result_t;

typedef struct
{
    char line[PRV_CMDLINE_MAX_LEN];
    size_t length;
    char history[PRV_CMDLINE_HISTORY_MAX][PRV_CMDLINE_MAX_LEN];
    int history_count;
    int history_pos;
} prv_input_state_t;

static void prv_restore_terminal(void)
{
    if (g_raw_input != 0)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_termios);
        g_raw_input = 0;
    }
}

static int prv_enable_raw_input(void)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO))
    {
        return 0;
    }

    if (tcgetattr(STDIN_FILENO, &g_old_termios) != 0)
    {
        return -1;
    }

    raw = g_old_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
    {
        return -1;
    }

    g_raw_input = 1;
    atexit(prv_restore_terminal);

    return 0;
}

static void prv_input_redraw(const prv_input_state_t * state)
{
    fprintf(stdout, "\r> %s\033[K", state->line);
    fflush(stdout);
}

static void prv_input_set_line(prv_input_state_t * state,
                               const char * line)
{
    strncpy(state->line, line, PRV_CMDLINE_MAX_LEN - 1);
    state->line[PRV_CMDLINE_MAX_LEN - 1] = 0;
    state->length = strlen(state->line);
    prv_input_redraw(state);
}

static void prv_input_add_history(prv_input_state_t * state,
                                  const char * line)
{
    if (line[0] == 0)
    {
        state->history_pos = state->history_count;
        return;
    }

    if (state->history_count > 0
     && strcmp(state->history[state->history_count - 1], line) == 0)
    {
        state->history_pos = state->history_count;
        return;
    }

    if (state->history_count == PRV_CMDLINE_HISTORY_MAX)
    {
        memmove(state->history[0], state->history[1],
                sizeof(state->history[0]) * (PRV_CMDLINE_HISTORY_MAX - 1));
        state->history_count--;
    }

    strncpy(state->history[state->history_count], line, PRV_CMDLINE_MAX_LEN - 1);
    state->history[state->history_count][PRV_CMDLINE_MAX_LEN - 1] = 0;
    state->history_count++;
    state->history_pos = state->history_count;
}

static int prv_read_escape_char(char * value)
{
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) <= 0)
    {
        return 0;
    }

    return read(STDIN_FILENO, value, 1) == 1;
}

static void prv_input_handle_escape(prv_input_state_t * state)
{
    char sequence[2];

    if (!prv_read_escape_char(&sequence[0])
     || !prv_read_escape_char(&sequence[1]))
    {
        return;
    }

    if (sequence[0] != '[')
    {
        return;
    }

    switch (sequence[1])
    {
    case 'A':
        if (state->history_count > 0 && state->history_pos > 0)
        {
            state->history_pos--;
            prv_input_set_line(state, state->history[state->history_pos]);
        }
        break;

    case 'B':
        if (state->history_pos < state->history_count - 1)
        {
            state->history_pos++;
            prv_input_set_line(state, state->history[state->history_pos]);
        }
        else if (state->history_pos < state->history_count)
        {
            state->history_pos = state->history_count;
            prv_input_set_line(state, "");
        }
        break;

    default:
        break;
    }
}

static prv_input_result_t prv_input_read(prv_input_state_t * state,
                                         char * command,
                                         size_t command_len)
{
    char value;

    if (read(STDIN_FILENO, &value, 1) != 1)
    {
        return PRV_INPUT_NONE;
    }

    if (value == '\r' || value == '\n')
    {
        fprintf(stdout, "\r\n");
        strncpy(command, state->line, command_len - 1);
        command[command_len - 1] = 0;
        prv_input_add_history(state, state->line);
        state->line[0] = 0;
        state->length = 0;
        return PRV_INPUT_READY;
    }

    if (value == 4 && state->length == 0)
    {
        g_quit = 1;
        return PRV_INPUT_NONE;
    }

    if (value == 27)
    {
        prv_input_handle_escape(state);
        return PRV_INPUT_NONE;
    }

    if (value == 127 || value == '\b')
    {
        if (state->length > 0)
        {
            state->length--;
            state->line[state->length] = 0;
            fprintf(stdout, "\b \b");
            fflush(stdout);
        }
        return PRV_INPUT_NONE;
    }

    if (isprint(value & 0xFF))
    {
        if (state->length < PRV_CMDLINE_MAX_LEN - 1)
        {
            state->line[state->length] = value;
            state->length++;
            state->line[state->length] = 0;
            fputc(value, stdout);
            fflush(stdout);
        }
        else
        {
            return PRV_INPUT_ERROR;
        }
    }

    return PRV_INPUT_NONE;
}

static void prv_print_error(uint8_t status)
{
    fprintf(stdout, "Error: ");
    print_status(stdout, status);
    fprintf(stdout, "\r\n");
}

static const char * prv_dump_version(lwm2m_version_t version)
{
    switch(version)
    {
    case VERSION_MISSING:
        return "Missing";
    case VERSION_UNRECOGNIZED:
        return "Unrecognized";
    case VERSION_1_0:
        return "1.0";
    case VERSION_1_1:
        return "1.1";
    default:
        return "";
    }
}

static void prv_dump_binding(lwm2m_binding_t binding)
{
    if(BINDING_UNKNOWN == binding)
    {
        fprintf(stdout, "\tbinding: \"Not specified\"\r\n");
    }
    else
    {
        const struct bindingTable
        {
            lwm2m_binding_t binding;
            const char *text;
        } bindingTable[] =
        {
            { BINDING_U, "UDP" },
            { BINDING_T, "TCP" },
            { BINDING_S, "SMS" },
            { BINDING_N, "Non-IP" },
            { BINDING_Q, "queue mode" },
        };
        size_t i;
        bool oneSeen = false;
        fprintf(stdout, "\tbinding: \"");
        for (i = 0; i < sizeof(bindingTable) / sizeof(bindingTable[0]); i++)
        {
            if ((binding & bindingTable[i].binding) != 0)
            {
                if (oneSeen)
                {
                    fprintf(stdout, ", %s", bindingTable[i].text);
                }
                else
                {
                    fprintf(stdout, "%s", bindingTable[i].text);
                    oneSeen = true;
                }
            }
        }
        fprintf(stdout, "\"\r\n");
    }
}

static void prv_dump_client(lwm2m_client_t * targetP)
{
    lwm2m_client_object_t * objectP;

    fprintf(stdout, "Client #%d:\r\n", targetP->internalID);
    fprintf(stdout, "\tname: \"%s\"\r\n", targetP->name);
    fprintf(stdout, "\tversion: \"%s\"\r\n", prv_dump_version(targetP->version));
    prv_dump_binding(targetP->binding);
    if (targetP->msisdn) fprintf(stdout, "\tmsisdn: \"%s\"\r\n", targetP->msisdn);
    if (targetP->altPath) fprintf(stdout, "\talternative path: \"%s\"\r\n", targetP->altPath);
    fprintf(stdout, "\tformat: %d \r\n", targetP->format);
    fprintf(stdout, "\tlifetime: %d sec\r\n", targetP->lifetime);
    fprintf(stdout, "\tobsId: %d sec\r\n", targetP->observationId);
    fprintf(stdout, "\tobjects: ");
    for (objectP = targetP->objectList; objectP != NULL ; objectP = objectP->next)
    {
        if (objectP->instanceList == NULL)
        {
            if (objectP->versionMajor != 0 || objectP->versionMinor != 0)
            {
                fprintf(stdout, "/%d (%u.%u), ", objectP->id, objectP->versionMajor, objectP->versionMinor);
            }
            else
            {
                fprintf(stdout, "/%d, ", objectP->id);
            }
        }
        else
        {
            lwm2m_list_t * instanceP;

            if (objectP->versionMajor != 0 || objectP->versionMinor != 0)
            {
                fprintf(stdout, "/%d (%u.%u), ", objectP->id, objectP->versionMajor, objectP->versionMinor);
            }

            for (instanceP = objectP->instanceList; instanceP != NULL ; instanceP = instanceP->next)
            {
                fprintf(stdout, "/%d/%d, ", objectP->id, instanceP->id);
            }
        }
    }
    fprintf(stdout, "\r\n");
}

static void prv_output_clients(lwm2m_context_t *lwm2mH,
                               char * buffer,
                               void * user_data)
{
    lwm2m_client_t * targetP;

    /* unused parameter */
    (void)user_data;

    targetP = lwm2mH->clientList;

    if (targetP == NULL)
    {
        fprintf(stdout, "No client.\r\n");
        return;
    }

    for (targetP = lwm2mH->clientList ; targetP != NULL ; targetP = targetP->next)
    {
        prv_dump_client(targetP);
    }
}

static int prv_read_id(char * buffer,
                       uint16_t * idP)
{
    int nb;
    int value;

    nb = sscanf(buffer, "%d", &value);
    if (nb == 1)
    {
        if (value < 0 || value > LWM2M_MAX_ID)
        {
            nb = 0;
        }
        else
        {
            *idP = value;
        }
    }

    return nb;
}

static void prv_printUri(const lwm2m_uri_t * uriP)
{
    fprintf(stdout, "/%d", uriP->objectId);
    if (LWM2M_URI_IS_SET_INSTANCE(uriP))
        fprintf(stdout, "/%d", uriP->instanceId);
    else if (LWM2M_URI_IS_SET_RESOURCE(uriP))
        fprintf(stdout, "/");
    if (LWM2M_URI_IS_SET_RESOURCE(uriP))
            fprintf(stdout, "/%d", uriP->resourceId);
#ifndef LWM2M_VERSION_1_0
    else if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
        fprintf(stdout, "/");
    if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
            fprintf(stdout, "/%d", uriP->resourceInstanceId);
#endif
}

static bool prv_uri_is_resource(lwm2m_uri_t * uriP,
                                uint16_t objectId,
                                uint16_t instanceId,
                                uint16_t resourceId)
{
    return uriP != NULL
        && LWM2M_URI_IS_SET_OBJECT(uriP)
        && LWM2M_URI_IS_SET_INSTANCE(uriP)
        && LWM2M_URI_IS_SET_RESOURCE(uriP)
        && uriP->objectId == objectId
        && uriP->instanceId == instanceId
        && uriP->resourceId == resourceId;
}

static bool prv_decode_single_byte_resource(uint8_t * data,
                                            size_t dataLength,
                                            uint16_t resourceId,
                                            uint8_t * value)
{
    if (data == NULL || value == NULL)
    {
        return false;
    }
    if (dataLength == 1)
    {
        *value = data[0];
        return true;
    }
    if (dataLength == 3 && data[0] == 0xC1 && data[1] == resourceId)
    {
        *value = data[2];
        return true;
    }

    return false;
}

static void prv_result_callback(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, int status,
                                block_info_t *block_info, lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                void *userData) {
    /* unused parameters */
    (void)userData;
    (void)format;

    fprintf(stdout, "\r\nClient #%d ", clientID);
    prv_printUri(uriP);
    fprintf(stdout, " : ");
    print_status(stdout, status);
    fprintf(stdout, "\r\n");

    output_data(stdout, block_info, format, data, dataLength, 1);

    if (g_dfotaState == PRV_DFOTA_WAIT_RESULT
     && clientID == g_dfotaClientId
     && status == COAP_205_CONTENT
     && prv_uri_is_resource(uriP, 5, 0, 5))
    {
        uint8_t updateResult;

        if (prv_decode_single_byte_resource(data, dataLength, 5, &updateResult) && updateResult == 0)
        {
            lwm2m_uri_t updateUri;
            int result;

            result = lwm2m_stringToUri("/5/0/2", strlen("/5/0/2"), &updateUri);
            if (result != 0)
            {
                fprintf(stdout, "\r\nDFOTA result is 0, executing /5/0/2.\r\n");
                result = lwm2m_dm_execute(contextP, clientID, &updateUri, 0, NULL, 0, prv_result_callback, NULL);
                if (result == 0)
                {
                    g_dfotaState = PRV_DFOTA_WAIT_EXECUTE;
                }
                else
                {
                    prv_print_error(result);
                    g_dfotaState = PRV_DFOTA_IDLE;
                }
            }
        }
        else
        {
            fprintf(stdout, "\r\nDFOTA update result is not 0; execute skipped.\r\n");
            g_dfotaState = PRV_DFOTA_IDLE;
        }
    }
    else if (g_dfotaState == PRV_DFOTA_WAIT_EXECUTE
          && clientID == g_dfotaClientId
          && prv_uri_is_resource(uriP, 5, 0, 2))
    {
        g_dfotaState = PRV_DFOTA_IDLE;
    }

    fprintf(stdout, "\r\n> ");
    fflush(stdout);
}

static void prv_notify_callback(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, int count,
                                block_info_t *block_info, lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                void *userData) {
    /* unused parameters */
    (void)userData;
    (void)format;

    fprintf(stdout, "\r\nNotify from client #%d ", clientID);
    prv_printUri(uriP);
    fprintf(stdout, " number %d\r\n", count);

    output_data(stdout, block_info, format, data, dataLength, 1);

    if (g_dfotaState == PRV_DFOTA_WAIT_DOWNLOAD
     && clientID == g_dfotaClientId
     && prv_uri_is_resource(uriP, 5, 0, 3))
    {
        uint8_t firmwareState;

        if (prv_decode_single_byte_resource(data, dataLength, 3, &firmwareState) && firmwareState == 2)
        {
            lwm2m_uri_t resultUri;
            int result;

            result = lwm2m_stringToUri("/5/0/5", strlen("/5/0/5"), &resultUri);
            if (result != 0)
            {
                fprintf(stdout, "\r\nDFOTA download completed, reading /5/0/5.\r\n");
                result = lwm2m_dm_read(contextP, clientID, &resultUri, prv_result_callback, NULL);
                if (result == 0)
                {
                    g_dfotaState = PRV_DFOTA_WAIT_RESULT;
                }
                else
                {
                    prv_print_error(result);
                    g_dfotaState = PRV_DFOTA_IDLE;
                }
            }
        }
    }

    fprintf(stdout, "\r\n> ");
    fflush(stdout);
}

static void prv_read_client(lwm2m_context_t * lwm2mH,
                            char * buffer,
                            void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char* end = NULL;
    int result;

    /* unused parameters */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    if (!check_end_of_args(end)) goto syntax_error;

    result = lwm2m_dm_read(lwm2mH, clientId, &uri,  prv_result_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

static void prv_discover_client(lwm2m_context_t * lwm2mH,
                                char * buffer,
                                void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char* end = NULL;
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    if (!check_end_of_args(end)) goto syntax_error;

    result = lwm2m_dm_discover(lwm2mH, clientId, &uri, prv_result_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

// #define HARD_CODED_DL_DATA_FOR_KT // young added for KT
static void prv_do_write_client(char * buffer,
                                lwm2m_context_t * lwm2mH,
                                bool partialUpdate)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    lwm2m_data_t * dataP = NULL;
    int count = 0;
    char * end = NULL;

#if HARD_CODED_DL_DATA_FOR_KT
    // TLV format of string "test_message"
    uint8_t dlTlvDat[] = {0xc8,0x00,0x0c,0x74,0x65,0x73,0x74,0x5f,0x6d,0x65,0x73,0x73,0x61,0x67,0x65};
    size_t tlvLen = 15; // TLV data length
    // LOG_ARG("pbuf: %s", buffer);
    // LOG_ARG("tlv: %s", &dlTlvDat[3]);
#endif

    int result;


    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0) goto syntax_error;

    if (!check_end_of_args(end)) goto syntax_error;

#ifdef LWM2M_SUPPORT_SENML_JSON
    if (count <= 0)
    {
        count = lwm2m_data_parse(&uri, (uint8_t *)buffer, end - buffer, LWM2M_CONTENT_SENML_JSON, &dataP);
    }
#endif
#ifdef LWM2M_SUPPORT_JSON
    if (count <= 0)
    {
        count = lwm2m_data_parse(&uri, (uint8_t *)buffer, end - buffer, LWM2M_CONTENT_JSON, &dataP);
    }
#endif
    if (count > 0)
    {
        lwm2m_client_t * clientP = NULL;
        clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)lwm2mH->clientList, clientId);
        if (clientP != NULL)
        {
            lwm2m_media_type_t format = clientP->format;
            uint8_t *serialized;
            int length = lwm2m_data_serialize(&uri,
                                              count,
                                              dataP,
                                              &format,
                                              &serialized);
            if (length > 0)
            {
                result = lwm2m_dm_write(lwm2mH,
                                        clientId,
                                        &uri,
                                        format,
                                        serialized,
                                        length,
                                        partialUpdate,
                                        prv_result_callback,
                                        NULL);
                lwm2m_free(serialized);
            }
            else
            {
                result = COAP_500_INTERNAL_SERVER_ERROR;
            }
        }
        else
        {
            result = COAP_404_NOT_FOUND;
        }
        lwm2m_data_free(count, dataP);
    }
    else if(!partialUpdate)
    {
        result = lwm2m_dm_write(lwm2mH,
                                clientId,
                                &uri,
#if HARD_CODED_DL_DATA_FOR_KT
                                LWM2M_CONTENT_TLV, // young change
                                dlTlvDat, // young change
                                tlvLen, // young change
#else
                                LWM2M_CONTENT_TEXT, // org
                                (uint8_t *)buffer, // org
                                end - buffer, // org
#endif
                                partialUpdate,
                                prv_result_callback,
                                NULL);
    }
    else
    {
        goto syntax_error;
    }

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

static void prv_write_client(lwm2m_context_t * lwm2mH,
                             char * buffer,
                             void * user_data)
{
    /* unused parameter */
    (void)user_data;

    prv_do_write_client(buffer, lwm2mH, false);
}

static void prv_dfota_client(lwm2m_context_t * lwm2mH,
                             char * buffer,
                             void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char * end = NULL;
    char packageUri[PRV_CMDLINE_MAX_LEN];
    char command[PRV_CMDLINE_MAX_LEN];
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0 || strchr(buffer, '/') != NULL || strstr(buffer, "..") != NULL) goto syntax_error;
    if (!check_end_of_args(end)) goto syntax_error;

    result = snprintf(packageUri,
                      sizeof(packageUri),
                      "coap://%s:%s%s/%s",
                      g_dfotaHost,
                      g_localPort,
                      g_dfotaUriPrefix,
                      buffer);
    if (result < 0 || result >= (int)sizeof(packageUri))
    {
        fprintf(stdout, "Package URI too long !");
        return;
    }

    result = lwm2m_stringToUri("/5/0/3", strlen("/5/0/3"), &uri);
    if (result == 0) goto syntax_error;

    result = lwm2m_observe(lwm2mH, clientId, &uri, prv_notify_callback, NULL);
    if (result != 0)
    {
        prv_print_error(result);
        return;
    }

    result = snprintf(command, sizeof(command), "%u /5/0/1 %s", clientId, packageUri);
    if (result < 0 || result >= (int)sizeof(command))
    {
        fprintf(stdout, "Command too long !");
        return;
    }

    fprintf(stdout, "Package URI: %s\r\n", packageUri);
    g_dfotaClientId = clientId;
    g_dfotaState = PRV_DFOTA_WAIT_DOWNLOAD;
    prv_do_write_client(command, lwm2mH, false);
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

static int prv_send_dfota_response(connection_t * connP,
                                   coap_packet_t * request,
                                   uint8_t code,
                                   uint32_t blockNum,
                                   uint8_t more,
                                   uint16_t blockSize,
                                   const uint8_t * payload,
                                   size_t payloadLen)
{
    coap_packet_t response[1];
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t length;

    if (request->type == COAP_TYPE_CON)
    {
        coap_init_message(response, COAP_TYPE_ACK, code, request->mid);
    }
    else
    {
        coap_init_message(response, COAP_TYPE_NON, code, coap_get_mid());
    }

    if (request->token_len != 0)
    {
        coap_set_header_token(response, request->token, request->token_len);
    }

    if (code == COAP_205_CONTENT)
    {
        coap_set_header_content_type(response, APPLICATION_OCTET_STREAM);
        coap_set_header_block2(response, blockNum, more, blockSize);
    }
    coap_set_payload(response, payload, payloadLen);

    length = coap_serialize_message(response, buffer);
    if (length == 0)
    {
        return -1;
    }

    return connection_send(connP, buffer, length);
}

static int prv_handle_dfota_request(connection_t * connP,
                                    uint8_t * buffer,
                                    size_t length)
{
    coap_packet_t request[1];
    char * path;
    const char * filename;
    char filePath[PRV_CMDLINE_MAX_LEN];
    uint32_t blockNum = 0;
    uint32_t blockOffset = 0;
    uint16_t blockSize = lwm2m_get_coap_block_size();
    uint8_t payload[MAX_PACKET_SIZE];
    size_t payloadLen;
    long fileSize;
    FILE * file;
    int result;

    if (coap_parse_message(request, buffer, (uint16_t)length) != NO_ERROR)
    {
        return 0;
    }
    if (request->code != COAP_GET || !IS_OPTION(request, COAP_OPTION_URI_PATH))
    {
        coap_free_header(request);
        return 0;
    }

    path = coap_get_multi_option_as_path_string(request->uri_path);
    if (path == NULL)
    {
        coap_free_header(request);
        return 0;
    }

    if (strncmp(path, g_dfotaUriPrefix, strlen(g_dfotaUriPrefix)) != 0
     || path[strlen(g_dfotaUriPrefix)] != '/')
    {
        lwm2m_free(path);
        coap_free_header(request);
        return 0;
    }

    filename = path + strlen(g_dfotaUriPrefix) + 1;
    if (filename[0] == 0 || strchr(filename, '/') != NULL || strstr(filename, "..") != NULL)
    {
        prv_send_dfota_response(connP, request, COAP_400_BAD_REQUEST, 0, 0, blockSize, NULL, 0);
        lwm2m_free(path);
        coap_free_header(request);
        return 1;
    }

    result = snprintf(filePath, sizeof(filePath), "%s/%s", g_dfotaFwRoot, filename);
    if (result < 0 || result >= (int)sizeof(filePath))
    {
        prv_send_dfota_response(connP, request, COAP_400_BAD_REQUEST, 0, 0, blockSize, NULL, 0);
        lwm2m_free(path);
        coap_free_header(request);
        return 1;
    }

    file = fopen(filePath, "rb");
    if (file == NULL)
    {
        prv_send_dfota_response(connP, request, COAP_404_NOT_FOUND, 0, 0, blockSize, NULL, 0);
        lwm2m_free(path);
        coap_free_header(request);
        return 1;
    }

    if (coap_get_header_block2(request, &blockNum, NULL, &blockSize, &blockOffset) == 0)
    {
        blockSize = lwm2m_get_coap_block_size();
        blockOffset = 0;
    }
    if (blockSize > sizeof(payload))
    {
        blockSize = sizeof(payload);
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        prv_send_dfota_response(connP, request, COAP_500_INTERNAL_SERVER_ERROR, 0, 0, blockSize, NULL, 0);
        lwm2m_free(path);
        coap_free_header(request);
        return 1;
    }
    fileSize = ftell(file);
    if (fileSize < 0 || blockOffset >= (uint32_t)fileSize)
    {
        fclose(file);
        prv_send_dfota_response(connP, request, COAP_402_BAD_OPTION, blockNum, 0, blockSize, NULL, 0);
        lwm2m_free(path);
        coap_free_header(request);
        return 1;
    }
    if (fseek(file, (long)blockOffset, SEEK_SET) != 0)
    {
        fclose(file);
        prv_send_dfota_response(connP, request, COAP_500_INTERNAL_SERVER_ERROR, 0, 0, blockSize, NULL, 0);
        lwm2m_free(path);
        coap_free_header(request);
        return 1;
    }

    payloadLen = fread(payload, 1, blockSize, file);
    if (ferror(file))
    {
        fclose(file);
        prv_send_dfota_response(connP, request, COAP_500_INTERNAL_SERVER_ERROR, 0, 0, blockSize, NULL, 0);
        lwm2m_free(path);
        coap_free_header(request);
        return 1;
    }
    fclose(file);

    prv_send_dfota_response(connP,
                            request,
                            COAP_205_CONTENT,
                            blockNum,
                            blockOffset + payloadLen < (uint32_t)fileSize,
                            blockSize,
                            payload,
                            payloadLen);
    fprintf(stdout, "\r\nDFOTA GET %s block=%u offset=%u len=%zu more=%u\r\n> ",
            filePath,
            blockNum,
            blockOffset,
            payloadLen,
            blockOffset + payloadLen < (uint32_t)fileSize);
    fflush(stdout);

    lwm2m_free(path);
    coap_free_header(request);
    return 1;
}

static int prv_create_control_socket(const char *path)
{
    struct sockaddr_un address;
    int socket_fd;

    if (strlen(path) >= sizeof(address.sun_path))
    {
        fprintf(stderr, "Control socket path is too long.\r\n");
        return -1;
    }

    socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (socket_fd < 0)
    {
        perror("control socket");
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1);
    unlink(path);
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        perror("control bind");
        close(socket_fd);
        return -1;
    }
    if (chmod(path, S_IRUSR | S_IWUSR) != 0)
    {
        perror("control chmod");
        close(socket_fd);
        unlink(path);
        return -1;
    }

    return socket_fd;
}

static void prv_handle_control_request(lwm2m_context_t *lwm2mH, int socket_fd)
{
    char request[PRV_CMDLINE_MAX_LEN];
    char command[PRV_CMDLINE_MAX_LEN];
    char *command_name;
    char *client_id;
    char *data;
    char *filename;
    char *save;
    char *uri;
    ssize_t length;

    length = recv(socket_fd, request, sizeof(request) - 1, 0);
    if (length < 0)
    {
        perror("control recv");
        return;
    }
    request[length] = '\0';

    save = NULL;
    command_name = strtok_r(request, "\t", &save);
    client_id = strtok_r(NULL, "\t", &save);
    if (command_name != NULL && strcmp(command_name, "DFOTA") == 0)
    {
        filename = strtok_r(NULL, "\t", &save);
        if (client_id == NULL || filename == NULL || save == NULL || save[0] != '\0'
            || snprintf(command, sizeof(command), "%s %s", client_id, filename) >= (int)sizeof(command))
        {
            fprintf(stderr, "Invalid control request.\r\n");
            return;
        }
        prv_dfota_client(lwm2mH, command, NULL);
        fprintf(stdout, "\r\n> ");
        fflush(stdout);
        return;
    }

    uri = strtok_r(NULL, "\t", &save);
    data = save;
    if (command_name == NULL || strcmp(command_name, "WRITE") != 0
        || client_id == NULL || uri == NULL || data == NULL || data[0] == '\0'
        || snprintf(command, sizeof(command), "%s %s %s", client_id, uri, data) >= (int)sizeof(command))
    {
        fprintf(stderr, "Invalid control request.\r\n");
        return;
    }

    prv_do_write_client(command, lwm2mH, false);
    fprintf(stdout, "\r\n> ");
    fflush(stdout);
}

static void prv_update_client(lwm2m_context_t * lwm2mH,
                              char * buffer,
                              void * user_data)
{
    /* unused parameter */
    (void)user_data;

    prv_do_write_client(buffer, lwm2mH, true);
}

static void prv_time_client(lwm2m_context_t * lwm2mH,
                            char * buffer,
                            void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char * end = NULL;
    int result;
    lwm2m_attributes_t attr;
    int nb;
    int value;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    memset(&attr, 0, sizeof(lwm2m_attributes_t));
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0) goto syntax_error;

    nb = sscanf(buffer, "%d", &value);
    if (nb != 1) goto syntax_error;
    if (value < 0) goto syntax_error;
    attr.minPeriod = value;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0) goto syntax_error;

    nb = sscanf(buffer, "%d", &value);
    if (nb != 1) goto syntax_error;
    if (value < 0) goto syntax_error;
    attr.maxPeriod = value;

    if (!check_end_of_args(end)) goto syntax_error;

    result = lwm2m_dm_write_attributes(lwm2mH, clientId, &uri, &attr, prv_result_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}


static void prv_attr_client(lwm2m_context_t *lwm2mH,
                            char * buffer,
                            void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char * end = NULL;
    int result;
    lwm2m_attributes_t attr;
    int nb;
    float value;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    memset(&attr, 0, sizeof(lwm2m_attributes_t));
    attr.toSet = LWM2M_ATTR_FLAG_LESS_THAN | LWM2M_ATTR_FLAG_GREATER_THAN;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0) goto syntax_error;

    nb = sscanf(buffer, "%f", &value);
    if (nb != 1) goto syntax_error;
    attr.lessThan = value;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0) goto syntax_error;

    nb = sscanf(buffer, "%f", &value);
    if (nb != 1) goto syntax_error;
    attr.greaterThan = value;

    buffer = get_next_arg(end, &end);
    if (buffer[0] != 0)
    {
        nb = sscanf(buffer, "%f", &value);
        if (nb != 1) goto syntax_error;
        attr.step = value;

        attr.toSet |= LWM2M_ATTR_FLAG_STEP;
    }

    if (!check_end_of_args(end)) goto syntax_error;

    result = lwm2m_dm_write_attributes(lwm2mH, clientId, &uri, &attr, prv_result_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}


static void prv_clear_client(lwm2m_context_t *lwm2mH,
                             char * buffer,
                             void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char * end = NULL;
    int result;
    lwm2m_attributes_t attr;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    memset(&attr, 0, sizeof(lwm2m_attributes_t));
    attr.toClear = LWM2M_ATTR_FLAG_LESS_THAN | LWM2M_ATTR_FLAG_GREATER_THAN | LWM2M_ATTR_FLAG_STEP | LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD ;

    buffer = get_next_arg(end, &end);
    if (!check_end_of_args(end)) goto syntax_error;

    result = lwm2m_dm_write_attributes(lwm2mH, clientId, &uri, &attr, prv_result_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}


static void prv_exec_client(lwm2m_context_t *lwm2mH,
                            char * buffer,
                            void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char * end = NULL;
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    buffer = get_next_arg(end, &end);


    if (buffer[0] == 0)
    {
        result = lwm2m_dm_execute(lwm2mH, clientId, &uri, 0, NULL, 0, prv_result_callback, NULL);
    }
    else
    {
        if (!check_end_of_args(end)) goto syntax_error;

        result = lwm2m_dm_execute(lwm2mH, clientId, &uri, LWM2M_CONTENT_TEXT, (uint8_t *)buffer, end - buffer, prv_result_callback, NULL);
    }

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

static void prv_create_client(lwm2m_context_t *lwm2mH,
                              char * buffer,
                              void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char * end = NULL;
    int result;
    int64_t value;
    lwm2m_data_t * dataP = NULL;
    int size = 0;

    /* unused parameter */
    (void)user_data;

    //Get Client ID
    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    //Get Uri
    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;
    if (LWM2M_URI_IS_SET_RESOURCE(&uri)) goto syntax_error;

    //Get Data to Post
    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0) goto syntax_error;

    if (!check_end_of_args(end)) goto syntax_error;

   // TLV

#ifdef LWM2M_SUPPORT_SENML_JSON
    if (size <= 0)
    {
        size = lwm2m_data_parse(&uri,
                                (uint8_t *)buffer,
                                end - buffer,
                                LWM2M_CONTENT_SENML_JSON,
                                &dataP);
    }
#endif
#ifdef LWM2M_SUPPORT_JSON
    if (size <= 0)
    {
        size = lwm2m_data_parse(&uri,
                                (uint8_t *)buffer,
                                end - buffer,
                                LWM2M_CONTENT_JSON,
                                &dataP);
    }
#endif
   /* Client dependent part   */

    if (size <= 0 && uri.objectId == 31024)
    {
        if (1 != sscanf(buffer, "%"PRId64, &value))
        {
            fprintf(stdout, "Invalid value !");
            return;
        }

        size = 1;
        dataP = lwm2m_data_new(size);
        if (dataP == NULL)
        {
            fprintf(stdout, "Allocation error !");
            return;
        }
        lwm2m_data_encode_int(value, dataP);
        dataP->id = 1;
    }
   /* End Client dependent part*/

    if (size <= 0) {
        goto syntax_error;
    }

    if (LWM2M_URI_IS_SET_INSTANCE(&uri))
    {
        /* URI is only allowed to have the object ID. Wrap the instance in an
         * object instance to get it to the client. */
        int count = size;
        lwm2m_data_t * subDataP = dataP;
        size = 1;
        dataP = lwm2m_data_new(size);
        if (dataP == NULL)
        {
            fprintf(stdout, "Allocation error !");
            lwm2m_data_free(count, subDataP);
            return;
        }
        lwm2m_data_include(subDataP, count, dataP);
        dataP->type = LWM2M_TYPE_OBJECT_INSTANCE;
        dataP->id = uri.instanceId;
        uri.instanceId = LWM2M_MAX_ID;
    }

    //Create
    result = lwm2m_dm_create(lwm2mH, clientId, &uri, size, dataP, prv_result_callback, NULL);
    lwm2m_data_free(size, dataP);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

static void prv_delete_client(lwm2m_context_t *lwm2mH,
                              char * buffer,
                              void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char* end = NULL;
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    if (!check_end_of_args(end)) goto syntax_error;

    result = lwm2m_dm_delete(lwm2mH, clientId, &uri, prv_result_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

static void prv_observe_client(lwm2m_context_t *lwm2mH,
                               char * buffer,
                               void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char* end = NULL;
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    if (!check_end_of_args(end)) goto syntax_error;

    result = lwm2m_observe(lwm2mH, clientId, &uri, prv_notify_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

static lwm2m_context_t *slwm2mCtx;

void observeObj_10250(uint16_t clientId) // young added for LGU
{
    char uriStr[] = "/10250/0/0";
    lwm2m_uri_t uri;
    int result;

    result = lwm2m_stringToUri(uriStr, strlen(uriStr), &uri);
    if (result == 0) return;

    result = lwm2m_observe(slwm2mCtx, clientId, &uri, prv_notify_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

}

void observeObj_26241(uint16_t clientId) // young added for LGU
{
    char uriStr[] = "/26241/0/0";
    lwm2m_uri_t uri;
    int result;

    result = lwm2m_stringToUri(uriStr, strlen(uriStr), &uri);
    if (result == 0) return;

    result = lwm2m_observe(slwm2mCtx, clientId, &uri, prv_notify_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

}

static void prv_cancel_client(lwm2m_context_t *lwm2mH,
                              char * buffer,
                              void * user_data)
{
    uint16_t clientId;
    lwm2m_uri_t uri;
    char* end = NULL;
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1) goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0) goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0) goto syntax_error;

    if (!check_end_of_args(end)) goto syntax_error;

    result = lwm2m_observe_cancel(lwm2mH, clientId, &uri, prv_result_callback, NULL);

    if (result == 0)
    {
        fprintf(stdout, "OK");
    }
    else
    {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

static void prv_monitor_callback(lwm2m_context_t *lwm2mH, uint16_t clientID, lwm2m_uri_t *uriP, int status,
                                 block_info_t *block_info, lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                 void *userData) {
    lwm2m_client_t * targetP;

    /* unused parameter */
    (void)userData;

    switch (status)
    {
    case COAP_201_CREATED:
        fprintf(stdout, "\r\n New client #%d registered.\r\n", clientID);
        targetP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)lwm2mH->clientList, clientID);
        prv_dump_client(targetP);
                slwm2mCtx = lwm2mH;
                observeObj_10250(clientID);
                observeObj_26241(clientID);
        break;

    case COAP_202_DELETED:
        fprintf(stdout, "\r\n Client #%d unregistered.\r\n", clientID);
        break;

    case COAP_204_CHANGED:
        fprintf(stdout, "\r\n Client #%d updated.\r\n", clientID);
        targetP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)lwm2mH->clientList, clientID);
        prv_dump_client(targetP);
        break;

    default:
        fprintf(stdout, "\r\n Monitor callback called with an unknown status: %d.\r\n", status);
        break;
    }

    fprintf(stdout, "\r\n> ");
    fflush(stdout);
}

static void prv_quit(lwm2m_context_t *lwm2mH,
                     char * buffer,
                     void * user_data)
{
    /* unused parameters */
    (void)lwm2mH;
    (void)user_data;

    g_quit = 1;
}

void handle_sigint(int signum)
{
    g_quit = 2;
}

void print_usage(void)
{
    fprintf(stderr, "Usage: lwm2mserver [OPTION]\r\n");
    fprintf(stderr, "Launch a LWM2M server on localhost.\r\n\n");
    fprintf(stdout, "Options:\r\n");
    fprintf(stdout, "  -4\t\tUse IPv4 connection. Default: IPv6 connection\r\n");
    fprintf(stdout, "  -l PORT\tSet the local UDP port of the Server. Default: "LWM2M_STANDARD_PORT_STR"\r\n");
    fprintf(stdout, "  -p PATH\tSet Unix control socket path. Default: "PRV_CONTROL_SOCKET_DEFAULT"\r\n");
    fprintf(stdout, "  -F DIR\tSet DFOTA firmware directory. Default: "PRV_DFOTA_FW_ROOT_DEFAULT"\r\n");
    fprintf(stdout, "  -u PATH\tSet DFOTA URI prefix. Default: "PRV_DFOTA_URI_PREFIX_DEFAULT"\r\n");
    fprintf(stdout, "  -H HOST\tSet DFOTA Package URI host/IP. Default: "PRV_DFOTA_HOST_DEFAULT"\r\n");
    fprintf(stdout, "  -S BYTES\tCoAP block size. Options: 16, 32, 64, 128, 256, 512, 1024. Default: %" PRIu16 "\r\n",
            (uint16_t)LWM2M_COAP_DEFAULT_BLOCK_SIZE);
    fprintf(stdout, "\r\n");
}

int main(int argc, char *argv[])
{
    int sock;
    int control_socket;
    fd_set readfds;
    struct timeval tv;
    int result;
    lwm2m_context_t * lwm2mH = NULL;
    connection_t * connList = NULL;
    int addressFamily = AF_INET6;
    int opt;
    const char * localPort = LWM2M_STANDARD_PORT_STR;
    const char * controlSocketPath = PRV_CONTROL_SOCKET_DEFAULT;
    prv_input_state_t inputState = {0};

    command_desc_t commands[] =
    {
            {"list", "List registered clients.", NULL, prv_output_clients, NULL},
            {"read", "Read from a client.", " read CLIENT# URI\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to read such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                                            "Result will be displayed asynchronously.", prv_read_client, NULL},
            {"disc", "Discover resources of a client.", " disc CLIENT# URI\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to discover such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                                            "Result will be displayed asynchronously.", prv_discover_client, NULL},
            {"write", "Write to a client.", " write CLIENT# URI DATA\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to write to such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                                            "   DATA: data to write. Text or a supported JSON format.\r\n"
                                            "Result will be displayed asynchronously.", prv_write_client, NULL},
            {"dfota", "Trigger firmware download.", " dfota CLIENT# FILE\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   FILE: file name under the firmware directory.\r\n"
                                            "This observes /5/0/3 and writes coap://HOST:PORT/PREFIX/FILE to /5/0/1.", prv_dfota_client, NULL},
            {"update", "Write to a client with partial update.", " update CLIENT# URI DATA\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to write to such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                                            "   DATA: data to write. Must be a supported JSON format.\r\n"
                                            "Result will be displayed asynchronously.", prv_update_client, NULL},
            {"time", "Write time-related attributes to a client.", " time CLIENT# URI PMIN PMAX\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to write attributes to such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                                            "   PMIN: Minimum period\r\n"
                                            "   PMAX: Maximum period\r\n"
                                            "Result will be displayed asynchronously.", prv_time_client, NULL},
            {"attr", "Write value-related attributes to a client.", " attr CLIENT# URI LT GT [STEP]\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to write attributes to such as /3/0/2, /1024/0/1\r\n"
                                            "   LT: \"Less than\" value\r\n"
                                            "   GT: \"Greater than\" value\r\n"
                                            "   STEP: \"Step\" value\r\n"
                                            "Result will be displayed asynchronously.", prv_attr_client, NULL},
            {"clear", "Clear attributes of a client.", " clear CLIENT# URI\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to clear attributes of such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                                            "Result will be displayed asynchronously.", prv_clear_client, NULL},
            {"exec", "Execute a client resource.", " exec CLIENT# URI\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri of the resource to execute such as /3/0/2\r\n"
                                            "Result will be displayed asynchronously.", prv_exec_client, NULL},
            {"del", "Delete a client Object instance.", " del CLIENT# URI\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri of the instance to delete such as /1024/11\r\n"
                                            "Result will be displayed asynchronously.", prv_delete_client, NULL},
            {"create", "Create an Object instance.", " create CLIENT# URI DATA\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to which create the Object Instance such as /1024, /1024/45 \r\n"
                                            "   DATA: data to initialize the new Object Instance (0-255 for object 31024 or any supported JSON format) \r\n"
                                            "Result will be displayed asynchronously.", prv_create_client, NULL},
            {"observe", "Observe from a client.", " observe CLIENT# URI\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri to observe such as /3, /3/0/2, /1024/11\r\n"
                                            "Result will be displayed asynchronously.", prv_observe_client, NULL},
            {"cancel", "Cancel an observe.", " cancel CLIENT# URI\r\n"
                                            "   CLIENT#: client number as returned by command 'list'\r\n"
                                            "   URI: uri on which to cancel an observe such as /3, /3/0/2, /1024/11\r\n"
                                            "Result will be displayed asynchronously.", prv_cancel_client, NULL},

            {"q", "Quit the server.", NULL, prv_quit, NULL},

            COMMAND_END_LIST
    };

    opt = 1;
    while (opt < argc)
    {
        if (argv[opt] == NULL
            || argv[opt][0] != '-'
            || argv[opt][2] != 0)
        {
            print_usage();
            return 0;
        }
        switch (argv[opt][1])
        {
        case '4':
            addressFamily = AF_INET;
            break;
        case 'l':
            opt++;
            if (opt >= argc)
            {
                print_usage();
                return 0;
            }
            localPort = argv[opt];
            g_localPort = localPort;
            break;
        case 'p':
            opt++;
            if (opt >= argc)
            {
                print_usage();
                return 0;
            }
            controlSocketPath = argv[opt];
            break;
        case 'F':
            opt++;
            if (opt >= argc)
            {
                print_usage();
                return 0;
            }
            g_dfotaFwRoot = argv[opt];
            break;
        case 'u':
            opt++;
            if (opt >= argc || argv[opt][0] != '/')
            {
                print_usage();
                return 0;
            }
            g_dfotaUriPrefix = argv[opt];
            break;
        case 'H':
            opt++;
            if (opt >= argc)
            {
                print_usage();
                return 0;
            }
            g_dfotaHost = argv[opt];
            break;
        case 'S':
            opt++;
            if (opt >= argc) {
                print_usage();
                return 0;
            }
            uint16_t coap_block_size_arg;
            if (1 == sscanf(argv[opt], "%" SCNu16, &coap_block_size_arg) &&
                lwm2m_set_coap_block_size(coap_block_size_arg)) {
                break;
            } else {
                print_usage();
                return 0;
            }
        default:
            print_usage();
            return 0;
        }
        opt += 1;
    }

    sock = create_socket(localPort, addressFamily);
    if (sock < 0)
    {
        fprintf(stderr, "Error opening socket: %d\r\n", errno);
        return -1;
    }

    control_socket = prv_create_control_socket(controlSocketPath);
    if (control_socket < 0)
    {
        close(sock);
        return -1;
    }

    lwm2mH = lwm2m_init(NULL);
    if (NULL == lwm2mH)
    {
        fprintf(stderr, "lwm2m_init() failed\r\n");
        return -1;
    }

    signal(SIGINT, handle_sigint);

    g_stdin_is_tty = isatty(STDIN_FILENO);

    if (prv_enable_raw_input() != 0)
    {
        fprintf(stderr, "Failed to configure terminal input: %d\r\n", errno);
        return -1;
    }

    if (!g_stdin_is_tty)
    {
        fprintf(stderr, "stdin is not a TTY: console commands are disabled for this run.\r\n");
    }

    fprintf(stdout, "> "); fflush(stdout);

    lwm2m_set_monitoring_callback(lwm2mH, prv_monitor_callback, NULL);

    while (0 == g_quit)
    {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(control_socket, &readfds);
        /* Only watch stdin when it is a real TTY. Otherwise (e.g. run under
         * nohup/systemd, or with stdin redirected from /dev/null) stdin is
         * permanently "readable" (EOF), which would make select() return
         * immediately every time and spin the loop at 100% CPU. */
        if (g_stdin_is_tty)
        {
            FD_SET(STDIN_FILENO, &readfds);
        }

        tv.tv_sec = 60;
        tv.tv_usec = 0;

        result = lwm2m_step(lwm2mH, &(tv.tv_sec));
        if (result != 0)
        {
            fprintf(stderr, "lwm2m_step() failed: 0x%X\r\n", result);
            return -1;
        }

        result = select(FD_SETSIZE, &readfds, 0, 0, &tv);

        if ( result < 0 )
        {
            if (errno != EINTR)
            {
              fprintf(stderr, "Error in select(): %d\r\n", errno);
            }
        }
        else if (result > 0)
        {
            uint8_t buffer[MAX_PACKET_SIZE];
            ssize_t numBytes;

            if (FD_ISSET(sock, &readfds))
            {
                struct sockaddr_storage addr;
                socklen_t addrLen;

                addrLen = sizeof(addr);
                numBytes = recvfrom(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&addr, &addrLen);

                if (numBytes == -1)
                {
                    fprintf(stderr, "Error in recvfrom(): %d\r\n", errno);
                }
                else if (numBytes >= MAX_PACKET_SIZE) 
                {
                    fprintf(stderr, "Received packet >= MAX_PACKET_SIZE\r\n");
                } 
                else
                {
                    char s[INET6_ADDRSTRLEN];
                    in_port_t port;
                    connection_t * connP;

					s[0] = 0;
                    if (AF_INET == addr.ss_family)
                    {
                        struct sockaddr_in *saddr = (struct sockaddr_in *)&addr;
                        inet_ntop(saddr->sin_family, &saddr->sin_addr, s, INET6_ADDRSTRLEN);
                        port = saddr->sin_port;
                    }
                    else if (AF_INET6 == addr.ss_family)
                    {
                        struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)&addr;
                        inet_ntop(saddr->sin6_family, &saddr->sin6_addr, s, INET6_ADDRSTRLEN);
                        port = saddr->sin6_port;
                    }

                    fprintf(stderr, "%zd bytes received from [%s]:%hu\r\n", numBytes, s, ntohs(port));
                    output_buffer(stderr, buffer, (size_t)numBytes, 0);

                    connP = connection_find(connList, &addr, addrLen);
                    if (connP == NULL)
                    {
                        connP = connection_new_incoming(connList, sock, (struct sockaddr *)&addr, addrLen);
                        if (connP != NULL)
                        {
                            connList = connP;
                        }
                    }
                    if (connP != NULL)
                    {
                        if (!prv_handle_dfota_request(connP, buffer, (size_t)numBytes))
                        {
                            lwm2m_handle_packet(lwm2mH, buffer, (size_t)numBytes, connP);
                        }
                    }
                }
            }
            if (FD_ISSET(control_socket, &readfds))
            {
                prv_handle_control_request(lwm2mH, control_socket);
            }
            else if (FD_ISSET(STDIN_FILENO, &readfds))
            {
                char line[PRV_CMDLINE_MAX_LEN];
                prv_input_result_t inputResult;

                inputResult = prv_input_read(&inputState, line, sizeof(line));

                if (inputResult == PRV_INPUT_ERROR)
                {
                    fprintf(stdout, "\r\nCommand line is too long.\r\n");
                    inputState.line[0] = 0;
                    inputState.length = 0;
                }
                else if (inputResult == PRV_INPUT_READY && line[0] != 0)
                {
                    handle_command(lwm2mH, commands, line);
                    fprintf(stdout, "\r\n");
                }
                if (inputResult == PRV_INPUT_READY || inputResult == PRV_INPUT_ERROR || g_quit != 0)
                {
                    if (g_quit == 0)
                    {
                        fprintf(stdout, "> ");
                        fflush(stdout);
                    }
                    else
                    {
                        fprintf(stdout, "\r\n");
                    }
                }
            }
        }
    }

    prv_restore_terminal();

    lwm2m_close(lwm2mH);
    close(sock);
    close(control_socket);
    unlink(controlSocketPath);
    connection_free(connList);

    return 0;
}
