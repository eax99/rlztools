/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* count-vbyte-tokens: given vbyte-format RLZ input (either on stdin or
 * files named in the arguments), counts how many tokens there are.
 * Essentially calculates how many different vbyte numbers there are
 * and then divides that by two.
 * usage: either no input, or filenames on stdin;
 *        if filenames are given, prints out the number of tokens in each file.
 *        understands --help when that's given to it.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>

void work(FILE* infile) {
	unsigned long numbers_seen = 0;
	int c = 0;
	/* not caring about decoded values, nor whether the tokens fit into 64
	 * bits when decoded -- will gladly count (2^600, 2^99) as one token */
	int expecting_more = 0;
	while ((c = fgetc(infile)) != EOF) {
		if (expecting_more) {
			/* already seen some bytes of the number */
			if (c >= 0x80) {
				/* and there will still be more to come */
				expecting_more += 1;
			} else {
				/* final byte of the number */
				numbers_seen++;
				expecting_more = 0;
			}
		} else {
			if (c >= 0x80) {
				/* first byte of a character */
				expecting_more = 1;
			} else {
				numbers_seen += 1;
			}
		}
	}
	if (expecting_more) {
		fputs("warning: number decoding interrupted by EOF (incomplete token at end)\n", stderr);
	}
	unsigned long half_numbers = numbers_seen >> 1; 
	if (numbers_seen % 2 != 0) {
		/* half token left over, so print out a final .5 */
		printf("%lu.5\n", half_numbers);
	} else {
		printf("%lu\n", half_numbers);
	}
}

int main(int argc, const char** argv) {
	if (argc < 2) {
		work(stdin);
	} else {
		/* first, check if any of the "files" is called "--help" */
		for (int i = 1; i < argc; i++) {
			if (strcmp("--help", argv[i]) == 0) {
				fputs("usage: ", stdout);
				fputs(argv[0], stdout);
				fputs(
" [FILENAME]...\n"
"With no FILENAMEs, will read from stdin.\n"
"Prints out half of the number of vbyte-encoded numbers in each file.\n"
"If a file has an odd number of vbyte-encoded numbers,\n"
"will print out a number with .5 at its end.\n"
"(rlztools.count-vbyte-tokens v0.9.1, December 2023)\n",
				stdout);
				return(0);
			}
		}
		/* now do the vbyte counting thing for files. */
		for (int i = 1; i < argc; i++) {
			errno = 0;
			FILE* infile = fopen(argv[i], "rb");
			if (infile == NULL) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
				continue;
			}
			work(infile);
			fclose(infile);
		}
	}
}
