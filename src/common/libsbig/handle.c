/*****************************************************************************\
 *  Copyright (c) 2014 Jim Garlick All rights reserved.
 *
 *  This file is part of the sbig-util.
 *  For details, see https://github.com/garlick/sbig-util.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 3 of the license, or (at your option)
 *  any later version.
 *
 *  sbig-util is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>

#include "handle.h"
#include "handle_impl.h"
#include "sbigudrv.h"

sbig_t *sbig_new (void)
{
    sbig_t *sb = malloc (sizeof (*sb));
    if (!sb) {
        errno = ENOMEM;
        return NULL;
    }
    memset (sb, 0, sizeof (*sb));
    return sb;
}

int sbig_dlopen (sbig_t *sb, const char *path)
{
    if (!path)
        path = "libsbig.so";
    dlerror ();
    if (!(sb->dso = dlopen (path, RTLD_LAZY | RTLD_LOCAL)))
        return CE_OS_ERROR;
    if (!(sb->fun = dlsym (sb->dso, "SBIGUnivDrvCommand")))
        return CE_OS_ERROR;
    return CE_NO_ERROR;
}

void sbig_destroy (sbig_t *sb)
{
    if (sb->dso)
        dlclose (sb->dso);
    free (sb);
}

const char *sbig_get_error_string (sbig_t *sb, unsigned short errorNo)
{
    GetErrorStringParams in = { .errorNo = errorNo };
    static GetErrorStringResults out;
    static char unknown[] = "unknown error XXXXXXXX";
    int e = sb->fun (CC_GET_ERROR_STRING, &in, &out);
    if (e != CE_NO_ERROR) {
        snprintf (unknown + 14, sizeof (unknown) - 14, "%d", errorNo);
        return unknown;
    }
    return (const char *)out.errorString;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
