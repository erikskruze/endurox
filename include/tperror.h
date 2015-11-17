/* 
** Enduro/X ATMI Error handling header
**
** @file tperror.h
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
#ifndef TPERROR_H
#define	TPERROR_H

#ifdef	__cplusplus
extern "C" {
#endif

/*---------------------------Includes-----------------------------------*/
#include <regex.h>
#include <ndrstandard.h>
#include <ubf_int.h>
/*---------------------------Externs------------------------------------*/
/*---------------------------Macros-------------------------------------*/
#define MAX_TP_ERROR_LEN   1024
/*---------------------------Enums--------------------------------------*/
/*---------------------------Typedefs-----------------------------------*/
    
/**
 * ATMI error handler save/restore
 */
typedef struct
{
    char atmi_error_msg_buf[MAX_TP_ERROR_LEN+1];
    int atmi_error;
    short atmi_reason;
} atmi_error_t;

/*---------------------------Globals------------------------------------*/
/*---------------------------Statics------------------------------------*/
/*---------------------------Prototypes---------------------------------*/

extern void _TPset_error(int error_code);
extern void _TPset_error_msg(int error_code, char *msg);
extern void _TPset_error_fmt(int error_code, const char *fmt, ...);
public void _TPset_error_fmt_rsn(int error_code, short reason, const char *fmt, ...);
extern void _TPunset_error(void);
extern int _TPis_error(void);
extern void _TPappend_error_msg(char *msg);
extern void _TPoverride_code(int error_code);

extern void _TPsave_error(atmi_error_t *p_err);
extern void _TPrestore_error(atmi_error_t *p_err);

/* xa error handling */
extern void atmi_xa_set_error(UBFH *p_ub, short error_code, short reason);
extern void atmi_xa_set_error_msg(UBFH *p_ub, short error_code, short reason, char *msg);
extern void atmi_xa_set_error_fmt(UBFH *p_ub, short error_code, short reason, const char *fmt, ...);
extern void atmi_xa_override_error(UBFH *p_ub, short error_code);
extern void atmi_xa_unset_error(UBFH *p_ub);
extern int atmi_xa_is_error(UBFH *p_ub);
extern void atmi_xa_error_msg(UBFH *p_ub, char *msg);
extern void atmi_xa2tperr(UBFH *p_ub);
extern char *atmi_xa_geterrstr(int code);
extern void atmi_xa_approve(UBFH *p_ub);
extern short atmi_xa_get_reason(void);
#ifdef	__cplusplus
}
#endif

#endif	/* TPERROR_H */
