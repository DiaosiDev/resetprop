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

#define ANDROID_7 1

// copy the includes from bionic/libc/bionic/system_properties.cpp

#ifdef ANDROID_7
// 7.0
#include <new>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>

#include <sys/mman.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <sys/system_properties.h>

#include "../../../private/bionic_futex.h"
#include "../../../private/bionic_macros.h"

#else
// 5.1 ~ 6.0
#include <new>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>

#include <sys/mman.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <sys/system_properties.h>

#include "../../../private/bionic_atomic_inline.h"
#include "../../../private/bionic_futex.h"
#include "../../../private/bionic_macros.h"

// #include "init.h"
#endif

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

    if(pi != 0) {
        /* ro.* properties may NEVER be modified once set */
        //if(!strncmp(name, "ro.", 3)) return -1;

        __system_property_get(name, value_read);
        printf("   existing property '%s' value='%s'\n", name, value_read);

        __system_property_update(pi, value, valuelen);

        __system_property_get(name, value_read);
        printf("   existing property '%s' now set to value='%s'\n", name, value_read);
    } else {
        ret = __system_property_add(name, namelen, value, valuelen);
        if (ret < 0) {
            printf("Failed to set '%s'='%s'\n", name, value);
            return ret;
        }
        __system_property_get(name, value_read);
        printf("   non-existing property '%s' now set to value='%s'\n", name, value_read);
    }

    //property_changed(name, value);
    return 0;
}

int main(int argc, char *argv[])
{
    printf("Hacky setprop v0.3.0 by nkk71\n");
    if(argc != 3) {
        //fprintf(stderr,"usage: setprop <key> <value> <trigger property_changed>\n");
        fprintf(stderr,"usage: %s <key> <value>\n", argv[0]);
        return 1;
    }

    printf("   initializing...\n");
    if (__system_properties_init()) {
        printf("Error during init.\n");
        return 1;
    }

    if (strcmp(argv[1], "--delete") == 0) {
        printf("   attempting to delete '%s'...\n", argv[2]);
        __system_property_del(argv[2]);
    }
    else {
        printf("   attempting to set '%s' to '%s'...\n", argv[1], argv[2]);
        if(x_property_set(argv[1], argv[2])){
            fprintf(stderr,"Could not set property.\n");
            return 1;
        }
    }
    printf("Finished.\n\n");

    return 0;
}
