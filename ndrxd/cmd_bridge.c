/* 
** Bridge command back-ends.
**
** @file cmd_bridge.c
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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <utlist.h>

#include <ndrstandard.h>

#include <ndebug.h>
#include <userlog.h>
#include <ndrxd.h>
#include <ndrxdcmn.h>

#include "cmd_processor.h"
#include "bridge_int.h"
#include "atmi_shm.h"
/*---------------------------Externs------------------------------------*/
/*---------------------------Macros-------------------------------------*/
/*---------------------------Enums--------------------------------------*/
/*---------------------------Typedefs-----------------------------------*/
/*---------------------------Globals------------------------------------*/
/*---------------------------Statics------------------------------------*/
/*---------------------------Prototypes---------------------------------*/

/**
 * Bridge connection established. If needed we can now start to send the stuff.
 * @param call
 * @param data
 * @param len
 * @param context
 * @return 
 */
public int cmd_brcon (command_call_t * call, char *data, size_t len, int context)
{
    int ret=SUCCEED;
    bridge_info_t *brcall = (bridge_info_t *)call;
    
    /* Will not check for error, bad call could case server to exit. */
    ndrx_shm_bridge_connected(brcall->nodeid);
    
    brd_connected(brcall->nodeid);
    
out:
    return ret;
}

/**
 * The bridge have been disconnected - remove all services from shm!
 * @param call
 * @param data
 * @param len
 * @param context
 * @return 
 */
public int cmd_brdiscon (command_call_t * call, char *data, size_t len, int context)
{
    int ret=SUCCEED;
    bridge_info_t *brcall = (bridge_info_t *)call;

    /* Will not check for error, bad call could case server to exit. */
    ndrx_shm_bridge_disco(brcall->nodeid);
    
    brd_discconnected(brcall->nodeid);
    
out:
    return ret;
}

/**
 * Update shared mem.
 * @param svc_nm - service name
 * @param count - final count.
 * @return 
 */
public int brd_lock_and_update_shm(int nodeid, char *svc_nm, int count, char mode)
{
    int ret=SUCCEED;
    
    /* ###################### CRITICAL SECTION ###################### */
    /* So we make this part critical... */
    if (SUCCEED!=ndrx_lock_svc_op())
    {
        ret=FAIL;
        goto out;
    }

    ret=ndrx_shm_install_svc_br(svc_nm, 0, 
                TRUE, nodeid, count, mode);


    /* Remove the lock! */
    ndrx_unlock_svc_op();
    /* ###################### CRITICAL SECTION, END ################# */
    
out:
    return ret;
}

/**
 * Bridge sends us refersh command.
 * @param call
 * @param data
 * @param len
 * @param context
 * @return 
 */
public int cmd_brrefresh (command_call_t * call, char *data, size_t len, int context)
{
    int ret=SUCCEED;
    bridge_refresh_t *refresh = (bridge_refresh_t *)call;
    int i, j;
    bridgedef_t* br = brd_get_bridge(call->caller_nodeid);
    bridgedef_svcs_t * svc_ent;
    bridge_refresh_svc_t *ref_ent;
    
    /* We want temporary hash  */
    bridgedef_svcs_t *svcref= NULL;
    
    if (br==NULL)
    {
        NDRX_LOG(log_error, "Bridge %d not found!", call->caller_nodeid);
        goto out;
    }
    
    /* So we get the full list we should iter over */
    for (i=0; i<refresh->count; i++)
    {
        NDRX_LOG(log_error, "Got service from node: %d - [%s] count: [%d], mode: [%c]", 
                refresh->call.caller_nodeid,
                refresh->svcs[i].svc_nm,
                refresh->svcs[i].count,
                refresh->svcs[i].mode);
        ref_ent = &refresh->svcs[i];
        
        if (BRIDGE_REFRESH_MODE_FULL==refresh->mode)
        {
            brd_add_svc_to_hash_g(&svcref, ref_ent->svc_nm);
        }
        
        /* So now we should crosscheck the hash tables, if missing, then install
         * actually if service count changes, then we do not need to lock the SHM.
         */
        
        /* Update shared mem. */
        if (SUCCEED!=brd_lock_and_update_shm(br->nodeid, 
                ref_ent->svc_nm, ref_ent->count, ref_ent->mode))
        {
            NDRX_LOG(log_error, "Failed to update shared mem for [%s]/%d/%c!",
                    ref_ent->svc_nm, ref_ent->count, ref_ent->mode);
            FAIL_OUT(ret);
        }
        
        if (NULL!=(svc_ent = brd_get_svc_brhash(br, refresh->svcs[i].svc_nm)))
        {
            /* So it was already known to the system, check the mode */
            if (ref_ent->mode==BRIDGE_REFRESH_MODE_FULL)
            {
                svc_ent->count=ref_ent->count;
            }
            else
            {
		/* We receive signed delta */
                svc_ent->count+=ref_ent->count;
                NDRX_LOG(log_debug, "Bridge %d svc hash [%s] count: %d",
                        br->nodeid, ref_ent->svc_nm, svc_ent->count);
            }
            
            /* fix - remove zero items from hash... */
            if (0==svc_ent->count)
            {
                NDRX_LOG(log_warn, "Service count 0 - remove from br hash");
                brd_del_svc_brhash(br, svc_ent, ref_ent->svc_nm);
            }
            
        }
        else
        {
            /* Service not present in hash - so we should add, if it is DIFF >0, then add */
            if (ref_ent->count > 0)
            {
                NDRX_LOG(log_debug, "Adding service to bridge svc hash");
                /* Add that stuff to bridge! We run in full mode. */
                if (SUCCEED!=brd_add_svc_brhash(br, ref_ent->svc_nm, 
                        ref_ent->count))
                {
                    FAIL_OUT(ret);
                }
            }
        }
    } /* For each refresh entry */
    
    /* Go over the list of services in bridge */
    if (BRIDGE_REFRESH_MODE_FULL==refresh->mode)
    {
        bridgedef_svcs_t *r = NULL;
        bridgedef_svcs_t *rtmp = NULL;
        
        /* Check every their service (what we see in system)
         * in their refresh message, if not there, then we should erase 
         * it from our view
         */
        HASH_ITER(hh, br->theyr_services, r, rtmp)
        {
            NDRX_LOG(log_debug, "Cross checking service [%s] in "
                                            "refresh msg", r->svc_nm);
            if (NULL==brd_get_svc(svcref, r->svc_nm))
            {
                NDRX_LOG(log_warn, "Service [%s] not present in refresh "
                "message, but we see it our view - there was some packet loss! - "
                        "Removing it from our view!", r->svc_nm);
                /* Remove stuff from SHM.... */
                brd_lock_and_update_shm(br->nodeid, r->svc_nm, 0, BRIDGE_REFRESH_MODE_FULL);
                /* Remove from hash view! */
                brd_del_svc_brhash(br, r, r->svc_nm);
            }
            else
            {
                NDRX_LOG(log_debug, "Service [%s] present in refresh - OK", 
                        r->svc_nm);
            }
        }
    }
    
out:

    /* if svcref is not NULL, then clean up the list! */
    if (NULL!=svcref)
    {
        /* Erase the list to avoid memory leak..! */
        brd_erase_svc_hash_g(svcref);
        svcref=NULL;
    }

    return ret;
}



/**
 * Do the actual work, generate list of connected bridges...!
 * String will be filled from start to end with node id's in random order.
 * @param reply
 * @param send_size
 * @param params
 */
public void getbrs_reply_mod(command_reply_t *reply, size_t *send_size, mod_param_t *params)
{
    command_reply_getbrs_t * info = (command_reply_getbrs_t *)reply;
    bridgedef_t *cur = NULL;
    bridgedef_t *rtmp = NULL;
    int pos = 0;

    reply->msg_type = NDRXD_CALL_TYPE_GETBRS;
    /* calculate new send size */
    *send_size += (sizeof(command_reply_getbrs_t) - sizeof(command_reply_t));
    
    memset(info->nodes, 0, sizeof(info->nodes));
    
    HASH_ITER(hh, G_bridge_hash, cur, rtmp)
    {    
        if (cur->connected)
        {
            info->nodes[pos] = (char)cur->nodeid;
            pos++;
        } /* If connected */
    } /* HASH_ITER */
    
    br_dump_nodestack(info->nodes, "Returning list of connected nodes");

    NDRX_LOG(log_debug, "magic: %ld", info->rply.magic);
}

/**
 * Return list of connected bridges..
 * @param call
 * @param data
 * @param len
 * @param context
 * @return 
 */
public int cmd_getbrs (command_call_t * call, char *data, size_t len, int context)
{
    int ret=SUCCEED;

    NDRX_LOG(log_warn, "cmd_getbrs: call flags 0x%x", call->flags);

    /* Generate list of connected bridges... (in callback) */
    if (SUCCEED!=simple_command_reply(call, ret, 0L, NULL, getbrs_reply_mod, 0L))
    {
        userlog("Failed to send reply back to [%s]", call->reply_queue);
    }
    
    NDRX_LOG(log_warn, "cmd_getbrs returns with status %d", ret);
    
out:
    return ret;
}