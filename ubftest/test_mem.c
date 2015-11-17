/* 
**
** @file test_mem.c
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

#include <stdio.h>
#include <stdlib.h>
#include <cgreen/cgreen.h>
#include <ubf.h>
#include <ndrstandard.h>
#include <string.h>
#include "test.fd.h"
#include "ubfunit1.h"

/**
 * Test Balloc
 * @return
 */
void test_Balloc_Bfree(void)
{
    UBFH *p_ub = NULL;
    int i;
    /* will check with valgrind - do we have memory leaks or not */
    
    for (i=0; i<20; i++)
    {
        p_ub=Balloc(20, 30);
        assert_not_equal(p_ub, NULL);
        /* Put some data into memory so that we can test */
        set_up_dummy_data(p_ub);
        do_dummy_data_test(p_ub);
        assert_equal(Bfree(p_ub), SUCCEED);

    }
}

/**
 * Basic test for reallocation
 */
void test_Brealloc(void)
{
    UBFH *p_ub = NULL;

    p_ub=Balloc(1, 30);
    assert_not_equal(p_ub, NULL);

    assert_equal(Badd(p_ub, T_STRING_FLD, BIG_TEST_STRING, 0), FAIL);
    assert_equal(Berror, BNOSPACE);

    /* Now reallocate, space should be bigger! */
    p_ub=Brealloc(p_ub, 1, strlen(BIG_TEST_STRING)+1+2/* align */);
    assert_not_equal(p_ub, NULL);
    assert_equal(Badd(p_ub, T_STRING_FLD, BIG_TEST_STRING, 0), SUCCEED);
    
    /* should not allow to reallocate to 0! */
    assert_equal(Brealloc(p_ub, 1, 0), NULL);
    assert_equal(Berror, BEINVAL);

    /* should be bigger than existing. */
    assert_equal(Brealloc(p_ub, 1, strlen(BIG_TEST_STRING)+1), NULL);
    assert_equal(Berror, BEINVAL);

    assert_equal(SUCCEED, Bfree(p_ub));

}

TestSuite *ubf_mem_tests(void)
{
    TestSuite *suite = create_test_suite();

    add_test(suite, test_Balloc_Bfree);
    add_test(suite, test_Brealloc);

    return suite;
}
