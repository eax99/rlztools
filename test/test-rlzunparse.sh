#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright 2024 Eve Kivivuori
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Params: width, format, compressed input, dictionary, expected output.
# Wrapper around decompress_compare to pretty-print the inputs and result.
test_decompression () {
	echo -ne "Testing rlzunparse \033[1;33mw$1 \033[35m$2\033[0m"\
		"\033[34m$3\033[0m \033[36m$4\033[0m: ";
	if decompress_compare $@ ; then
		echo -e "\033[1;32mPASS\033[0m"
	else
		echo -e "\033[1;31mFAIL\033[0m"
	fi
}

# Same parameters in the same order as test_decompression:
# -w $1, -f $2, -i $3, -d $4, expected output = $5
decompress_compare () {
	local tmpf
	tmpf=testfile-rlzunparse-$1$2-$(date +%M%S)
	../build/rlzunparse -q -w $1 -f $2 -i $3 -d $4 -o $tmpf \
		&& cmp -s $tmpf $5
	identical=$?
	rm -f $tmpf
	return $identical
}

test_decompression 8 32x2 rlz/8-in-aaaab-dict-aaaa.rlz32 dict/8-dict-aaaa input/8-in-aaaab
test_decompression 8 64x2 rlz/8-in-aaaab-dict-aaaa.rlz64 dict/8-dict-aaaa input/8-in-aaaab
test_decompression 8 vbyte rlz/8-in-aaaab-dict-aaaa.rlzv dict/8-dict-aaaa input/8-in-aaaab
test_decompression 8 vbyte rlz/8-in-aaaab-dict-ababab.rlzv dict/8-dict-ababab input/8-in-aaaab

test_decompression 8 32x2 rlz/8-in-ababab-dict-ababab.rlz32 dict/8-dict-ababab input/8-in-ababab
test_decompression 8 32x2 rlz/8-in-abacab-dict-ababab.rlz32 dict/8-dict-ababab input/8-in-abacab
test_decompression 8 64x2 rlz/8-in-ababab-dict-ababab.rlz64 dict/8-dict-ababab input/8-in-ababab
test_decompression 8 64x2 rlz/8-in-ababab-dict-ababab.rlz64 dict/8-dict-ababab input/8-in-ababab
test_decompression 8 vbyte rlz/8-in-abacab-dict-ababab.rlzv dict/8-dict-ababab input/8-in-abacab
test_decompression 8 vbyte rlz/8-in-abacab-dict-ababab.rlzv dict/8-dict-ababab input/8-in-abacab

test_decompression 8 32x2 rlz/8-in-noise-dict-self.rlz32 input/8-in-noise input/8-in-noise
test_decompression 8 64x2 rlz/8-in-noise-dict-self.rlz64 input/8-in-noise input/8-in-noise
test_decompression 8 vbyte rlz/8-in-noise-dict-self.rlzv input/8-in-noise input/8-in-noise

test_decompression 8 32x2 rlz/8-in-permu-dict-permu.rlz32 dict/8-dict-permu input/8-in-permu
test_decompression 8 64x2 rlz/8-in-permu-dict-permu.rlz64 dict/8-dict-permu input/8-in-permu
test_decompression 8 vbyte rlz/8-in-permu-dict-permu.rlzv dict/8-dict-permu input/8-in-permu

