/* 
** Internal servers header.
**
** @file srv_int.h
** 
** -----------------------------------------------------------------------------
** Enduro/X Middleware Platform for Distributed Transaction Processing
** Copyright (C) 2015, ATR Baltic, SIA. All Rights Reserved.
** This software is released under one of the following licenses:
** GPL or ATR Baltic's license for commercial use.
** -----------------------------------------------------------------------------
** GPL license:
** 
** This program is free software; you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free Software
** Foundation; either version 2 of the License, or (at your option) any later
** version.
**
** This program is distributed in the hope that it will be useful, but WITHOUT ANY
** WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
** PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along with
** this program; if not, write to the Free Software Foundation, Inc., 59 Temple
** Place, Suite 330, Boston, MA 02111-1307 USA
**
** -----------------------------------------------------------------------------
** A commercial use license is available from ATR Baltic, SIA
** contact@atrbaltic.com
** -----------------------------------------------------------------------------
*/

#ifndef SRV_INT_H
#define	SRV_INT_H

#ifdef	__cplusplus
extern "C" {
#endif
/*---------------------------Includes-----------------------------------*/
#include <mqueue.h>
#include <sys/epoll.h>
#include <atmi.h>
#include <setjmp.h>
#include <ndrxdcmn.h>
/*---------------------------Externs------------------------------------*/
/*---------------------------Macros-------------------------------------*/
#define MIN_SVC_LIST_LEN        30
#define SVN_LIST_REALLOC        15

#define RETURN_FAILED             0x1
#define RETURN_TYPE_TPRETURN      0x2
#define RETURN_TYPE_TPFORWARD     0x4
#define RETURN_SVC_FAIL           0x8
#define RETURN_TYPE_THREAD        0x16  /* processing sent to thread   */

/* Special queue logical numbers */
#define ATMI_SRV_ADMIN_Q            0           /* This is admin queue */
#define ATMI_SRV_REPLY_Q            1           /* This is reply queue */
#define ATMI_SRV_Q_ADJUST           2           /* Adjustment for Q nr */
/*---------------------------Enums--------------------------------------*/
/*---------------------------Typedefs-----------------------------------*/

   
/*
 * Service name/alias entry.
 */
typedef struct svc_entry svc_entry_t;
struct svc_entry
{
    char svc_nm[XATMI_SERVICE_NAME_LENGTH+1];
    char svc_alias[XATMI_SERVICE_NAME_LENGTH+1];
    svc_entry_t *next, *prev;
};


/*
 * Service entry descriptor.
 */
typedef struct svc_entry_fn svc_entry_fn_t;
struct svc_entry_fn
{
    char svc_nm[XATMI_SERVICE_NAME_LENGTH+1]; /* service name */
    char fn_nm[XATMI_SERVICE_NAME_LENGTH+1]; /* function name */
    void (*p_func)(TPSVCINFO *);
    /* listing support */
    svc_entry_fn_t *next, *prev;
    char listen_q[FILENAME_MAX+1]; /* queue on which to listen */
    int is_admin;
    mqd_t q_descr; /* queue descriptor */
    n_timer_t qopen_time;
};

/*
 * Basic call info
 */
struct basic_call_info
{
    char *buf_ptr;
    long len;
    int no;
};
typedef struct basic_call_info call_basic_info_t;

/*
 * Basic server configuration.
 */
struct srv_conf
{
    char binary_name[MAXTIDENT+1];
    int srv_id;
    char err_output[FILENAME_MAX+1];
    int log_work;
    int advertise_all;
    svc_entry_t *svc_list;
    char q_prefix[FILENAME_MAX+1];
    /* Arguments passed after -- */
    int app_argc;
    char **app_argv;
    
    /*<THESE LISTS ARE USED ONLY TILL SERVER GOES ONLINE, STATIC INIT>*/
    svc_entry_fn_t *service_raw_list; /* As from initialization */
    svc_entry_fn_t **service_array; /* Direct array of items */
    /*</THESE LISTS ARE USED ONLY TILL SERVER GOES ONLINE, STATIC INIT>*/
    
    svc_entry_fn_t *service_list; /* Final list used for processing */
    
    int adv_service_count; /* advertised service count. */
    int flags; /* Special flags of the server (see: ndrxdcmn.h:SRV_KEY_FLAGS_BRIDGE) */
    int nodeid; /* Other node id of the bridge */
    int (*p_qmsg)(char *buf, int len, char msg_type); /* Q message processor for bridge */
    /**************** POLLING *****************/
    struct epoll_event *events;
	int epollfd;
    /* Information from calst call */
    char last_caller_address[NDRX_MAX_Q_SIZE+1];
    call_basic_info_t last_call;
    jmp_buf call_ret_env;
    int time_out;
    int max_events; /* Max epoll events. */
    
    /* Periodic callback */
    int (*p_periodcb)(void);
    int periodcb_sec; /* Number of seconds for each cb call */
    
    /* Callback used before server goes in poll state */
    int (*p_b4pollcb)(void);
};

typedef struct srv_conf srv_conf_t;

/*---------------------------Globals------------------------------------*/
extern srv_conf_t G_server_conf;
extern shm_srvinfo_t *G_shm_srv;
extern pollextension_rec_t *G_pollext;
extern int G_shutdown_req;
/*---------------------------Statics------------------------------------*/
/*---------------------------Prototypes---------------------------------*/
extern int sv_open_queue(void);
extern int sv_wait_for_request(void);
extern int unadvertse_to_ndrxd(char *srvcnm);

/* Server specific functions: */
extern void	_tpreturn (int rval, long rcode, char *data, long len, long flags);

/* ndrd api */
extern int advertse_to_ndrxd(svc_entry_fn_t *entry);
extern int advertse_to_ndrxd(svc_entry_fn_t *entry);
extern int report_to_ndrxd(void);
/* Return list of connected bridge nodes. */
extern int ndrxd_get_bridges(char *nodes_out);
    
/* Advertise & unadvertise */
extern int dynamic_unadvertise(char *svcname, int *found, svc_entry_fn_t *copy);
extern int	dynamic_advertise(svc_entry_fn_t *entry_new, 
                    char *svc_nm, void (*p_func)(TPSVCINFO *), char *fn_nm);
/* We want to re-advertise the stuff, this could be used for race conditions! */
extern int dynamic_readvertise(char *svcname);

/* Polling extension */
extern pollextension_rec_t * ext_find_poller(int fd);
extern int _tpext_addpollerfd(int fd, uint32_t events, 
        void *ptr1, int (*p_pollevent)(int fd, uint32_t events, void *ptr1));
extern int _tpext_delpollerfd(int fd);
extern int _tpext_addperiodcb(int secs, int (*p_periodcb)(void));
extern int _tpext_delperiodcb(void);
extern int _tpext_addb4pollcb(int (*p_b4pollcb)(void));
extern int _tpext_delb4pollcb(void);
extern int process_admin_req(char *buf, long len, int *shutdown_req);

#ifdef	__cplusplus
}
#endif

#endif	/* SRV_INT_H */
