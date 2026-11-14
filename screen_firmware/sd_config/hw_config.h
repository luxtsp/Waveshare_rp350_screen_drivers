#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#include <stddef.h>

#include "sd_card.h"

size_t sd_get_num(void);

sd_card_t *sd_get_by_num(size_t num);

#endif /* HW_CONFIG_H */
