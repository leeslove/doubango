/*
* Copyright (C) 2009 Mamadou Diop.
*
* Contact: Mamadou Diop <diopmamadou@yahoo.fr>
*	
* This file is part of Open Source Doubango Framework.
*
* DOUBANGO is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*	
* DOUBANGO is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*	
* You should have received a copy of the GNU General Public License
* along with DOUBANGO.
*
*/

/**@file tsk_md5.h
 * @brief Message-Digest algorithm 5 (RFC 1321).
 *
 * @author Mamadou Diop <diopmamadou(at)yahoo.fr>
 *
 * @date Created: Sat Nov 8 16:54:58 2009 mdiop
 */
#ifndef _TINYSAK_MD5_H_
#define _TINYSAK_MD5_H_

#include "tinySAK_config.h"

#define TSK_MD5_DIGEST_SIZE		16
#define TSK_MD5_BLOCK_SIZE		64

#define TSK_MD5_RESULT_SIZE		(TSK_MD5_DIGEST_SIZE*2)
typedef char tsk_md5result_t[TSK_MD5_RESULT_SIZE+1];

typedef struct tsk_md5context_s
{
	uint32_t buf[4];
	uint32_t bytes[2];
	uint32_t in[16];
}
tsk_md5context_t;

TINYSAK_API void tsk_md5init(tsk_md5context_t *context);
TINYSAK_API void tsk_md5update(tsk_md5context_t *context, uint8_t const *buf, size_t len);
TINYSAK_API void tsk_md5final(uint8_t digest[TSK_MD5_DIGEST_SIZE], tsk_md5context_t *context);
TINYSAK_API void tsk_md5transform(uint32_t buf[4], uint32_t const in[TSK_MD5_DIGEST_SIZE]);
TINYSAK_API void tsk_md5compute(const char* input, size_t size, tsk_md5result_t *result);


#endif /* _TINYSAK_MD5_H_ */