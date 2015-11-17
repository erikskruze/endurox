/* 
** ATMI tpcall function implementation (Common version)
** This will have to make the call and wait back for answer.
** This should pass to function file descriptor for reply back queue.
**
** @file tpcall.c
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

/*---------------------------Includes-----------------------------------*/
#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include <stdlib.h>
#include <errno.h>
#include <mqueue.h>

#include <atmi.h>
#include <ndebug.h>
#include <tperror.h>
#include <typed_buf.h>
#include <atmi_int.h>

#include "../libatmisrv/srv_int.h"
#include "ndrxd.h"
#include <thlock.h>
#include <xa_cmn.h>
/*---------------------------Externs------------------------------------*/
/*---------------------------Macros-------------------------------------*/
/*---------------------------Enums--------------------------------------*/
/*---------------------------Typedefs-----------------------------------*/
/*---------------------------Globals------------------------------------*/
__thread long M_svc_return_code=0;
/*---------------------------Statics------------------------------------*/
/*---------------------------Prototypes---------------------------------*/
private void unlock_call_descriptor(int cd, short status);

/**
 * Process individual call in progress.
 * @param cd
 * @return 
 */
private int call_check_tout(int cd)
{
    int ret=SUCCEED;
    time_t t;
    int t_diff;
    if (CALL_WAITING_FOR_ANS==G_call_state[cd].status &&
            !(G_call_state[cd].flags & TPNOTIME) &&
              (t_diff = ((t = time(NULL)) - G_call_state[cd].timestamp)) > G_atmi_env.time_out
            )
    {
        /* added some more debug info, because we have strange timeouts... */
        NDRX_LOG(log_warn, "cd %d (callseq %u) timeout condition - generating error "
                "(locked at: %ld current tstamp: %ld, diff: %d, timeout_value: %d)", 
                cd, G_call_state[cd].callseq, 
                G_call_state[cd].timestamp, t, t_diff, G_atmi_env.time_out);
        
        _TPset_error_fmt(TPETIME, "cd %d (callseq %u) timeout condition - generating error "
                "(locked at: %ld current tstamp: %ld, diff: %d, timeout_value: %d)", 
                cd, G_call_state[cd].callseq, 
                G_call_state[cd].timestamp, t, t_diff, G_atmi_env.time_out);
        
        /* mark cd as free (will mark as cancelled) */
        unlock_call_descriptor(cd, CALL_CANCELED);
        
        ret=FAIL;
        goto out;
    }
out:
    return ret;
}

/**
 * Function for extra debug.
 */
private void call_dump_descriptors(void)
{
    int i;
    time_t t = time(NULL);
    int t_diff;
    
    NDRX_LOG(log_warn, "***List of call descriptors waiting for answer***");
    NDRX_LOG(log_warn, "timeout(system wide): %d curr_tstamp: %ld", 
                            G_atmi_env.time_out, t);
    NDRX_LOG(log_warn, "cd\tcallseq\tlocked_at\tdiff");
        
    for (i=1; i<MAX_ASYNC_CALLS; i++)
    {
        if (CALL_WAITING_FOR_ANS==G_call_state[i].status)
        {
            t_diff = t - G_call_state[i].timestamp;
            NDRX_LOG(log_warn, "%d\t%u\t%ld\t%d", 
                    i, G_call_state[i].callseq, 
                    G_call_state[i].timestamp, t_diff);
        }
    }
    
    NDRX_LOG(log_warn, "*************************************************");
}

#define CALL_TOUT_DEBUG

/**
 * We should check all call descriptors and imitate time-out condition,
 * if any of calls are timed-out
 * 
 * TODO: hash vs iterate...
 * @param cd - if > 0 the
 * @param cd_out - return failed cd...
 * @return 
 */
private int call_scan_tout(int cd, int *cd_out)
{
MUTEX_LOCK;
{
    /* for performance reasons shouldn't we keep in HASH list 
     * the list of open call descriptors, iterate them and check for
     * time-out condition
     */
    int ret = SUCCEED;
    static int first = TRUE;
    /* initialize timers... */
    static n_timer_t start = {0};
    int i;
    if (first)
    {
        first = FALSE;
    }
#ifdef CALL_TOUT_DEBUG
    call_dump_descriptors();
#endif
    
    /* Check that it is time for scan... */
    if (n_timer_get_delta(&start) >=1000)
    {
        /* we should scan the stuff. */
        if (0 < cd)
        {
            if (SUCCEED!=call_check_tout(cd))
            {
                *cd_out = cd;
                ret=FAIL;
                goto out;
            }
        }
        else
        {
            for (i=1; i<MAX_ASYNC_CALLS; i++)
            {
                if (SUCCEED!=call_check_tout(i))
                {
                    *cd_out = i;
                    ret=FAIL;
                    goto out;
                }
            }
        }
        /* if all ok, schedule after 1 sec. */
        n_timer_reset(&start);
    } /* if check time... */
    
out:

    MUTEX_UNLOCK;
    return ret;
} /* mutex */
}

/**
 * Returns free call descriptro
 * @return >0 (ok), -1 = FAIL
 */
private int get_call_descriptor(unsigned *p_callseq)
{

/* Lock the call descriptor giver...! So that we have common CDs 
 * over the hreads
 */
MUTEX_LOCK;
{
    static int cd=MAX_ASYNC_CALLS-2; /* first available, we want test overlap!*/
    static unsigned callseq=0;
    int start_cd = cd; /* mark where we began */

    while (CALL_WAITING_FOR_ANS==G_call_state[cd].status)
    {
        cd++;

        if (cd > MAX_ASYNC_CALLS-1)
        {
            cd=1; /* TODO: Maybe start with 0? */
        }

        if (start_cd==cd)
        {
            NDRX_LOG(log_debug, "All call descritors overflow restart!");
            break;
        }
    }

    if (CALL_WAITING_FOR_ANS==G_call_state[cd].status)
    {
        NDRX_LOG(log_debug, "All call descritors have been taken - FAIL!");
        cd=FAIL;
    }
    else
    {
        callseq++;
        *p_callseq=callseq;
        NDRX_LOG(log_debug, "Got free call descriptor %d, callseq: %u",
                                            cd, callseq);
    }

    MUTEX_UNLOCK;
    return cd;
}

}

/**
 * Mark particular call descriptor as used. Change status + put timestamp
 * @param cd
 * @return
 */
private int lock_call_descriptor(int cd, time_t timestamp, unsigned callseq, long flags)
{
    int ret = SUCCEED;
    NDRX_LOG(log_debug, "cd %d locked to %d timestamp callseq: %u",
                                        cd, timestamp, callseq);
    G_call_state[cd].status = CALL_WAITING_FOR_ANS;
    G_call_state[cd].timestamp = timestamp;
    G_call_state[cd].callseq = callseq;
    G_call_state[cd].flags = flags;
    
    /* If we have global tx open, then register cd under it! */
    if (!(flags & TPNOTRAN) && G_atmi_xa_curtx.txinfo)
    {
        NDRX_LOG(log_debug, "Registering cd=%d under global "
                "transaction!", cd);
        if (SUCCEED!=atmi_xa_cd_reg(&(G_atmi_xa_curtx.txinfo->call_cds), cd))
        {
            FAIL_OUT(ret);
        }
    }
    
out:
    return ret;
}

/**
 * Unlock call descriptor
 * @param cd
 * @return
 */
private void unlock_call_descriptor(int cd, short status)
{
    G_call_state[cd].status = status;
    
    if (!(G_call_state[cd].flags & TPNOTRAN) && G_atmi_xa_curtx.txinfo)
    {
        NDRX_LOG(log_debug, "Un-registering cd=%d under global "
                "transaction!", cd);
        
        atmi_xa_cd_unreg(&(G_atmi_xa_curtx.txinfo->call_cds), cd);
    }
}

/**
 * Cancel the call descriptor if was expected
 * @param call
 */
public void cancel_if_expected(tp_command_call_t *call)
{
    call_descriptor_state_t *callst  = &G_call_state[call->cd];
    if (CALL_WAITING_FOR_ANS==callst->status &&
            call->timestamp==callst->timestamp &&
            call->callseq==callst->callseq)
    {
        NDRX_LOG(log_debug, "Reply was expected, but probably "
                                        "timeouted - cancelling!");
        unlock_call_descriptor(call->cd, CALL_CANCELED);
    }
    else
    {
        NDRX_LOG(log_debug, "Replyl was NOT expected, ignoring!");
    }
}
/**
 * Do asynchronus call.
 * If it is evpost (is_evpost) htne extradata must be loaded with `my_id' from then
 * server. We will parse it out get the target queue message must be put!
 * 
 * @param svc
 * @param data
 * @param len
 * @param flags - should be managed from parent function (is it real async call
 *                  or tpcall wrapper)
 * @return call descriptor
 */
public int _tpacall (char *svc, char *data,
                long len, long flags, char *extradata, 
                int dest_node, int ex_flags, TPTRANID *p_tranid)
{
    int ret=SUCCEED;
    char buf[ATMI_MSG_MAX_SIZE];
    tp_command_call_t *call=(tp_command_call_t *)buf;
    typed_buffer_descr_t *descr;
    buffer_obj_t *buffer_info;
    char fn[] = "_tpacall";
    long data_len = MAX_CALL_DATA_SIZE;
    char send_q[NDRX_MAX_Q_SIZE+1];
    static __thread int cd = 0;
    time_t timestamp;
    int is_bridge;
    
    NDRX_LOG(log_debug, "%s enter", fn);

    
    if (G_atmi_xa_curtx.txinfo)
    {
        atmi_xa_print_knownrms(log_info, "Known RMs before call: ",
                    G_atmi_xa_curtx.txinfo->tmknownrms);
    }
    
     /* Might want to remove in future... but it might be dangerouse!*/
     memset(call, 0, sizeof(tp_command_call_t));
    
    /* Check service availability by SHM? 
     * TODO: We might want to check the flags, the service might be marked for shutdown!
     */
    if (ex_flags & TPCALL_BRCALL)
    {
        /* If this is a bridge call, then format the bridge Q */
        sprintf(send_q, NDRX_SVC_QBRDIGE, G_atmi_conf.q_prefix, dest_node);
        is_bridge=TRUE;
    }
    else if (ex_flags & TPCALL_EVPOST)
    {
        if (SUCCEED!=_get_evpost_sendq(send_q, extradata))
        {
            NDRX_LOG(log_error, "%s: Cannot get send Q for server: [%s]", 
                    fn, extradata);
            ret=FAIL;
            _TPset_error_fmt(TPENOENT, "%s: Cannot get send Q for server: [%s]", 
                    fn, extradata);
            goto out;
        }
    }
    else if (FALSE==ndrx_shm_get_svc(svc, send_q, &is_bridge))
    {
        NDRX_LOG(log_error, "Service is not available %s by shm", 
                svc);
        ret=FAIL;
        _TPset_error_fmt(TPENOENT, "%s: Service is not available %s by shm", 
                fn, svc);
        goto out;
    }

    if (NULL!=data)
    {
        if (NULL==(buffer_info = find_buffer(data)))
        {
            _TPset_error_fmt(TPEINVAL, "Buffer %p not known to system!", fn);
            ret=FAIL;
            goto out;
        }
    }

    if (NULL!=data)
    {
        descr = &G_buf_descr[buffer_info->type_id];
        /* prepare buffer for call */
        if (SUCCEED!=descr->pf_prepare_outgoing(descr, data, len, call->data, &data_len, flags))
        {
            /* not good - error should be already set */
            ret=FAIL;
            goto out;
        }
    }
    else
    {
        data_len=0;
    }

    /* OK, now fill up the details */
    call->data_len = data_len;
    
    data_len+=sizeof(tp_command_call_t);

    if (NULL==data)
        call->buffer_type_id = BUF_TYPE_NULL;
    else
        call->buffer_type_id = buffer_info->type_id;

    strcpy(call->reply_to, G_atmi_conf.reply_q_str);
    if (!(ex_flags & TPCALL_EVPOST))
    {
        call->command_id = ATMI_COMMAND_TPCALL;
    }
    else
    {
        call->command_id = ATMI_COMMAND_EVPOST;
    }
    
    strncpy(call->name, svc, XATMI_SERVICE_NAME_LENGTH);
    call->name[XATMI_SERVICE_NAME_LENGTH] = EOS;
    call->flags = flags;
    
    if (NULL!=extradata)
        strcpy(call->extradata, extradata);

    timestamp = time(NULL);
    
    /* Add global transaction info to call (if needed & tx available) */
    if (!(call->flags & TPNOTRAN) && G_atmi_xa_curtx.txinfo)
    {
        NDRX_LOG(log_info, "Current process in global transaction (%s) - "
                "prepare call", G_atmi_xa_curtx.txinfo->tmxid);
        
        atmi_xa_cpy_xai_to_call(call, G_atmi_xa_curtx.txinfo);
        
        if (call->flags & TPTRANSUSPEND && NULL!=p_tranid &&
                SUCCEED!=_tpsuspend(p_tranid, 0))
        {
            FAIL_OUT(ret);
        }
    }
    
    /* lock call descriptor */
    if (!(flags & TPNOREPLY))
    {
        /* get the call descriptor */
        if (FAIL==(cd = get_call_descriptor(&call->callseq)))
        {
            NDRX_LOG(log_error, "Do not have resources for "
                                "track this call!");
            _TPset_error_fmt(TPELIMIT, "%s:All call descriptor entries have been used "
                                "(check why they do not free up? Maybe need to "
                                "use tpcancel()?)", fn);
            FAIL_OUT(ret);
        }    
        if (SUCCEED!=lock_call_descriptor(cd, timestamp, call->callseq, flags))
        {
            FAIL_OUT(ret);
        }
    }
    else
    {
        NDRX_LOG(log_warn, "TPNOREPLY => cd =0");
        cd = 0;
    }
    
    call->cd = cd;
    call->timestamp = timestamp;
    
    /* Reset call timer */
    n_timer_reset(&call->timer);
    
    strcpy(call->my_id, G_atmi_conf.my_id); /* Setup my_id */
    NDRX_LOG(log_debug, "Sending request to: [%s] my_id=[%s] reply_to=[%s] cd=%d", 
            send_q, call->my_id, call->reply_to, cd);

    if (SUCCEED!=(ret=generic_q_send(send_q, (char *)call, data_len, flags)))
    {
        int err;

        if (ENOENT==ret)
        {
            err=TPENOENT;
        }
        else
        {
            CONV_ERROR_CODE(ret, err);
        }
        _TPset_error_fmt(err, "%s: Failed to send, os err: %s", fn, strerror(ret));
        ret=FAIL;

        /* unlock call descriptor */
        unlock_call_descriptor(cd, CALL_NOT_ISSUED);
        
        goto out;

    }
    /* return call descriptor */
    ret=cd;

out:
    NDRX_LOG(log_debug, "%s return %d", fn, ret);
    return ret;
}

/**
 * Internal version of tpgetrply.
 * TODO: How about TPGETANY|TPNOCHANGE?
 * @param cd
 * @param data
 * @param len
 * @param flags
 * @return
 */
public int _tpgetrply (int *cd,
                       int cd_exp,
                       char * *data ,
                       long *len, long flags,
                       TPTRANID *p_tranid)
{
    int ret=SUCCEED;
    char fn[] = "_tpgetrply";
    int change_flags = FALSE;
    struct mq_attr new;
    long rply_len;
    unsigned prio;
    char rply_buf[ATMI_MSG_MAX_SIZE];
    tp_command_call_t *rply=(tp_command_call_t *)rply_buf;
    typed_buffer_descr_t *call_type;
    int answ_ok = FALSE;
    int is_abort_only = FALSE; /* Should we abort global tx (if open) */

    NDRX_LOG(log_debug, "%s enter, flags %ld", fn, flags);
    
    if (flags & TPNOBLOCK && !(G_atmi_conf.q_attr.mq_flags & O_NONBLOCK))
    {
        /* change attributes non block mode*/
        new = G_atmi_conf.q_attr;
        new.mq_flags |= O_NONBLOCK;
        change_flags = TRUE;
        NDRX_LOG(log_debug, "Changing queue [%s] to non blocked",
                                            G_atmi_conf.reply_q_str);
    }
    else if (!(flags & TPNOBLOCK) && (G_atmi_conf.q_attr.mq_flags & O_NONBLOCK))
    {
        /* change attributes to block mode */
        new = G_atmi_conf.q_attr;
        new.mq_flags &= ~O_NONBLOCK; /* remove non block flag */
        change_flags = TRUE;
        NDRX_LOG(log_debug, "Changing queue [%s] to blocked",
                                            G_atmi_conf.reply_q_str);
    }
    
    if (change_flags)
    {
        if (FAIL==mq_setattr(G_atmi_conf.reply_q, &new,
                            &G_atmi_conf.q_attr))
        {
            _TPset_error_fmt(TPEOS, "%s: Failed to change attributes for queue [%s] fd %d: %s",
                                fn, G_atmi_conf.reply_q_str, G_atmi_conf.reply_q, strerror(errno));
            ret=FAIL;
            goto out;
        }
    }

    /* TODO: If we keep linked list with call descriptors and if there is
     * none, then we should return something back - FAIL/proto, not? */
    /**
     * We will drop any answers not registered for this call
     */
    while (!answ_ok)
    {
        
        /* receive the reply back */
        rply_len = generic_q_receive(G_atmi_conf.reply_q, rply_buf,
                                        sizeof(rply_buf), &prio, flags);
        
        /* In case  if we did receive any response (in non blocked mode
         * or we did get fail in blocked mode with TPETIME, then we should
         * look up the table for which really we did get the time-out.
         */
        if (flags & TPNOBLOCK && GEN_QUEUE_ERR_NO_DATA==rply_len || 
                FAIL==rply_len && TPETIME==tperrno)
        {
            if (flags & TPGETANY)
            {
                if (SUCCEED!=(ret = call_scan_tout(FAIL, cd)))
                {
                    goto out;
                }
            }
            else
            {
                if (SUCCEED!=(ret = call_scan_tout(cd_exp, cd)))
                {
                    goto out;
                }
            }
        }

        if (GEN_QUEUE_ERR_NO_DATA==rply_len)
        {
            /* there is no data in reply, nothing to do & nothing to return */
            *cd = 0;
            goto out;
        }
        else if (FAIL==rply_len)
        {
            /* we have failed */
            NDRX_LOG(log_debug, "%s failed to receive answer", fn);
            ret=FAIL;
            goto out;
        }
        else
        {
            NDRX_LOG(log_debug, "accept any: %s", (flags & TPGETANY)?"yes":"no" );

            /* if answer is not expected, then we receive again! */
            if (CALL_WAITING_FOR_ANS==G_call_state[rply->cd].status &&
                    G_call_state[rply->cd].timestamp == rply->timestamp &&
                    G_call_state[rply->cd].callseq == rply->callseq)
            {
                /* Drop if we do not expect it!!! */
		/* 01/11/2012 - if we have TPGETANY we ignore the cd */
                if (/*cd_exp!=FAIL*/ !(flags & TPGETANY) && rply->cd!=cd_exp)
                {
                    NDRX_LOG(log_warn, "Dropping incoming message (not expected): "
                        "cd: %d, expected cd: %d timestamp: %d callseq: %u, reply from %s",
                        rply->cd, cd_exp, rply->timestamp, rply->callseq, rply->reply_to);
                    _tpcancel(rply->cd);
                    continue;
                }

                NDRX_LOG(log_warn, "Reply cd: %d, timestamp :%d callseq: %u from %s - expected OK!",
                        rply->cd, rply->timestamp, rply->callseq, rply->reply_to);
                answ_ok=TRUE;
                /* Free up call descriptor!! */
                unlock_call_descriptor(rply->cd, CALL_NOT_ISSUED);
                
            }
            else
            {
                NDRX_LOG(log_warn, "Dropping incoming message (not expected): "
                        "cd: %d, timestamp :%d, callseq: %u reply from %s",
                        rply->cd, rply->timestamp, rply->callseq, rply->reply_to);
                
                continue; /* Wait for next message! */
            }

            /* If we are in global tx & we have abort_only, then report */
            if (TMTXFLAGS_IS_ABORT_ONLY & rply->tmtxflags)
            {
                NDRX_LOG(log_warn, "Reply contains SYS_XA_ABORT_ONLY!");
                is_abort_only = TRUE;
            }
            /* TODO: check incoming type! */
            /* we have an answer - prepare buffer */
            *cd = rply->cd;
            if (rply->sysflags & SYS_FLAG_REPLY_ERROR)
            {
                _TPset_error_msg(rply->rcode, "Server failed to generate reply");
                ret=FAIL;
                goto out;
            }
            else
            {
                /* Convert only if we have data */
                if (rply->data_len > 0)
                {
                    call_type = &G_buf_descr[rply->buffer_type_id];

                    ret=call_type->pf_prepare_incoming(call_type,
                                    rply->data,
                                    rply->data_len,
                                    data,
                                    len,
                                    flags);
                }
                /* put rcode in global */
                M_svc_return_code = rply->rcode;

                /* TODO: Check buffer acceptance or do it inside of prepare_incoming? */
                if (ret==FAIL)
                {
                    goto out;
                }
                /* if all OK, then finally check the reply answer
                 * Maybe want to use code returned in rval?
                 */
                if (TPSUCCESS!=rply->rval)
                {
                    _TPset_error_fmt(TPESVCFAIL, "Service returned %d", rply->rval);
                    ret=FAIL;
                    goto out;
                }
            }
        } /* reply recieved ok */
    }
out:

    /* Restore transaction if was suspended. */
    if (flags & TPTRANSUSPEND && p_tranid && p_tranid->tmxid[0])
    {
        /* resume the transaction */
        if (SUCCEED!=_tpresume(p_tranid, 0) && SUCCEED==ret)
        {
            ret=FAIL;
        }
    }

    if (G_atmi_xa_curtx.txinfo && 0==strcmp(G_atmi_xa_curtx.txinfo->tmxid, rply->tmxid) &&
            SUCCEED!=atmi_xa_update_known_rms(G_atmi_xa_curtx.txinfo->tmknownrms, 
            rply->tmknownrms))
    {
        FAIL_OUT(ret);
    }

    if ( !(flags & TPNOTRAN) &&  /* Do not abort, if TPNOTRAN specified. */
	G_atmi_xa_curtx.txinfo &&
	(SUCCEED!=ret || is_abort_only))
    {
        NDRX_LOG(log_warn, "Marking current transaction as abort only!");
        
        /* later should be handled by transaction initiator! */
        G_atmi_xa_curtx.txinfo->tmtxflags |= TMTXFLAGS_IS_ABORT_ONLY;
    }
                
    NDRX_LOG(log_debug, "%s return %d", fn, ret);
    return ret;
}

/**
 * Internal version to tpcall.
 * Support for flags:
 * TPNOTRAN    TPNOBLOCK
 * TPNOTIME    TPSIGRSTRT
 *
 * @param svc
 * @param idata
 * @param ilen
 * @param odata
 * @param olen
 * @param flags
 * @return
 */
public int _tpcall (char *svc, char *idata, long ilen,
                char * *odata, long *olen, long flags,
                char *extradata, int dest_node, int ex_flags)
{
    int ret=SUCCEED;
    char fn[] = "_tpcall";
    int cd_req = 0;
    int cd_rply = 0;
    TPTRANID tranid, *p_tranid;
    
    NDRX_LOG(log_debug, "%s: enter", fn);

    flags&=~TPNOBLOCK; /* we are working in sync (blocked) mode
                        * because we do want anser back! */
    
    if (flags & TPTRANSUSPEND)
    {
        memset(&tranid, 0, sizeof(tranid));
        p_tranid = &tranid;
    }
    else
    {
        p_tranid = NULL;
    }
    
    if (FAIL==(cd_req=_tpacall (svc, idata, ilen, flags, extradata, 
            dest_node, ex_flags, p_tranid)))
    {
        NDRX_LOG(log_error, "_tpacall to %s failed", svc);
        ret=FAIL;
        goto out;
    }
    else if (SUCCEED!=(ret=_tpgetrply(&cd_rply, cd_req, odata, olen, flags, 
            p_tranid)))
    {
        NDRX_LOG(log_error, "_tpgetrply to %s failed", svc);
        goto out;
    }

    /*
     * Did we get back what we asked for?
     */
    if (cd_req!=cd_rply)
    {
        ret=FAIL;
        _TPset_error_fmt(TPEPROTO, "%s: Got invalid reply! cd_req: %d, cd_rply: %d",
                                        fn, cd_req, cd_rply);
        goto out;
    }


out:
    NDRX_LOG(log_debug, "%s: return %d cd %d", fn, ret, cd_rply);

    return ret;
}

/**
 * Internal version to tpcancel
 * @param
 * @return
 */
public int	_tpcancel (int cd)
{
    int ret=SUCCEED;
    char fn[]="_tpcancel";
    
    NDRX_LOG(log_debug, "tpcancel issued for %d", cd);

    if (cd<1||cd>=MAX_ASYNC_CALLS)
    {
        _TPset_error_fmt(TPEBADDESC, "%s: Invalid call descriptor %d, should be 0<cd<%d",
                                        fn, cd, MAX_ASYNC_CALLS);
        ret=FAIL;
        goto out;
    }
    /* Mark call as cancelled, so that we could re-use it later. */
    G_call_state[cd].status = CALL_CANCELED;

out:
    return ret;
}


/**
 * ATMI standard
 * @return - pointer to int holding error code?
 */
public long * _exget_tpurcode_addr (void)
{
	return &M_svc_return_code;
}

/**
 * Get the queue name where we shall post the event!
 * @param send_q
 * @param extradata
 * @return 
 */
public int _get_evpost_sendq(char *send_q, char *extradata)
{
    int ret=SUCCEED;
    char tmp[NDRX_MAX_ID_SIZE];
    
    char binary[NDRX_MAX_ID_SIZE] = {EOS};
    int srvid = FAIL;
    int pid = FAIL;
    int nodeid = FAIL;
    long contextid = FAIL;
    char fn[] = "get_evpost_sendq";
    int i, len;
    
    if (NULL==extradata || EOS==extradata[0] || NULL==send_q)
    {
        NDRX_LOG(log_error, "Invalid arguments");
        ret=FAIL;
        goto out;
    }
    
    NDRX_LOG(log_debug, "%s: server's id=[%s]", fn, extradata);
    
    send_q[0] = EOS;
    
    strcpy(tmp, extradata);
    
    len = strlen(extradata);
    
    for (i=0; i<len; i++)
    {
        if (NDRX_FMT_SEP==tmp[i])
            tmp[i]=' ';
    }
    
    sscanf(tmp, NDRX_MY_ID_SRV_PARSE, binary, &srvid, &pid, &nodeid);
    
    NDRX_LOG(log_debug, "Parsed: binary=[%s] srvid=%d pid=%d nodeid=%d",
            binary, srvid, pid, nodeid);
    
    /*check is parsed ok?*/
    if (EOS==binary[0] || FAIL==srvid || FAIL==pid || FAIL==nodeid)
    {
        NDRX_LOG(log_warn, "Invalid server's my id: binary=[%s] "
                        "srvid=%d pid=%d nodeid=%d",
                        binary, srvid, pid, nodeid );
    }
    
    if (G_atmi_env.our_nodeid!=nodeid)
    {
        NDRX_LOG(log_debug, "Server is located on different server, "
                "our nodeid=%d their=%d",
                G_atmi_env.our_nodeid, nodeid);
        
        sprintf(send_q, NDRX_SVC_QBRDIGE, G_atmi_conf.q_prefix, nodeid);
    }
    else
    {
        NDRX_LOG(log_debug, "This is local server");
        sprintf(send_q, NDRX_ADMIN_FMT, G_atmi_conf.q_prefix, binary, srvid, pid);
    }
    
out:
                
    NDRX_LOG(log_debug, "%s returns send_q=[%s] ret=%d", 
                    fn, send_q, ret);

    return ret;
}

