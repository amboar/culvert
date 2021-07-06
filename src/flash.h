/* Copyright 2013-2019 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _FLASH_H
#define _FLASH_H

#include "array.h"
#include "sfc.h"

#include <stdint.h>
#include <stdbool.h>

/* Flash status bits */
#define STAT_WIP	0x01
#define STAT_WEN	0x02

struct flash_chip {
    struct sfc *ctrl;
    struct flash_info info;
    uint32_t tsize;
    uint32_t min_erase_mask;
    bool mode_4b;
    struct flash_req *cur_req;
    void *smart_buf;
};

int flash_init(struct sfc *ctrl, struct flash_chip **chip);
void flash_destroy(struct flash_chip *chip);

int flash_read(struct flash_chip *c, uint64_t pos, void *buf, uint64_t len);
int flash_erase(struct flash_chip *c, uint64_t dst, uint64_t size);
int flash_write(struct flash_chip *c, uint32_t dst, const void *src,
		uint32_t size, bool verify);
int flash_smart_write(struct flash_chip *c, uint64_t dst, const void *src,
		      uint64_t size);

int flash_erase_chip(struct flash_chip *c);

#endif
