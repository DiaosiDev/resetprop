/*
 *
 * resetprop.cpp
 * 
 * Copyright 2016 nkk71 <nkk71x@gmail.com>
 *
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include "_system_properties.h"
#include <sys/system_properties.h>

/* Info:
 * 
 * all changes are in
 *
 * bionic/libc/bionic/system_properties.cpp
 *
 * we can avoid changes in bionic/libc/include/_system_properties.h
 * by directly patching __system_properties_init() 
 *
 * 
 * Functions that need to be patched in system_properties.cpp
 *
 * int __system_properties_init()
 *     on android 7, first tear down the everything then let it initialize again:
 *         if (initialized) {
 *             //list_foreach(contexts, [](context_node* l) { l->reset_access(); });
 *             //return 0;
 *             free_and_unmap_contexts();
 *             initialized = false;
 *         }
 *
 *
 * static prop_area* map_prop_area(const char* filename, bool is_legacy)
 *     we dont want this read only so change: 'O_RDONLY' to 'O_RDWR'
 *
 * static prop_area* map_fd_ro(const int fd)
 *     we dont want this read only so change: 'PROT_READ' to 'PROT_READ | PROT_WRITE'
 *     
 *
 * by patching just those functions directly, all other functions should be ok
 * as is.
 *
 *
 */

int verbose = 0, del = 0, file = 0, trigger = 1;

static bool is_legal_property_name(const char* name, size_t namelen)
{
    size_t i;
    if (namelen >= PROP_NAME_MAX) return false;
    if (namelen < 1) return false;
    if (name[0] == '.') return false;
    if (name[namelen - 1] == '.') return false;

    /* Only allow alphanumeric, plus '.', '-', or '_' */
    /* Don't allow ".." to appear in a property name */
    for (i = 0; i < namelen; i++) {
        if (name[i] == '.') {
            // i=0 is guaranteed to never have a dot. See above.
            if (name[i-1] == '.') return false;
            continue;
        }
        if (name[i] == '_' || name[i] == '-') continue;
        if (name[i] >= 'a' && name[i] <= 'z') continue;
        if (name[i] >= 'A' && name[i] <= 'Z') continue;
        if (name[i] >= '0' && name[i] <= '9') continue;
        return false;
    }

    return true;
}

int x_property_set(const char *name, const char *value)
{
    prop_info *pi;
    int ret;
    char value_read[PROP_VALUE_MAX+1];

    size_t namelen = strlen(name);
    size_t valuelen = strlen(value);

    if (!is_legal_property_name(name, namelen)) return -1;
    if (valuelen >= PROP_VALUE_MAX) return -1;

    pi = (prop_info*) __system_property_find(name);
    __system_property_get(name, value_read);

    if(strlen(value_read)) {
        /* ro.* properties may NEVER be modified once set */
        //if(!strncmp(name, "ro.", 3)) return -1;

        // __system_property_get(name, value_read);
        printf("   Existing property: '%s'='%s'\n", name, value_read);

        if (trigger) {
            __system_property_del(name);
            ret = __system_property_set(name, value);
        } else {
            ret = __system_property_update(pi, value, valuelen);
        }

    } else {
        if (trigger) {
            ret = __system_property_set(name, value);
        } else {
            ret = __system_property_update(pi, value, valuelen);
        }
    }

    if (ret != 0) {
        printf("Failed to set '%s'='%s'\n", name, value);
        return ret;
    }

    __system_property_get(name, value_read);
    printf("   Changed property: '%s'='%s'\n", name, value_read);

    return 0;
}

int usage(char* name) {
    fprintf(stderr, "usage: %s [-v] [-n] [--file propfile] [--delete name] [ name value ] \n", name);
    fprintf(stderr, "   -v :\n");
    fprintf(stderr, "      verbose output (Default: Disabled)\n");
    fprintf(stderr, "   -n :\n");
    fprintf(stderr, "      no event triggers when changing props (Default: Will trigger events)\n");
    fprintf(stderr, "   --file propfile :\n");
    fprintf(stderr, "      Read props from prop files (e.g. build.prop)\n");
    fprintf(stderr, "   --delete name :\n");
    fprintf(stderr, "      Remove a prop entry\n\n");
    return 1;
}

int main(int argc, char *argv[])
{
    
    int exp_arg = 2, stdout_bak, null;
    char *name, *value, *filename;

    if (argc < 3) {
        return usage(argv[0]);
    }

    for (int i = 1; i < argc; ++i) {
        if (!strcmp("-v", argv[i])) {
            verbose = 1;
        } else if (!strcmp("-n", argv[i])) {
            trigger = 0;
        } else if (!strcmp("--file", argv[i])) {
            file = 1;
            exp_arg = 1;
        } else if (!strcmp("--delete", argv[i])) {
            del = 1;
            exp_arg = 1;
        } else {
            if (i + exp_arg > argc) {
                return usage(argv[0]);
            }
            if (file) {
                filename = argv[i];
                break;
            } else {
                if(!is_legal_property_name(argv[i], strlen(argv[i]))) {
                    fprintf(stderr, "Illegal property name \'%s\'\n", argv[i]);
                    return 1;
                }
                name = argv[i];
                if (exp_arg > 1) value = argv[i + 1];
                break;
            }
        }
    }

    if (!verbose) {
        fflush(stdout);
        stdout_bak = dup(1);
        null = open("/dev/null", O_WRONLY);
        dup2(null, 1);
    }

    printf("Hacky setprop by nkk71\n");
    printf("Modified for Magisk by topjohnwu\n");

    printf("   Initializing...\n");
    if (__system_properties_init()) {
        fprintf(stderr, "Error during init\n");
        return 1;
    }

    if (file) {
        printf("   Attempting to read props from '%s'\n", filename);
        // TODO!!
    } else if (del) {
        printf("   Attempting to delete '%s'\n", name);
        __system_property_del(name);
    } else {
        printf("   Attempting to set '%s'='%s'\n", name, value);
        if(x_property_set(name, value)) return 1;
    }
    printf("Done!\n\n");
    return 0;

}
