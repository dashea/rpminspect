/*
 * Copyright (C) 2019  Red Hat, Inc.
 * Author(s):  David Shea <dshea@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Mock rpm Header interface.
 * To use, add '-Wl,--wrap=headerGet' to the test program's LDFLAGS
 */

#include "config.h"

#include <assert.h>
#include <sys/queue.h>

#include <rpm/rpmtd.h>

#include "rpminspect.h"
#include "mock-rpm.h"

/* Struct to use instead of rpm's Header */
struct _wrappedHeader {
    char         **filenamesArray;
    size_t       filenamesCount;
    unsigned int refcount;
};

static void _mock_rpm_free_filenames(struct _wrappedHeader *h) {
    unsigned int i;

    assert(h);

    if (h->filenamesArray == NULL) {
        return;
    }

    for (i = 0; i < h->filenamesCount; i++) {
        free(h->filenamesArray[i]);
    }

    free(h->filenamesArray);

    h->filenamesArray = NULL;
    h->filenamesCount = 0;
}

void mock_rpm_set_filenames(Header rpmHeader, string_list_t *list) {
    struct _wrappedHeader *h = (struct _wrappedHeader *) rpmHeader;
    string_entry_t *list_iter;
    int array_idx;

    assert(list);
    _mock_rpm_free_filenames(h);

    /* Figure out how big of an array we need. */
    h->filenamesCount = list_len(list);

    /* Allocate an array of string pointers */
    h->filenamesArray = calloc(h->filenamesCount, sizeof(char *));
    assert(h->filenamesArray != NULL);

    /* Walk the input list, and create a new array entry for each filename */
    array_idx = 0;
    TAILQ_FOREACH(list_iter, list, items) {
        h->filenamesArray[array_idx] = strdup(list_iter->data);
        assert(h->filenamesArray[array_idx] != NULL);

        array_idx++;
    }
}

Header __wrap_headerNew(void) {
    struct _wrappedHeader *h;

    h = calloc(1, sizeof(*h));
    assert(h);

    h->refcount = 1;

    return (Header) h;
}

Header __wrap_headerFree(Header rpmHeader) {
    struct _wrappedHeader *h = (struct _wrappedHeader *) rpmHeader;

    if (h == NULL) {
        return NULL;
    }

    h->refcount--;
    if (h->refcount == 0) {
        _mock_rpm_free_filenames(h);
        free(h);
    }

    return NULL;
}

Header __wrap_headerLink(Header rpmHeader) {
    struct _wrappedHeader *h = (struct _wrappedHeader *) rpmHeader;

    if (h != NULL) {
        h->refcount++;
    }

    return rpmHeader;
}

int __wrap_headerGet(Header rpmHeader, rpmTagVal tag, rpmtd td, headerGetFlags flags __attribute__((unused))) {
    struct _wrappedHeader *h = (struct _wrappedHeader *) rpmHeader;

    rpmtdReset(td);
    rpmtdSetTag(td, tag);

    switch (tag) {
        case RPMTAG_FILENAMES:
            td->data = h->filenamesArray;
            td->count = h->filenamesCount;
            break;
        default:
            fprintf(stderr, "*** Unable to mock headerGet for tag %d\n", tag);
            fprintf(stderr, "*** Extend __wrap_headerGet in mock-rpm.c\n");
            abort();
    }

    return 0;
}
