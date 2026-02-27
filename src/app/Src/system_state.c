#include "system_state.h"
#include "error.h"

K_MUTEX_DEFINE(g_state_lock);
static pm_state_t g_pm_state;

void system_state_init(void)
{
    k_mutex_lock(&g_state_lock, K_FOREVER);
    g_pm_state.valid = 0;
    k_mutex_unlock(&g_state_lock);
}

void system_state_set_pm(const pm_state_t *state)
{
    if (state == NULL) {
        return;
    }
    k_mutex_lock(&g_state_lock, K_FOREVER);
    g_pm_state = *state;
    k_mutex_unlock(&g_state_lock);
}

int system_state_get_pm(pm_state_t *out)
{
    if (out == NULL) {
        return HAL_EINVAL;
    }
    k_mutex_lock(&g_state_lock, K_FOREVER);
    *out = g_pm_state;
    k_mutex_unlock(&g_state_lock);
    return HAL_OK;
}
