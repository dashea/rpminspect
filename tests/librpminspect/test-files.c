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

#include "config.h"

#include <CUnit/Basic.h>
#include "rpminspect.h"
#include "test-main.h"
#include "mock-rpm.h"

int init_test_file(void) {
    return 0;
}

int clean_test_file(void) {
    mock_rpm_free();
    return 0;
}

void test_mock(void) {
    string_list_t *list;
    rpmtd td;

    td = rpmtdNew();

    list = make_list("one", "two", "three", NULL);
    mock_rpm_set_filenames(list);
    list_free(list, free);

    headerGet(NULL, RPMTAG_FILENAMES, td, 0);
    RI_ASSERT_EQUAL(rpmtdCount(td), 3);

    rpmtdSetIndex(td, 1);
    RI_ASSERT_STRING_EQUAL(rpmtdGetString(td), "two");

    rpmtdFree(td);
}

bool add_test_suites(void) {
    CU_pSuite pSuite;

    pSuite = CU_add_suite("files", init_test_file, clean_test_file);
    if (pSuite == NULL) {
        return false;
    }

    if (CU_add_test(pSuite, "test mock functionality", test_mock) == NULL) {
        return false;
    }

    return true;
}
