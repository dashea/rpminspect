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

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <CUnit/Basic.h>
#include "rpminspect.h"
#include "test-listfuncs.h"

/* Make a string_list_t out each argument until it hits NULL */
string_list_t * make_list(const char *start, ...) {
    va_list ap;
    char *word;

    string_list_t *list;
    string_entry_t *entry;

    list = malloc(sizeof(*list));
    assert(list);

    TAILQ_INIT(list);

    if (start == NULL) {
        return list;
    }

    va_start(ap, start);

    for (word = start; word != NULL; word = va_arg(ap, char *)) {
        entry = calloc(1, sizeof(*entry));
        assert(entry);

        entry->data = strdup(word);
        assert(entry->data);

        TAILQ_INSERT_TAIL(list, entry, items);
    }

    va_end(ap);

    return list;
}
