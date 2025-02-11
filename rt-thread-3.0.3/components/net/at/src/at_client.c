/*
 * File      : at_client.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2006 - 2018, RT-Thread Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-03-30     chenyong     first version
 * 2018-04-12     chenyong     add client implement
 * 2018-08-17     chenyong     multiple client support
 */

#include <at.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AT_RESP_END_OK                 "OK"
#define AT_RESP_END_ERROR              "ERROR"
#define AT_RESP_END_FAIL               "FAIL"
#define AT_END_CR_LF                   "\r\n"

static struct at_client at_client_table[AT_CLIENT_NUM_MAX] = { 0 };

int AT_COMPonet_Station = 1;



extern rt_size_t at_vprintfln(rt_device_t device, const char *format, va_list args);
extern void at_print_raw_cmd(const char *type, const char *cmd, rt_size_t size);
extern const char *at_get_last_cmd(rt_size_t *cmd_size);

/**
 * Create response object.
 *
 * @param buf_size the maximum response buffer size
 * @param line_num the number of setting response lines
 *         = 0: the response data will auto return when received 'OK' or 'ERROR'
 *        != 0: the response data will return when received setting lines number data
 * @param timeout the maximum response time
 *
 * @return != RT_NULL: response object
 *          = RT_NULL: no memory
 */
at_response_t at_create_resp(rt_size_t buf_size, rt_size_t line_num, rt_int32_t timeout)
{
    at_response_t resp = RT_NULL;
	
    resp = (at_response_t) rt_calloc(1, sizeof(struct at_response));
    if (resp == RT_NULL)
    {
//       LOG_E("AT create response object failed! No memory for response object!");
        return RT_NULL;
    }

    resp->buf = (char *) rt_calloc(1, buf_size);
    if (resp->buf == RT_NULL)
    {
//        LOG_E("AT create response object failed! No memory for response buffer!");
        rt_free(resp);
        return RT_NULL;
    }

    resp->buf_size = buf_size;
    resp->line_num = line_num;
    resp->line_counts = 0;
    resp->timeout = timeout;

    return resp;
}

/**
 * Delete and free response object.
 *
 * @param resp response object
 */
void at_delete_resp(at_response_t resp)
{   
    if (resp && resp->buf)
    {
        rt_free(resp->buf);
    }

    if (resp)
    {
        rt_free(resp);
        resp = RT_NULL;
    }
}

/**
 * Set response object information
 *
 * @param resp response object
 * @param buf_size the maximum response buffer size
 * @param line_num the number of setting response lines
 *         = 0: the response data will auto return when received 'OK' or 'ERROR'
 *        != 0: the response data will return when received setting lines number data
 * @param timeout the maximum response time
 *
 * @return  != RT_NULL: response object
 *           = RT_NULL: no memory
 */
at_response_t at_resp_set_info(at_response_t resp, rt_size_t buf_size, rt_size_t line_num, rt_int32_t timeout)
{
    RT_ASSERT(resp);

    if (resp->buf_size != buf_size)
    {
        resp->buf_size = buf_size;

        resp->buf = (char *) rt_realloc(resp->buf, buf_size);
        if (!resp->buf)
        {
//            LOG_D("No memory for realloc response buffer size(%d).", buf_size);
            return RT_NULL;
        }
    }

    resp->line_num = line_num;
    resp->timeout = timeout;

    return resp;
}

/**
 * Get one line AT response buffer by line number.
 *
 * @param resp response object
 * @param resp_line line number, start from '1'
 *
 * @return != RT_NULL: response line buffer
 *          = RT_NULL: input response line error
 */
const char *at_resp_get_line(at_response_t resp, rt_size_t resp_line)
{
    char *resp_buf = resp->buf;
    char *resp_line_buf = RT_NULL;
    rt_size_t line_num = 1;

    RT_ASSERT(resp);

    if (resp_line > resp->line_counts || resp_line <= 0)
    {
    //    LOG_E("AT response get line failed! Input response line(%d) error!", resp_line);
        return RT_NULL;
    }

    for (line_num = 1; line_num <= resp->line_counts; line_num++)
    {
        if (resp_line == line_num)
        {
            resp_line_buf = resp_buf;

            return resp_line_buf;
        }

        resp_buf += strlen(resp_buf) + 1;
    }

    return RT_NULL;
}

/**
 * Get one line AT response buffer by keyword
 *
 * @param resp response object
 * @param keyword query keyword
 *
 * @return != RT_NULL: response line buffer
 *          = RT_NULL: no matching data
 */
const char *at_resp_get_line_by_kw(at_response_t resp, const char *keyword)
{
    char *resp_buf = resp->buf;
    char *resp_line_buf = RT_NULL;
    rt_size_t line_num = 1;

    RT_ASSERT(resp);
    RT_ASSERT(keyword);

    for (line_num = 1; line_num <= resp->line_counts; line_num++)
    {
        if (strstr(resp_buf, keyword))
        {
            resp_line_buf = resp_buf;

            return resp_line_buf;
        }

        resp_buf += strlen(resp_buf) + 1;
    }

    return RT_NULL;
}

/**
 * Get and parse AT response buffer arguments by line number.
 *
 * @param resp response object
 * @param resp_line line number, start from '1'
 * @param resp_expr response buffer expression
 *
 * @return -1 : input response line number error or get line buffer error
 *          0 : parsed without match
 *         >0 : the number of arguments successfully parsed
 */
int at_resp_parse_line_args(at_response_t resp, rt_size_t resp_line, const char *resp_expr, ...)
{
    va_list args;
    int resp_args_num = 0;
    const char *resp_line_buf = RT_NULL;

    RT_ASSERT(resp);
    RT_ASSERT(resp_expr);

    if ((resp_line_buf = at_resp_get_line(resp, resp_line)) == RT_NULL)
    {
        return -1;
    }

    va_start(args, resp_expr);

    resp_args_num = vsscanf(resp_line_buf, resp_expr, args);

    va_end(args);

    return resp_args_num;
}

/**
 * Get and parse AT response buffer arguments by keyword.
 *
 * @param resp response object
 * @param keyword query keyword
 * @param resp_expr response buffer expression
 *
 * @return -1 : input keyword error or get line buffer error
 *          0 : parsed without match
 *         >0 : the number of arguments successfully parsed
 */
int at_resp_parse_line_args_by_kw(at_response_t resp, const char *keyword, const char *resp_expr, ...)
{
    va_list args;
    int resp_args_num = 0;
    const char *resp_line_buf = RT_NULL;

    RT_ASSERT(resp);
    RT_ASSERT(resp_expr);

    if ((resp_line_buf = at_resp_get_line_by_kw(resp, keyword)) == RT_NULL)
    {
        return -1;
    }

    va_start(args, resp_expr);

    resp_args_num = vsscanf(resp_line_buf, resp_expr, args);

    va_end(args);

    return resp_args_num;
}

/**
 * Send commands to AT server and wait response.
 *
 * @param client current AT client object
 * @param resp AT response object, using RT_NULL when you don't care response
 * @param cmd_expr AT commands expression
 *
 * @return 0 : success
 *        -1 : response status error
 *        -2 : wait timeout
 */
int at_obj_exec_cmd(at_client_t client, at_response_t resp, const char *cmd_expr, ...)
{
    va_list args;
    rt_size_t cmd_size = 0;
    rt_err_t result = RT_EOK;
    const char *cmd = RT_NULL;

    RT_ASSERT(cmd_expr);

    if (client == RT_NULL)
    {
//        LOG_E("input AT Client object is NULL, please create or get AT Client object!");
        return -RT_ERROR;
    }

    rt_mutex_take(client->lock, RT_WAITING_FOREVER);

    client->resp_status = AT_RESP_OK;
    client->resp = resp;

    va_start(args, cmd_expr);
    at_vprintfln(client->device, cmd_expr, args);
    va_end(args);

    if (resp != RT_NULL)
    {
        resp->line_counts = 0;
        if (rt_sem_take(client->resp_notice, resp->timeout) != RT_EOK)
        {
            cmd = at_get_last_cmd(&cmd_size);
//          LOG_E("execute command (%.*s) timeout (%d ticks)!", cmd_size, cmd, resp->timeout);
            client->resp_status = AT_RESP_TIMEOUT;
            result = -RT_ETIMEOUT;
            goto __exit;
        }
        if (client->resp_status != AT_RESP_OK)
        {
            cmd = at_get_last_cmd(&cmd_size);
//          LOG_E("execute command (%.*s) failed!", cmd_size, cmd);
            result = -RT_ERROR;
            goto __exit;
        }
    }

__exit:
    client->resp = RT_NULL;

    rt_mutex_release(client->lock);

    return result;
}

/**
 * Waiting for connection to external devices.
 *
 * @param client current AT client object
 * @param timeout millisecond for timeout
 *
 * @return 0 : success
 *        -2 : timeout
 *        -5 : no memory
 */
int at_client_obj_wait_connect(at_client_t client, rt_uint32_t timeout)
{
    rt_err_t result = RT_EOK;
    at_response_t resp = RT_NULL;
    rt_tick_t start_time = 0;

    if (client == RT_NULL)
    {
//      LOG_E("input AT Client object is NULL, please create or get AT Client object!");
        return -RT_ERROR;
    }

    resp = at_create_resp(16, 0, rt_tick_from_millisecond(500));
    if (resp == RT_NULL)
    {
//      LOG_E("No memory for response object!");
        return -RT_ENOMEM;
    }

    rt_mutex_take(client->lock, RT_WAITING_FOREVER);
    client->resp = resp;

    start_time = rt_tick_get();

    while (1)
    {
        /* Check whether it is timeout */
        if (rt_tick_get() - start_time > timeout)
        {
//          LOG_E("wait connect timeout (%d millisecond)!", timeout);
            result = -RT_ETIMEOUT;
            break;
        }

        /* Check whether it is already connected */
        resp->line_counts = 0;
        rt_device_write(client->device, 0, "AT\r\n", 4);

        if (rt_sem_take(client->resp_notice, resp->timeout) != RT_EOK)
            continue;
        else
            break;
    }

    at_delete_resp(resp);

    client->resp = RT_NULL;

    rt_mutex_release(client->lock);

    return result;
}

/**
 * Send data to AT server, send data don't have end sign(eg: \r\n).
 *
 * @param client current AT client object
 * @param buf   send data buffer
 * @param size  send fixed data size
 *
 * @return >0: send data size
 *         =0: send failed
 */
rt_size_t at_client_obj_send(at_client_t client, const char *buf, rt_size_t size)
{
    RT_ASSERT(buf);

    if (client == RT_NULL)
    {
//        LOG_E("input AT Client object is NULL, please create or get AT Client object!");
        return 0;
    }

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("send", buf, size);
#endif

    return rt_device_write(client->device, 0, buf, size);
}

static char at_client_getchar(at_client_t client)
{
    char ch = 0;

    while (rt_device_read(client->device, 0, &ch, 1) == 0)
    {
        rt_sem_control(client->rx_notice, RT_IPC_CMD_RESET, RT_NULL);
        rt_sem_take(client->rx_notice, RT_WAITING_FOREVER);
    }

    return ch;
}

/**
 * AT client receive fixed-length data.
 *
 * @param client current AT client object
 * @param buf   receive data buffer
 * @param size  receive fixed data size
 *
 * @note this function can only be used in execution function of URC data
 *
 * @return >0: receive data size
 *         =0: receive failed
 */
rt_size_t at_client_obj_recv(at_client_t client, char *buf, rt_size_t size)
{
    rt_size_t read_idx = 0;
    char ch;

    RT_ASSERT(buf);

    if (client == RT_NULL)
    {
//        LOG_E("input AT Client object is NULL, please create or get AT Client object!");
        return 0;
    }

    while (1)
    {
        if (read_idx < size)
        {
            ch = at_client_getchar(client);

            buf[read_idx++] = ch;
        }
        else
        {
            break;
        }
    }

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("urc_recv", buf, size);
#endif

    return read_idx;
}

/**
 *  AT client set end sign.
 *
 * @param client current AT client object
 * @param ch the end sign, can not be used when it is '\0'
 */
void at_obj_set_end_sign(at_client_t client, char ch)
{
    if (client == RT_NULL)
    {
//        LOG_E("input AT Client object is NULL, please create or get AT Client object!");
        return;
    }

    client->end_sign = ch;
}

/**
 * set URC(Unsolicited Result Code) table
 *
 * @param client current AT client object
 * @param table URC table
 * @param size table size
 */
void at_obj_set_urc_table(at_client_t client, const struct at_urc *urc_table, rt_size_t table_sz)
{
    rt_size_t idx;

    if (client == RT_NULL)
    {
//        LOG_E("input AT Client object is NULL, please create or get AT Client object!");
        return;
    }

    for (idx = 0; idx < table_sz; idx++)
    {
        RT_ASSERT(urc_table[idx].cmd_prefix);
        RT_ASSERT(urc_table[idx].cmd_suffix);
    }

    client->urc_table = urc_table;
    client->urc_table_size = table_sz;
}

/**
 * get AT client object by AT device name.
 *
 * @dev_name AT client device name
 *
 * @return AT client object
 */
at_client_t at_client_get(const char *dev_name)
{
    int idx = 0;

    RT_ASSERT(dev_name);

    for (idx = 0; idx < AT_CLIENT_NUM_MAX; idx++)
    {
        if (rt_strcmp(at_client_table[idx].device->parent.name, dev_name) == 0)
        {
            return &at_client_table[idx];
        }
    }

    return RT_NULL;
}

/**
 * get first AT client object in the table.
 *
 * @return AT client object
 */
at_client_t at_client_get_first(void)
{
    if (at_client_table[0].device == RT_NULL)
    {
        return RT_NULL;
    }

    return &at_client_table[0];
}

static const struct at_urc *get_urc_obj(at_client_t client)
{
    rt_size_t i, prefix_len, suffix_len;
    rt_size_t buf_sz;
    char *buffer = RT_NULL;

    if (client->urc_table == RT_NULL)
    {
        return RT_NULL;
    }

    buffer = client->recv_buffer;
    buf_sz = client->cur_recv_len;

    for (i = 0; i < client->urc_table_size; i++)
    {
        prefix_len = strlen(client->urc_table[i].cmd_prefix);
        suffix_len = strlen(client->urc_table[i].cmd_suffix);
        if (buf_sz < prefix_len + suffix_len)
        {
            continue;
        }
        if ((prefix_len ? !strncmp(buffer, client->urc_table[i].cmd_prefix, prefix_len) : 1)
                && (suffix_len ? !strncmp(buffer + buf_sz - suffix_len, client->urc_table[i].cmd_suffix, suffix_len) : 1))
        {
            return &client->urc_table[i];
        }
    }

    return RT_NULL;
}

static int at_recv_readline(at_client_t client)
{
    rt_size_t read_len = 0;
    char ch = 0, last_ch = 0;
    rt_bool_t is_full = RT_FALSE;

    memset(client->recv_buffer, 0x00, client->recv_bufsz);
    client->cur_recv_len = 0;

    while (1)
    {
        ch = at_client_getchar(client);

        if (read_len < client->recv_bufsz)
        {
            client->recv_buffer[read_len++] = ch;
            client->cur_recv_len = read_len;
        }
        else
        {
            is_full = RT_TRUE;
        }

        /* is newline or URC data */
        if ((ch == '\n' && last_ch == '\r') || (client->end_sign != 0 && ch == client->end_sign)
                || get_urc_obj(client))
        {
            if (is_full)
            {
//                LOG_E("read line failed. The line data length is out of buffer size(%d)!", client->recv_bufsz);
                memset(client->recv_buffer, 0x00, client->recv_bufsz);
                client->cur_recv_len = 0;
                return -RT_EFULL;
            }
            break;
        }
        last_ch = ch;
    }

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("recvline", client->recv_buffer, read_len);
#endif

    return read_len;
}

static void client_parser(at_client_t client)
{
    int resp_buf_len = 0;
    const struct at_urc *urc;
    rt_size_t line_counts = 0;

    while(1)
    {           
		       
	      if(AT_COMPonet_Station == 0)
		  {
			   rt_thread_delay(100);
			   rt_kprintf("AT Componet Sleep!\n");
			   continue;
						
		  }

		
        if (at_recv_readline(client) > 0)
        {
            if ((urc = get_urc_obj(client)) != RT_NULL)
            {
                /* current receive is request, try to execute related operations */
                if (urc->func != RT_NULL)
                {
                    urc->func(client->recv_buffer, client->cur_recv_len);
                }
            }
            else if (client->resp != RT_NULL)
            {
                /* current receive is response */
                client->recv_buffer[client->cur_recv_len - 1] = '\0';
                if (resp_buf_len + client->cur_recv_len < client->resp->buf_size)
                {
                    /* copy response lines, separated by '\0' */
                    memcpy(client->resp->buf + resp_buf_len, client->recv_buffer, client->cur_recv_len);
                    resp_buf_len += client->cur_recv_len;

                    line_counts++;
                }
                else
                {
                    client->resp_status = AT_RESP_BUFF_FULL;
//                    LOG_E("Read response buffer failed. The Response buffer size is out of buffer size(%d)!", client->resp->buf_size);
                }
                /* check response result */
                if (memcmp(client->recv_buffer, AT_RESP_END_OK, strlen(AT_RESP_END_OK)) == 0
                        && client->resp->line_num == 0)
                {
                    /* get the end data by response result, return response state END_OK. */
                    client->resp_status = AT_RESP_OK;
                }
                else if (strstr(client->recv_buffer, AT_RESP_END_ERROR)
                        || (memcmp(client->recv_buffer, AT_RESP_END_FAIL, strlen(AT_RESP_END_FAIL)) == 0))
                {
                    client->resp_status = AT_RESP_ERROR;
                }
                else if (line_counts == client->resp->line_num && client->resp->line_num)
                {
                    /* get the end data by response line, return response state END_OK.*/
                    client->resp_status = AT_RESP_OK;
                }
                else
                {
                    continue;
                }
                client->resp->line_counts = line_counts;

                client->resp = RT_NULL;
                rt_sem_release(client->resp_notice);
                resp_buf_len = 0, line_counts = 0;
            }
            else
            {
//                log_d("unrecognized line: %.*s", client->cur_recv_len, client->recv_buffer);
            }
        }
    }
}

static rt_err_t at_client_rx_ind(rt_device_t dev, rt_size_t size)
{
    int idx = 0;

    for (idx = 0; idx < AT_CLIENT_NUM_MAX; idx++)
    {
        if (at_client_table[idx].device == dev && size > 0)
        {
            rt_sem_release(at_client_table[idx].rx_notice);
        }
    }

    return RT_EOK;
}

/* initialize the client object parameters */
static int at_client_para_init(at_client_t client)
{
#define AT_CLIENT_LOCK_NAME            "at_c"
#define AT_CLIENT_SEM_NAME             "at_cs"
#define AT_CLIENT_RESP_NAME            "at_cr"
#define AT_CLIENT_THREAD_NAME          "at_clnt"

    int result = RT_EOK;
    static int at_client_num = 0;
    char name[RT_NAME_MAX];

    client->status = AT_STATUS_UNINITIALIZED;

    client->cur_recv_len = 0;
    client->recv_buffer = (char *) rt_calloc(1, client->recv_bufsz);
    if (client->recv_buffer == RT_NULL)
    {
//      LOG_E("AT client initialize failed! No memory for receive buffer.")
        result = -RT_ENOMEM;
        goto __exit;
    }

    rt_snprintf(name, RT_NAME_MAX, "%s%d", AT_CLIENT_LOCK_NAME, at_client_num);
    client->lock = rt_mutex_create(name, RT_IPC_FLAG_FIFO);
    if (client->lock == RT_NULL)
    {
//      LOG_E("AT client initialize failed! at_client_recv_lock create failed!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    rt_snprintf(name, RT_NAME_MAX, "%s%d", AT_CLIENT_SEM_NAME, at_client_num);
    client->rx_notice = rt_sem_create(name, 0, RT_IPC_FLAG_FIFO);
    if (client->rx_notice == RT_NULL)
    {
//        LOG_E("AT client initialize failed! at_client_notice semaphore create failed!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    rt_snprintf(name, RT_NAME_MAX, "%s%d", AT_CLIENT_RESP_NAME, at_client_num);
    client->resp_notice = rt_sem_create(name, 0, RT_IPC_FLAG_FIFO);
    if (client->resp_notice == RT_NULL)
    {
//        LOG_E("AT client initialize failed! at_client_resp semaphore create failed!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    client->urc_table = RT_NULL;
    client->urc_table_size = 0;

    rt_snprintf(name, RT_NAME_MAX, "%s%d", AT_CLIENT_THREAD_NAME, at_client_num);
    client->parser = rt_thread_create(name,
                                     (void (*)(void *parameter))client_parser,
                                     client,
                                     1024 + 512,
                                     RT_THREAD_PRIORITY_MAX / 3 - 1,
                                     5);
    if (client->parser == RT_NULL)
    {
        result = -RT_ENOMEM;
        goto __exit;
    }

__exit:
    if (result != RT_EOK)
    {
        if (client->lock)
        {
            rt_mutex_delete(client->lock);
        }

        if (client->rx_notice)
        {
            rt_sem_delete(client->rx_notice);
        }

        if (client->resp_notice)
        {
            rt_sem_delete(client->resp_notice);
        }

        if (client->device)
        {
            rt_device_close(client->device);
        }

        if (client->recv_buffer)
        {
            rt_free(client->recv_buffer);
        }

        rt_memset(client, 0x00, sizeof(struct at_client));
    }
    else
    {
        at_client_num++;
    }

    return result;
}

/**
 * AT client initialize.
 *
 * @param dev_name AT client device name
 * @param recv_bufsz the maximum number of receive buffer length
 *
 * @return 0 : initialize success
 *        -1 : initialize failed
 *        -5 : no memory
 */
int at_client_init(const char *dev_name,  rt_size_t recv_bufsz)
{
    int idx = 0;
    int result = RT_EOK;
    rt_err_t open_result = RT_EOK;
    at_client_t client = RT_NULL;

    RT_ASSERT(dev_name);
    RT_ASSERT(recv_bufsz > 0);

    for (idx = 0; idx < AT_CLIENT_NUM_MAX && at_client_table[idx].device; idx++);

    if (idx >= AT_CLIENT_NUM_MAX)
    {
//        LOG_E("AT client initialize filed! Check the maximum number(%d) of AT client.", AT_CLIENT_NUM_MAX);
        result = -RT_EFULL;
        goto __exit;
    }

    client = &at_client_table[idx];
    client->recv_bufsz = recv_bufsz;

    /* find and open command device */
    client->device = rt_device_find(dev_name);
    if (client->device)
    {
        RT_ASSERT(client->device->type == RT_Device_Class_Char);

        /* using DMA mode first */
      //  open_result = rt_device_open(client->device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
        /* using interrupt mode when DMA mode not supported */
       // if (open_result == -RT_EIO)
        {
            open_result = rt_device_open(client->device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
        }
        RT_ASSERT(open_result == RT_EOK);

        rt_device_set_rx_indicate(client->device, at_client_rx_ind);
    }
    else
    {
//        LOG_E("AT client initialize failed! Not find the device(%s).", dev_name);
        result = -RT_ERROR;
        goto __exit;
    }

    result = at_client_para_init(client);
    if (result != RT_EOK)
    {
        goto __exit;
    }

__exit:
    if (result == RT_EOK)
    {
        client->status = AT_STATUS_INITIALIZED;

        rt_thread_startup(client->parser);

//        LOG_I("AT client(V%s) on device %s initialize success.", AT_SW_VERSION, dev_name);
    }
    else
    {
//        LOG_E("AT client(V%s) on device %s initialize failed(%d).", AT_SW_VERSION, dev_name, result);
    }

    return result;
}
