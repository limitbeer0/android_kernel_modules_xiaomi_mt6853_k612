// SPDX-License-Identifier: GPL-3.0-only
/* Minimal C adaptation of tiann/KernelSU manager/app/src/main/cpp/adbroot.cc.
 * Upstream reference: https://github.com/tiann/KernelSU/pull/3382
 * Modified for cannon, 2026-10-24. See adbroot-GPL-3.txt.
 * Only this injected adbd sees the overrides. USB authentication is unchanged.
 */
typedef unsigned int uint32_t;
struct prop_info;
extern void *dlsym(void *handle, const char *symbol);
extern int unsetenv(const char *name);
#define NEXT ((void *)-1L)
#define PUBLIC __attribute__((visibility("default"), used))

static char root_property;

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

PUBLIC int __android_log_is_debuggable(void)
{
    return 1;
}

PUBLIC const struct prop_info *__system_property_find(const char *name)
{
    static const struct prop_info *(*original)(const char *);
    if (equal(name, "service.adb.root"))
        return (const struct prop_info *)&root_property;
    if (!original)
        original = dlsym(NEXT, "__system_property_find");
    return original ? original(name) : (void *)0;
}

PUBLIC void __system_property_read_callback(const struct prop_info *property,
        void (*callback)(void *, const char *, const char *, uint32_t), void *cookie)
{
    static void (*original)(const struct prop_info *,
            void (*)(void *, const char *, const char *, uint32_t), void *);
    if (property == (const struct prop_info *)&root_property) {
        if (callback) callback(cookie, "service.adb.root", "1", 0);
        return;
    }
    if (!original)
        original = dlsym(NEXT, "__system_property_read_callback");
    if (original) original(property, callback, cookie);
}

/* Retain the kernel-selected root context, as the upstream adbroot shim does. */
PUBLIC int selinux_android_setcon(const char *context)
{
    (void)context;
    return 0;
}

__attribute__((constructor)) static void initialize(void)
{
    unsetenv("LD_PRELOAD");
}
