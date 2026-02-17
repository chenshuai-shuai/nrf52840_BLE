#ifndef COMMON_ERROR_H
#define COMMON_ERROR_H

#include <errno.h>

#define HAL_OK 0
#define HAL_EINVAL (-EINVAL)
#define HAL_ENODEV (-ENODEV)
#define HAL_ENOTSUP (-ENOTSUP)
#define HAL_EBUSY (-EBUSY)
#define HAL_EIO (-EIO)
#define HAL_ENOMEM (-ENOMEM)

#endif /* COMMON_ERROR_H */
