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
 * Read an RPM header from a JSON file.
 *
 * The JSON file should be in $srcdir/data. This function takes of adding
 * the directory prefix, so just call with, e.g., getRpmHeader("test-find-file-peers-1.json").
 *
 * This file needs to be compiled with -DSRCDIR=$(srcdir) in the CPPFLAGS, as well as
 * $(JSON_C_CFLAGS) and $(JSON_C_LIBS) in the appropriate places.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <rpm/header.h>
#include <rpm/rpmtag.h>

#include <json.h>

static int _addHeaderString(Header h, rpmTagVal tag, const char *key, json_object *val) {
    if (headerPutString(h, tag, json_object_get_string(val)) == 0) {
        fprintf(stderr, "*** Error inserting value for %s\n", key);
        return -1;
    }

    return 0;
}

static int _addHeaderInt(Header h, rpmTagVal tag, rpmTagType tagType, const char *key, json_object *val) {
    char c;
    int64_t i;
    uint16_t i16;
    uint32_t i32;

    /* JSON-C doesn't have unsigned types, so get the int as the biggest type it supports and convert down */
    /* Per the docs, reset and check errno to check for errors in returning the int */
    errno = 0;
    i = json_object_get_int64(val);

    if ((errno != 0) || (i < 0)) {
        fprintf(stderr, "*** Invalid value %s for %s\n", json_object_get_string(val), key);
        return -1;
    }

    switch (tagType) {
        case RPM_CHAR_TYPE: 
            if (i > UINT8_MAX) {
                fprintf(stderr, "*** Invalid value %u for %s\n", i, key);
                return -1;
            }

            c = (char) i;
            
            if (headerPutChar(h, tag, &c, 1) == 0) {
                fprintf(stderr, "*** Error inserting value for %s\n", key);
                return -1;
            }
            
            break;
        case RPM_INT16_TYPE:
            if (i > UINT16_MAX) {
                fprintf(stderr, "*** Invalid value %u for %s\n", i, key);
                return -1;
            }

            i16 = (uint16_t) i;

            if (headerPutUint16(h, tag, &i16, 1) == 0) {
                fprintf(stderr, "*** Error inserting value for %s\n", key);
                return -1;
            }

            break;

        case RPM_INT32_TYPE:
            if (headerPutUint32(h, tag, &i, 1) == 0) {
                fprintf(stderr, "*** Error inserting value for %s\n", key);
                return -1;
            }

            break;

        default:
            fprintf(stderr, "*** Unexpected type %d for %s\n", tagType, key);
            return -1;
    }

    return 0;
}

static int _addHeaderArray(Header h, rpmTagVal tag, rpmTagType tagType, const char *key, json_object *val) {
    size_t array_len;
    size_t i;
    json_object *iter;

    rpmTagType tagType;
    json_type exepctedType;
    json_type actualType;

    /* Figure out what type the elements are supposed to be */
    switch (tagType) {
        case RPM_CHAR_TYPE:
        case RPM_INT16_TYPE:
        case RPM_INT32_TYPE:
            expectedType = json_type_int;
            break;

        case RPM_STRING_ARRAY_TYPE:
            expectedType = json_type_string;
            break;

        default:
            fprintf(stderr, "*** Unexpected type %d for %s\n", tagType, key);
            return -1;
    }

    for (i = 0; i < json_object_array_length(val); i++) {
        iter = json_object_array_get_idx(val, i);

        actualType = json_object_get_type(iter);

        if (expectedType != actualType) {
            fprintf(stderr, "*** Invalid value %s for %s\n", json_object_get_string(iter), key);
            return -1;
        }

        if (expectedType == json_type_string) {
            if (_addheaderString(h, tag, key, iter) != 0) {
                return -1;
            }
        } else {
            if (_addHeaderInt(h, tag, tagType, key, iter) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int _addHeaderData(Header h, const char *key, json_object *val) {
    rpmTagVal tag;
    rpmTagType tagType;
    rpmTagReturnType returnType;
    json_type obj_type;
    const char *obj_value;
    const char *iter;

    /* Get the tag value for this string */
    if ((tag = rpmTagGetValue(key)) == -1) {
        fprintf(stderr, "Invalid RPM tag %s\n", key);
        return -1;
    }

    /* Check that the JSON object type and the RPM header type match, and insert the value */

    /* RPM has two types associated with a tag: the "type" (int8, string, etc)
     * and the "return type" (scalar, array).
     * 
     * The basic idea is that rpmTagReturnType describes the container and rpmTagType
     * describes the type of the values inside that container. For rpmTagReturnType,
     * the only values actually used are RPM_SCALAR_RETURN_TYPE, RPM_ARRAY_RETURN_TYPE.
     * RPM_ANY_RETURN_TYPE also shows up, always paired with RPM_NULL_TYPE, but these are
     * only used with the old RPMTAG_HEADER* tags used with converted legacy headers. There's
     * nothing we can or would want to do with these, so ignore them.
     *
     * For rpmTagType:
     *   * RPM_NULL_TYPE is not really used (see above)
     *   * RPM_CHAR_TYPE is only used as an array. Accept either a string or a list of ints in the JSON
     *   * RPM_INT8_TYPE is unused
     *   * RPM_INT16_TYPE is only used as an array
     *   * we're skipping RPM_INT64_TYPE because JSON-C sucks at handling ints; no uint64 return types.
     *     This could be fixed by parsing the string representation, but it's not that important. The
     *     type is only used for the RPMTAG_LONG* values.
     *   * RPM_STRING_TYPE is unused
     *   * RPM_BIN_TYPE is only used as a scalar, probably because there would be no
     *     way in the actual tag store to figure out the separation between array elements
     *     (no null-terminators). Skipping implementation of it for now.
     *   * RPM_STRING_ARRAY_TYPE is only used as an array, which kind of makes sense. The answer to the
     *     obvious "why" is that it predates the existence of rpmTagReturnType.
     *   * RPM_I18NSTRING_TYPE is specified as a scalar, but allows different values for different locales.
     *     Hardly anyone uses this feature, so at least for now just accept strings, and add them without locale.
     */

    obj_type = json_object_get_type(val);
    returnType = rpmTagGetReturnType(tag);
    tagType = rpmTagGetTagType(tag);

    switch (returnType) {
        case RPM_ARRAY_RETURN_TYPE:
            /* Special case for RPM_CHAR_TYPE */
            if ((tagType == RPM_CHAR_TYPE) && (obj_type == json_type_string)) {
                obj_value = json_object_get_string(val);
                iter = obj_value;

                while (*iter != '\0') {
                    if (headerPutChar(h, tag, &iter, 1) == 0) {
                        fprintf(stderr, "*** Error inserting value for %s\n", key);
                        return -1;
                    }

                    iter++;
                }

                return 0;
            }

            if (obj_type != json_type_array) {
                fprintf(stderr, "*** Error parsing tag %s: expected %s, got %s\n", key,
                        json_type_to_name(json_type_array), json_type_to_name(obj_type));
                return -1;
            }

            /* 
             * Use a helper to iterate over the array, which will also check the values of
             * the array elements.
             */
            if(_addHeaderArray(h, tag, tagType, key, val) != 0) {
                return -1;
            }

            break;

        case RPM_SCALAR_RETURN_TYPE:
            tagType = rpmTagGetTagType(tag);

            switch (rpmTagGetTagType(tag)) {
                case RPM_INT32_TYPE:
                    if (obj_type != json_type_int) {
                        fprintf(stderr, "*** Error parsing tag %s: expected %s, got %s\n", key,
                                json_type_to_name(json_type_int), json_type_to_name(obj_type));
                        return -1;
                    }

                    if (_addHeaderInt(h, tag, tagType, key, val) != 0) {
                        return -1;
                    }

                    break;


                case RPM_I18NSTRING_TYPE:
                    if (obj_type != json_type_string) {
                        fprintf(stderr, "*** Error parsing tag %s: expected %s, got %s\n", key,
                                json_type_to_name(json_type_string), json_type_to_name(obj_type));
                        return -1;
                    }

                    if (headerAddI18NString(h, tag, json_object_get_string(val), NULL) == 0) {
                        fprintf(stderr, "*** Error inserting value for tag %s\n", key);
                        return -1;
                    }

                    break;

                default:
                    fprintf(stderr, "Unexpected type for tag %s: %d\n", key, tagType);
                    return -1;
            }

            break;
        default:
            fprintf(stderr, "*** Unexpected tag return type for %s: %#08x\n", key, returnType);
            return -1;
    }

    return 0;
}

Header getRpmHeader(const char *path) {
    char *fullpath = NULL;
    char *data = MAP_FAILED;
    off_t data_len;

    int fd = -1;

    json_tokener *tok = NULL;
    json_object *obj = NULL;

    Header h = NULL;
    enum json_tokener_error json_err;

    /* Find the file, parse the data */
    xasprintf(&fullpath, "%s/data/%s", SRCDIR, path);

    if ((fd = open(fullpath, O_RDONLY)) == -1) {
        fprintf(stderr, "*** Unable to open RPM header description %s: %s\n", fullpath, strerror(errno));
        goto cleanup;
    }

    if ((data_len = lseek(fd, 0, SEEK_END)) == -1) {
        fprintf(stderr, "*** Unable to seek RPM header desciption file %s: %s\n", fullpath, strerror(errno));
        goto cleanup;
    }

    if ((data = mmap(NULL, data_len, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
        fprintf(stderr, "*** Unable to mmap RPM header description %s: %s\n", fullpath, strerror(errno));
        goto cleanup;
    }

    if ((tok = json_tokener_new) == NULL) {
        fprintf(stderr, "*** Unable to allocate json_tokener: %s\n", strerror(errno));
        goto cleanup;
    }

    obj = json_tokener_parse_ex(tok, data, data_len);
    json_err = json_tokener_get_error(tok);

    if ((obj == NULL) || (json_err != json_tokener_success)) {
        fprintf(stderr, "*** Unable to parse RPM header description %s: %s\n", fullpath, json_tokener_error_desc(json_err));
        goto cleanup;
    }

    /* Sanity check: make sure the parsed data is an object */
    if (json_object_get_type(obj) != json_type_object) {
        fprintf(stderr, "*** Error parsing RPM header description %s: not an object\n", fullpath);
        goto cleanup;
    }

    /* Set up the RPM header object */
    if ((h = headerNew()) == NULL) {
        fprintf(stderr, "*** Unable to allocate RPM header\n");
        goto cleanup;
    }

    /* Walk through the object's properties and add each one to the RPM Header's tag store */
    json_object_object_foreach(obj, key, val) {
        if (_addHeaderData(h, key, val) == -1) {
            headerFree(h);
            h = NULL;
            goto cleanup;
        }
    }

cleanup:
    free(fullpath);

    if (fd != -1) {
        close(fd);
    }

    if (data != MAP_FAILED) {
        munmap(data, data_len);
    }

    if (tok != NULL) {
        json_tokener_free(tok);
    }

    if (obj != NULL) {
        json_object_put(obj);
    }

    return h;
}
