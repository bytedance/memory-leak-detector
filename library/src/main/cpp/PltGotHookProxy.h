#ifndef _PLT_GOT_HOOK_PROXY_H
#define _PLT_GOT_HOOK_PROXY_H

#include "Logger.h"
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <pthread.h>
#include <dirent.h>
#include <unwind.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <regex.h>
#include <link.h>
#include <xh_elf.h>
#include <cctype>
#include <errno.h>
#include "backtrace.h"
#include "HookProxy.h"

#ifndef __LP64__
#define LINKER_N "/system/bin/linker"
#define DLOPEN_EXT_SYMBOL_N "__dl__Z9do_dlopenPKciPK17android_dlextinfoPv"
#define DLOPEN_MUTEX_SYMBOL_N "__dl__ZL10g_dl_mutex"
#define __PRI_PTR_prefix "%x-%*lx %4s %lx %*x:%*x %*d%n"
#else
#define LINKER_N "/system/bin/linker64"
#define DLOPEN_EXT_SYMBOL_N "__dl__Z9do_dlopenPKciPK17android_dlextinfoPv"
#define DLOPEN_MUTEX_SYMBOL_N "__dl__ZL10g_dl_mutex"
#define __PRI_PTR_prefix  "%lx-%*lx %4s %lx %*x:%*x %*d%n"
#endif

#define SO_LOAD_SYMBOL_O_1 "__loader_android_dlopen_ext"
#define SO_LOAD_SYMBOL_O_2 "__loader_dlopen"

#define SO_LOAD_SYMBOL_N_1 "android_dlopen_ext"
#define SO_LOAD_SYMBOL_N_2 "dlopen"

#define SO_LOAD_SYMBOL "dlopen"

#define SO_SELF "libraphael.so"
#define SO_LIBDL "libdl.so"

static void *(*dlopen_origin_O)(const char *, int, const void *) = nullptr;

static void *(*dlopen_origin_N)(const char *, int) = nullptr;

static void *(*dlopen_origin)(const char *, int) = nullptr;

static void *
(*android_dlopen_origin_O)(const char *, int, const void *, const void *) = nullptr;

static void *
(*android_dlopen_origin_N)(const char *, int, const void *) = nullptr;

static void *(*dlopen_ext_N)(const char *, int, const void *, const void *) = nullptr;

static pthread_mutex_t *g_dl_mutex = nullptr;

static void try_pltgot_hook_on_soload(const char *filename);

static int api_level;

static regex_t focused_regex;
static bool use_regex = false;

static bool is_hook_libdl_success = false;

struct so_load_data {
    const char *name;
};

static bool is_so_name(const char *name) {
    if (name == NULL) {
        return false;
    }
    size_t len = strlen(name);
    if (len > 3) {
        return *(name + len - 1) == 'o' &&
               *(name + len - 2) == 's' &&
               *(name + len - 3) == '.';
    }
    return false;
}

static bool is_self_so(const char *name) {
    if (name == NULL) {
        return false;
    }
    return strstr(name, SO_SELF) != NULL;
}

static const char *pretty_name(const char *filename) {
    if (strchr(filename, '/') == NULL) {
        return filename;
    }
    size_t len = strlen(filename);
    size_t index = len;
    while (index > 0) {
        if (*(filename + index - 1) == '/') {
            break;
        }
        index--;
    }
    return filename + index - 1;
}

static ElfW(Addr) get_so_base(struct dl_phdr_info *info) {
    const ElfW(Phdr) *phdr = info->dlpi_phdr;
    ElfW(Addr) min_vaddr = INTPTR_MAX;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (phdr->p_type != PT_LOAD) {
            phdr++;
            continue;
        }
        if (min_vaddr <= phdr->p_vaddr) {
            phdr++;
            continue;
        }
        min_vaddr = phdr->p_vaddr;
        phdr++;
    }
    return min_vaddr == INTPTR_MAX ? 0 : info->dlpi_addr + min_vaddr;
}

static void *dlopen_proxy_O(const char *filename, int flags, const void *caller_addr) {
    void *result = dlopen_origin_O(filename, flags, caller_addr);
    if (result != NULL) {
        try_pltgot_hook_on_soload(filename);
    }
    return result;
}

static void *dlopen_proxy_N(const char *filename, int flags) {
    void *result = dlopen_origin_N(filename, flags);
    if (result == NULL) {
        void *caller = __builtin_return_address(0);
        pthread_mutex_lock(g_dl_mutex);
        result = dlopen_ext_N(filename, flags, nullptr, caller);
        pthread_mutex_unlock(g_dl_mutex);
    }
    if (result != NULL) {
        try_pltgot_hook_on_soload(filename);
    }
    return result;
}

static void *dlopen_proxy(const char *filename, int flags) {
    void *result = dlopen_origin(filename, flags);
    if (result != NULL) {
        try_pltgot_hook_on_soload(filename);
    }
    return result;
}

static void *
android_dlopen_proxy_O(const char *filename,
                       int flags,
                       const void *extinfo,
                       const void *caller_addr) {
    void *result = android_dlopen_origin_O(filename, flags, extinfo, caller_addr);
    if (result != NULL) {
        try_pltgot_hook_on_soload(filename);
    }
    return result;
}

static void *
android_dlopen_proxy_N(const char *filename,
                       int flags,
                       const void *extinfo) {
    void *result = android_dlopen_origin_N(filename, flags, extinfo);
    if (result == NULL) {
        void *caller = __builtin_return_address(0);
        pthread_mutex_lock(g_dl_mutex);
        result = dlopen_ext_N(filename, flags, extinfo, caller);
        pthread_mutex_unlock(g_dl_mutex);
    }
    if (result != NULL) {
        try_pltgot_hook_on_soload(filename);
    }
    return result;
}


static const void *sSoLoad_O[][3] = {
        {
                SO_LOAD_SYMBOL_O_1,
                (void *) android_dlopen_proxy_O,
                (void **) &android_dlopen_origin_O
        },
        {
                SO_LOAD_SYMBOL_O_2,
                (void *) dlopen_proxy_O,
                (void **) &dlopen_origin_O
        },
};

static const void *sSoLoad_N[][3] = {
        {
                SO_LOAD_SYMBOL_N_1,
                (void *) android_dlopen_proxy_N,
                (void **) &android_dlopen_origin_N
        },
        {
                SO_LOAD_SYMBOL_N_2,
                (void *) dlopen_proxy_N,
                (void **) &dlopen_origin_N
        },
};

static const void *sSoLoad[][3] = {
        {
                SO_LOAD_SYMBOL,
                (void *) dlopen_proxy,
                (void **) &dlopen_origin
        },
};

static void tryHookAllFunc(xh_elf_t elf) {
    for (int i = 0; i < sizeof(sPltGot) / sizeof(sPltGot[0]); i++) {
        xh_elf_hook(&elf, (const char *) sPltGot[i][0], (void *) sPltGot[i][1], NULL);
    }
}

static void tryHookSoLoadFunc(xh_elf_t elf, bool save_old_func) {
    if (api_level >= __ANDROID_API_O__) {
        if (!is_hook_libdl_success && strstr(elf.pathname, SO_LIBDL) != NULL) {
            for (int i = 0; i < sizeof(sSoLoad_O) / sizeof(sSoLoad_O[0]); i++) {
                xh_elf_hook(&elf, (const char *) sSoLoad_O[i][0], (void *) sSoLoad_O[i][1],
                            save_old_func ? (void **) sSoLoad_O[i][2] : NULL);
            }
            is_hook_libdl_success = true;
        }
    } else if (api_level >= __ANDROID_API_N__) {
        if (g_dl_mutex != NULL &&
            dlopen_ext_N != NULL) {
            for (int i = 0; i < sizeof(sSoLoad_N) / sizeof(sSoLoad_N[0]); i++) {
                xh_elf_hook(&elf, (const char *) sSoLoad_N[i][0], (void *) sSoLoad_N[i][1],
                            save_old_func ? (void **) sSoLoad_N[i][2] : NULL);
            }
        }
    } else {
        for (int i = 0; i < sizeof(sSoLoad) / sizeof(sSoLoad[0]); i++) {
            xh_elf_hook(&elf, (const char *) sSoLoad[i][0], (void *) sSoLoad[i][1],
                        save_old_func ? (void **) sSoLoad[i][2] : NULL);
        }
    }
}

int default_callback(const char *name, uintptr_t base) {
    xh_elf_t elf;
    if (0 != xh_elf_init(&elf, base, name)) {
        return 0;
    }
    if (!use_regex || (regexec(&focused_regex, name, 0, NULL, 0) == 0)) {
        tryHookAllFunc(elf);
    }
    tryHookSoLoadFunc(elf, true);
    return 0;
}

int so_load_callback(const char *name, uintptr_t base, so_load_data *data) {
    const char *filename = data->name;
    if (strstr(name, filename) != NULL || strstr(filename, name) != NULL) {
        xh_elf_t elf;
        if (0 != xh_elf_init(&elf, base, name)) {
            return 1;
        }
        tryHookAllFunc(elf);
        tryHookSoLoadFunc(elf, false);
        LOGGER(">>>>>>>> Hook so %s success by soload", name);
        return 1;
    }
    return 0;
}

int common_callback(const char *name, uintptr_t base, void *data) {
    int ret = 0;

    if (name == NULL || base == 0) {
        return ret;
    }
    if (!is_so_name(name)) {
        return ret;
    }
    if (is_self_so(name)) {
        return ret;
    }
    if (data == NULL) {
        ret = default_callback(name, base);
    } else {
        ret = so_load_callback(name, base, (so_load_data *) data);
    }
    return ret;
}

int dl_iterate_callback(dl_phdr_info *info, size_t size, void *data) {
    if (info == NULL) {
        return 0;
    }
    return common_callback(info->dlpi_name, get_so_base(info), data);
}

static void try_pltgot_hook_on_soload(const char *filename) {
    if (!is_so_name(filename)) {
        return;
    }
    if (!use_regex || (regexec(&focused_regex, filename, 0, NULL, 0) == 0)) {
        const char *pretty = pretty_name(filename);
        so_load_data *data = new so_load_data();
        data->name = pretty;
        xdl_iterate_phdr(dl_iterate_callback, (void *) data, XDL_FULL_PATHNAME);
        delete data;
    }
}

int registerSoLoadProxy(JNIEnv *env, jstring focused) {
    api_level = android_get_device_api_level();

    if (focused != NULL) {
        const char *focused_reg = (char *) env->GetStringUTFChars(focused, 0);
        use_regex = regcomp(&focused_regex, focused_reg, REG_EXTENDED|REG_NOSUB) == 0;
        env->ReleaseStringUTFChars(focused, focused_reg);
    }

    if (api_level >= __ANDROID_API_N__ && api_level < __ANDROID_API_O__) {
        void *dl = xdl_open(LINKER_N);
        if (dl != NULL) {
            dlopen_ext_N = (void *(*)(const char *, int, const void *, const void *)) (xdl_sym(dl,
                                                                                               DLOPEN_EXT_SYMBOL_N));
            g_dl_mutex = (pthread_mutex_t *) (xdl_sym(dl, DLOPEN_MUTEX_SYMBOL_N));
            xdl_close(dl);
        }
    }

    xdl_iterate_phdr(dl_iterate_callback, NULL, XDL_FULL_PATHNAME);
    return 0;
}

#endif