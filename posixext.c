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
 * Copyright 2025 Fractal Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#include "posixext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#ifndef HAVE_TIMEGM
time_t timegm(struct tm *tm) {
    // Store the current timezone offset
    long original_offset = 0;
    time_t result;

    // Temporarily set the timezone to UTC
    setenv("TZ", "UTC0", 1);
    tzset();

    // Use mktime to convert the struct tm to time_t, assuming UTC
    result = mktime(tm);

    // Reset timezone to the original
    unsetenv("TZ");
    tzset();

    return result;
}
#endif

typedef struct {
    FILE *stream;
    ssize_t (*read)(void *cookie, char *buf, size_t size);
    ssize_t (*write)(void *cookie, const char *buf, size_t size);
    off_t (*seek)(void *cookie, off_t offset, int whence);
    int (*close)(void *cookie);
    void *cookie;
} custom_file;

static ssize_t custom_read(void *cookie, char *buf, size_t size) {
    custom_file *cfile = (custom_file *)cookie;
    return cfile->read(cfile->cookie, buf, size);
}

static ssize_t custom_write(void *cookie, const char *buf, size_t size) {
    custom_file *cfile = (custom_file *)cookie;
    return cfile->write(cfile->cookie, buf, size);
}

static off_t custom_seek(void *cookie, off_t offset, int whence) {
    custom_file *cfile = (custom_file *)cookie;
    return cfile->seek(cfile->cookie, offset, whence);
}

static int custom_close(void *cookie) {
    custom_file *cfile = (custom_file *)cookie;
    return cfile->close(cfile->cookie);
}

FILE *funopen(void *cookie, 
              ssize_t (*read)(void *cookie, char *buf, size_t size),
              ssize_t (*write)(void *cookie, const char *buf, size_t size),
              off_t (*seek)(void *cookie, off_t offset, int whence),
              int (*close)(void *cookie)) {
    custom_file *cfile;

    // Allocate memory for the custom file structure
    cfile = (custom_file *)malloc(sizeof(custom_file));
    if (cfile == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    // Set up the function pointers and the cookie
    cfile->read = read;
    cfile->write = write;
    cfile->seek = seek;
    cfile->close = close;
    cfile->cookie = cookie;

    // Return a FILE pointer that wraps the custom file operations
    return fdopen(fileno(stdin), "r+"); // This part would need modification to integrate properly
}
struct memstream {
    char **bufptr;
    size_t *sizeptr;
    size_t capacity;
    FILE *stream;
};

static int memstream_grow(struct memstream *ms, size_t new_capacity) {
    char *new_buf = realloc(*ms->bufptr, new_capacity);
    if (!new_buf) {
        return -1; // Allocation failed
    }

    *ms->bufptr = new_buf;
    memset(new_buf + ms->capacity, 0, new_capacity - ms->capacity);
    ms->capacity = new_capacity;
    return 0;
}

static int memstream_write(void *cookie, const char *data, int size) {
    struct memstream *ms = (struct memstream *)cookie;
    size_t pos = ftell(ms->stream);

    if (pos + size >= ms->capacity) {
        if (memstream_grow(ms, (pos + size) * 2) != 0) {
            return -1;
        }
    }

    memcpy((*ms->bufptr) + pos, data, size);
    fseek(ms->stream, pos + size, SEEK_SET);
    *ms->sizeptr = ftell(ms->stream);
    return size;
}

static int memstream_seek(void *cookie, fpos_t *pos, int whence) {
    return fseek(((struct memstream *)cookie)->stream, *pos, whence);
}

static int memstream_close(void *cookie) {
    struct memstream *ms = (struct memstream *)cookie;
    fclose(ms->stream);
    free(ms);
    return 0;
}

FILE *open_memstream(char **bufptr, size_t *sizeptr) {
    struct memstream *ms = malloc(sizeof(struct memstream));
    if (!ms) {
        return NULL;
    }

    ms->bufptr = bufptr;
    ms->sizeptr = sizeptr;
    ms->capacity = 128;

    *bufptr = malloc(ms->capacity);
    if (!*bufptr) {
        free(ms);
        return NULL;
    }

    memset(*bufptr, 0, ms->capacity);
    *sizeptr = 0;

    ms->stream = fmemopen(*bufptr, ms->capacity, "w+");
    if (!ms->stream) {
        free(*bufptr);
        free(ms);
        return NULL;
    }

    return funopen(ms, memstream_write, NULL, memstream_seek, memstream_close);
}
typedef struct {
    unsigned char *buffer;  // Memory buffer
    size_t size;            // Size of the buffer
    size_t pos;             // Current position
    size_t end;             // End of written data
    int writable;           // Writable flag
    int readable;           // Readable flag
} MemFile;

static int memfile_close(void *cookie) {
    free(cookie);
    return 0;
}

static ssize_t memfile_read(void *cookie, char *buf, size_t size) {
    MemFile *memfile = (MemFile *)cookie;

    if (!memfile->readable) {
        return 0;
    }

    size_t available = memfile->end - memfile->pos;
    size_t to_read = size < available ? size : available;

    memcpy(buf, memfile->buffer + memfile->pos, to_read);
    memfile->pos += to_read;

    return to_read;
}

static ssize_t memfile_write(void *cookie, const char *buf, size_t size) {
    MemFile *memfile = (MemFile *)cookie;

    if (!memfile->writable) {
        return 0;
    }

    size_t available = memfile->size - memfile->pos;
    size_t to_write = size < available ? size : available;

    memcpy(memfile->buffer + memfile->pos, buf, to_write);
    memfile->pos += to_write;

    if (memfile->pos > memfile->end) {
        memfile->end = memfile->pos;
    }

    return to_write;
}

static int memfile_seek(void *cookie, fpos_t *pos, int whence) {
    MemFile *memfile = (MemFile *)cookie;

    size_t new_pos;
    switch (whence) {
        case SEEK_SET:
            new_pos = *pos;
            break;
        case SEEK_CUR:
            new_pos = memfile->pos + *pos;
            break;
        case SEEK_END:
            new_pos = memfile->end + *pos;
            break;
        default:
            return -1;
    }

    if (new_pos > memfile->size) {
        return -1;
    }

    memfile->pos = new_pos;
    return 0;
}

FILE *fmemopen(void *buf, size_t size, const char *mode) {
    if (!buf || !mode) {
        errno = EINVAL;
        return NULL;
    }

    MemFile *memfile = calloc(1, sizeof(MemFile));
    if (!memfile) {
        return NULL;
    }

    memfile->buffer = buf;
    memfile->size = size;

    if (strchr(mode, 'r')) {
        memfile->readable = 1;
    }
    if (strchr(mode, 'w') || strchr(mode, 'a')) {
        memfile->writable = 1;
        if (strchr(mode, 'w')) {
            memset(buf, 0, size);
        }
    }

    return funopen(memfile, memfile_read, memfile_write, memfile_seek, memfile_close);

}
