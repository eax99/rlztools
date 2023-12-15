/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* 32-to-vbyte: recode 32-bit little-endian unsinged integers with a
 * variable-byte code that encodes numbers in little-endian with
 * all bytes carrying 7 bits of the input and all except the last byte
 * having their high bit set to 1.
 * usage: input is read on stdin, output on stdout, so use shell redirection
 *        to pull and push data from and to files.
 */

#include <stdio.h>
#include <stdint.h>

void write_out(uint32_t n) {
	if (n == 0)
		putchar('\0');
	while (n > 0) {
		if (n <= 127) {
			putchar((unsigned char) n);
		} else {
			unsigned char low_7 = n & 0x7F;
			putchar(low_7 | 0x80);
		}
		n = n >> 7;
	}
}

int main() {
	uint32_t input_number = 0;
	int in_byte = 0;
	int shiftwidth = 0;
	while ((in_byte = getchar()) != EOF) {
		input_number += in_byte << shiftwidth;
		shiftwidth += 8;
		if (shiftwidth == 32) {
			write_out(input_number);
			shiftwidth = 0;
			input_number = 0;
		}
	}
	if (shiftwidth != 0) {
		fputs("warning: input had a not-divisible-by-4 amount of bytes, padding with zero.\n", stderr);
		write_out(input_number);
		return 1;
	}
	return 0;
}

