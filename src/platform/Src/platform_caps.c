#include <stddef.h>

#include "platform_caps.h"
#include "error.h"

static platform_caps_t g_caps;
static bool g_caps_valid;

int platform_caps_set(const platform_caps_t *caps)
{
    if (caps == NULL) {
        return HAL_EINVAL;
    }

    g_caps = *caps;
    g_caps_valid = true;
    return HAL_OK;
}

const platform_caps_t *platform_caps_get(void)
{
    if (!g_caps_valid) {
        return NULL;
    }

    return &g_caps;
}
