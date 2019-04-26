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

static char **_filenamesArray = NULL;
static size_t _filenamesCount = 0;
static void _mock_rpm_free_filenames(void);

void mock_rpm_set_filenames(string_list_t *list) {
    string_entry_t *list_iter;
    int array_idx;

    assert(list);
    _mock_rpm_free_filenames();

    /* Figure out how big of an array we need. */
    _filenamesCount = list_len(list);

    /* Allocate an array of string pointers */
    _filenamesArray = calloc(_filenamesCount, sizeof(char *));
    assert(_filenamesArray != NULL);

    /* Walk the input list, and create a new array entry for each filename */
    array_idx = 0;
    TAILQ_FOREACH(list_iter, list, items) {
        _filenamesArray[array_idx] = strdup(list_iter->data);
        assert(_filenamesArray[array_idx] != NULL);

        array_idx++;
    }
}

static void _mock_rpm_free_filenames(void) {
    int i;

    if (_filenamesArray == NULL) {
        return;
    }

    for (i = 0; i < _filenamesCount; i++) {
        free(_filenamesArray[i]);
    }

    free(_filenamesArray);

    _filenamesArray = NULL;
    _filenamesCount = 0;
}

void mock_rpm_free(void) {
    _mock_rpm_free_filenames();
}

int __wrap_headerGet(Header h, rpmTagVal tag, rpmtd td, headerGetFlags flags) {
    rpmtdReset(td);
    rpmtdSetTag(td, tag);

    switch (tag) {
        case RPMTAG_FILENAMES:
            td->data = _filenamesArray;
            td->count = _filenamesCount;
            break;
        default:
            fprintf(stderr, "*** Unable to mock headerGet for tag %d\n", tag);
            fprintf(stderr, "*** Extend __wrap_headerGet in mock-rpm.c\n");
            abort();
    }

    return 0;
}
