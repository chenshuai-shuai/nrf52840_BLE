#include <string.h>

#include "platform_registry.h"
#include "error.h"

#define PLATFORM_REGISTRY_MAX 8

struct platform_entry {
    const char *name;
    platform_init_fn_t init_fn;
};

static struct platform_entry g_registry[PLATFORM_REGISTRY_MAX];
static int g_registry_count;
static const struct platform_entry *g_active;

int platform_register(const char *name, platform_init_fn_t init_fn)
{
    if (name == NULL || init_fn == NULL) {
        return HAL_EINVAL;
    }

    for (int i = 0; i < g_registry_count; ++i) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return HAL_EBUSY;
        }
    }

    if (g_registry_count >= PLATFORM_REGISTRY_MAX) {
        return HAL_ENOMEM;
    }

    g_registry[g_registry_count].name = name;
    g_registry[g_registry_count].init_fn = init_fn;
    g_registry_count++;

    return HAL_OK;
}

int platform_set_active(const char *name)
{
    if (name == NULL) {
        return HAL_EINVAL;
    }

    for (int i = 0; i < g_registry_count; ++i) {
        if (strcmp(g_registry[i].name, name) == 0) {
            g_active = &g_registry[i];
            return HAL_OK;
        }
    }

    return HAL_ENODEV;
}

int platform_init_selected(void)
{
    if (g_active == NULL || g_active->init_fn == NULL) {
        return HAL_ENODEV;
    }

    return g_active->init_fn();
}
