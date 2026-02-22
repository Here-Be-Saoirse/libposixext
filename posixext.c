#include "posixext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://illumos.org/license/CDDL.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2026 Fractal Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
*	ident	"@(#)	posixext.c	1.6	26/02/21"
*/

#ifndef HAVE_TIMEGM
time_t timegm(struct tm *tm) {
    time_t result;
    char *original_tz = getenv("TZ");
    char *saved_tz = NULL;

    // Save original TZ value if it was set
    if (original_tz != NULL) {
        saved_tz = strdup(original_tz);
        if (!saved_tz) {
            return (time_t)-1;
        }
    }

    // Temporarily set the timezone to UTC
    setenv("TZ", "UTC0", 1);
    tzset();

    // Use mktime to convert the struct tm to time_t, assuming UTC
    result = mktime(tm);

    // Restore original timezone
    if (saved_tz != NULL) {
        setenv("TZ", saved_tz, 1);
        free(saved_tz);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return result;
}
#endif

// funopen() is a BSD extension not available on Solaris/Linux.
// We implement it here using fopencookie(), which is the POSIX/Linux equivalent.
typedef struct {
    ssize_t (*read)(void *cookie, char *buf, size_t size);
    ssize_t (*write)(void *cookie, const char *buf, size_t size);
    off_t (*seek)(void *cookie, off_t offset, int whence);
    int (*close)(void *cookie);
    void *cookie;
} funopen_cookie;

static ssize_t funopen_read(void *cookie, char *buf, size_t size) {
    funopen_cookie *fc = (funopen_cookie *)cookie;
    return fc->read(fc->cookie, buf, size);
}

static ssize_t funopen_write(void *cookie, const char *buf, size_t size) {
    funopen_cookie *fc = (funopen_cookie *)cookie;
    return fc->write(fc->cookie, buf, size);
}

static int funopen_seek(void *cookie, off_t *offset, int whence) {
    funopen_cookie *fc = (funopen_cookie *)cookie;
    off_t result = fc->seek(fc->cookie, *offset, whence);
    if (result < 0) {
        return -1;
    }
    *offset = result;
    return 0;
}

static int funopen_close(void *cookie) {
    funopen_cookie *fc = (funopen_cookie *)cookie;
    int result = 0;
    if (fc->close) {
        result = fc->close(fc->cookie);
    }
    free(fc);
    return result;
}

static FILE *funopen(void *cookie,
              ssize_t (*read)(void *cookie, char *buf, size_t size),
              ssize_t (*write)(void *cookie, const char *buf, size_t size),
              off_t (*seek)(void *cookie, off_t offset, int whence),
              int (*close)(void *cookie)) {
    funopen_cookie *fc = malloc(sizeof(funopen_cookie));
    if (!fc) {
        errno = ENOMEM;
        return NULL;
    }

    fc->read   = read;
    fc->write  = write;
    fc->seek   = seek;
    fc->close  = close;
    fc->cookie = cookie;

    cookie_io_functions_t io = {
        .read  = read  ? funopen_read  : NULL,
        .write = write ? funopen_write : NULL,
        .seek  = seek  ? funopen_seek  : NULL,
        .close = funopen_close,
    };

    const char *mode = (read && write) ? "r+" : (write ? "w" : "r");
    FILE *fp = fopencookie(fc, mode, io);
    if (!fp) {
        free(fc);
        return NULL;
    }
    return fp;
}

/* ---------- open_memstream ---------- */

typedef struct {
    char   **bufptr;
    size_t  *sizeptr;
    size_t   capacity;
    size_t   pos;       // current write position
} memstream;

static int memstream_grow(memstream *ms, size_t needed) {
    size_t new_cap = ms->capacity * 2;
    if (new_cap < needed) new_cap = needed;

    char *new_buf = realloc(*ms->bufptr, new_cap);
    if (!new_buf) {
        return -1;
    }

    memset(new_buf + ms->capacity, 0, new_cap - ms->capacity);
    *ms->bufptr  = new_buf;
    ms->capacity = new_cap;
    return 0;
}

static ssize_t memstream_write(void *cookie, const char *data, size_t size) {
    memstream *ms = (memstream *)cookie;

    // Ensure there is room for data plus a NUL terminator
    if (ms->pos + size + 1 > ms->capacity) {
        if (memstream_grow(ms, ms->pos + size + 1) != 0) {
            return -1;
        }
    }

    memcpy(*ms->bufptr + ms->pos, data, size);
    ms->pos += size;

    // Update the caller's size and keep buffer NUL-terminated
    if (ms->pos > *ms->sizeptr) {
        *ms->sizeptr = ms->pos;
    }
    (*ms->bufptr)[*ms->sizeptr] = '\0';

    return (ssize_t)size;
}

static off_t memstream_seek(void *cookie, off_t offset, int whence) {
    memstream *ms = (memstream *)cookie;
    off_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (off_t)ms->pos + offset;
            break;
        case SEEK_END:
            new_pos = (off_t)*ms->sizeptr + offset;
            break;
        default:
            return (off_t)-1;
    }

    if (new_pos < 0) {
        return (off_t)-1;
    }

    ms->pos = (size_t)new_pos;
    return new_pos;
}

static int memstream_close(void *cookie) {
    free(cookie);
    return 0;
}

FILE *open_memstream(char **bufptr, size_t *sizeptr) {
    if (!bufptr || !sizeptr) {
        errno = EINVAL;
        return NULL;
    }

    memstream *ms = malloc(sizeof(memstream));
    if (!ms) {
        return NULL;
    }

    ms->bufptr   = bufptr;
    ms->sizeptr  = sizeptr;
    ms->capacity = 128;
    ms->pos      = 0;

    *bufptr = malloc(ms->capacity);
    if (!*bufptr) {
        free(ms);
        return NULL;
    }

    memset(*bufptr, 0, ms->capacity);
    *sizeptr = 0;

    FILE *fp = funopen(ms, NULL, memstream_write, memstream_seek, memstream_close);
    if (!fp) {
        free(*bufptr);
        *bufptr = NULL;
        free(ms);
        return NULL;
    }
    return fp;
}

/* ---------- fmemopen ---------- */

typedef struct {
    unsigned char *buffer;
    size_t size;
    size_t pos;
    size_t end;
    int    writable;
    int    readable;
} MemFile;

static int memfile_close(void *cookie) {
    free(cookie);
    return 0;
}

static ssize_t memfile_read(void *cookie, char *buf, size_t size) {
    MemFile *memfile = (MemFile *)cookie;

    if (!memfile->readable) {
        errno = EBADF;
        return -1;
    }

    size_t available = memfile->end - memfile->pos;
    size_t to_read   = size < available ? size : available;

    memcpy(buf, memfile->buffer + memfile->pos, to_read);
    memfile->pos += to_read;

    return (ssize_t)to_read;
}

static ssize_t memfile_write(void *cookie, const char *buf, size_t size) {
    MemFile *memfile = (MemFile *)cookie;

    if (!memfile->writable) {
        errno = EBADF;
        return -1;
    }

    size_t available = memfile->size - memfile->pos;
    size_t to_write  = size < available ? size : available;

    memcpy(memfile->buffer + memfile->pos, buf, to_write);
    memfile->pos += to_write;

    if (memfile->pos > memfile->end) {
        memfile->end = memfile->pos;
    }

    return (ssize_t)to_write;
}

static off_t memfile_seek(void *cookie, off_t offset, int whence) {
    MemFile *memfile = (MemFile *)cookie;
    off_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (off_t)memfile->pos + offset;
            break;
        case SEEK_END:
            new_pos = (off_t)memfile->end + offset;
            break;
        default:
            return (off_t)-1;
    }

    if (new_pos < 0 || (size_t)new_pos > memfile->size) {
        return (off_t)-1;
    }

    memfile->pos = (size_t)new_pos;
    return new_pos;
}

FILE *fmemopen(void *buf, size_t size, const char *mode) {
    if (!buf || !mode || size == 0) {
        errno = EINVAL;
        return NULL;
    }

    MemFile *memfile = calloc(1, sizeof(MemFile));
    if (!memfile) {
        return NULL;
    }

    memfile->buffer = (unsigned char *)buf;
    memfile->size   = size;

    // Parse mode string, including '+' for read+write
    if (strchr(mode, 'r')) {
        memfile->readable = 1;
        // For 'r', the entire buffer contents are available to read
        memfile->end = size;
    }
    if (strchr(mode, 'w')) {
        memfile->writable = 1;
        memset(buf, 0, size);
        memfile->end = 0;
    }
    if (strchr(mode, 'a')) {
        memfile->writable = 1;
        // Append: find end of existing string or end of buffer
        size_t len = strnlen((char *)buf, size);
        memfile->pos = len;
        memfile->end = len;
    }
    if (strchr(mode, '+')) {
        memfile->readable = 1;
        memfile->writable = 1;
    }

    return funopen(memfile,
                   memfile->readable ? memfile_read  : NULL,
                   memfile->writable ? memfile_write : NULL,
                   memfile_seek,
                   memfile_close);
}
