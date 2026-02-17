#ifndef PLATFORM_REGISTRY_H
#define PLATFORM_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*platform_init_fn_t)(void);

int platform_register(const char *name, platform_init_fn_t init_fn);
int platform_set_active(const char *name);
int platform_init_selected(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_REGISTRY_H */
