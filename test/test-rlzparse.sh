#!/bin/sh

# Params: width, SA width, input, dictionary, SA, format, expected output.
# Wrapper around compress_compare to pretty-print the inputs and result.
test_compression () {
	echo -ne "Testing rlzparse \033[1;33mw$1 \033[35m$6\033[0m"\
		"\033[34m$3\033[0m \033[36m$4\033[0m: ";
	if compress_compare $@ ; then
		echo -e "\033[1;32mPASS\033[0m"
	else
		echo -e "\033[1;31mFAIL\033[0m"
	fi
}

# Same parameters in the same order as test_compression:
# -w $1, -W $2, -i $3, -d $4, -s $5, -f $6, expected output = $7
compress_compare () {
	local tmpf
	tmpf=testfile-rlzparse-$1-$(date +%M%S)
	../build/rlzparse -q -w $1 -W $2 -i $3 -d $4 -s $5 -f $6 -o $tmpf \
		&& cmp -s $tmpf $7
	identical=$?
	rm -f $tmpf
	return $identical
}

test_compression 8 32 input/8-in-ababab dict/8-dict-ababab sa/8-dict-ababab 32x2 rlz/8-in-ababab-dict-ababab.rlz32
test_compression 8 32 input/8-in-ababab dict/8-dict-ababab sa/8-dict-ababab 64x2 rlz/8-in-ababab-dict-ababab.rlz64
test_compression 8 32 input/8-in-ababab dict/8-dict-ababab sa/8-dict-ababab vbyte rlz/8-in-ababab-dict-ababab.rlzv

test_compression 8 32 input/8-in-abacab dict/8-dict-ababab sa/8-dict-ababab 32x2 rlz/8-in-abacab-dict-ababab.rlz32
test_compression 8 32 input/8-in-abacab dict/8-dict-ababab sa/8-dict-ababab 64x2 rlz/8-in-abacab-dict-ababab.rlz64
test_compression 8 32 input/8-in-abacab dict/8-dict-ababab sa/8-dict-ababab vbyte rlz/8-in-abacab-dict-ababab.rlzv

test_compression 8 32 input/8-in-aaaab dict/8-dict-aaaa sa/8-dict-aaaa 32x2 rlz/8-in-aaaab-dict-aaaa.rlz32
test_compression 8 32 input/8-in-aaaab dict/8-dict-aaaa sa/8-dict-aaaa 64x2 rlz/8-in-aaaab-dict-aaaa.rlz64
test_compression 8 32 input/8-in-aaaab dict/8-dict-aaaa sa/8-dict-aaaa vbyte rlz/8-in-aaaab-dict-aaaa.rlzv
test_compression 8 32 input/8-in-aaaab dict/8-dict-ababab sa/8-dict-ababab vbyte rlz/8-in-aaaab-dict-ababab.rlzv

test_compression 8 32 input/8-in-noise input/8-in-noise sa/8-in-noise 32x2 rlz/8-in-noise-dict-self.rlz32
test_compression 8 32 input/8-in-noise input/8-in-noise sa/8-in-noise 64x2 rlz/8-in-noise-dict-self.rlz64
test_compression 8 32 input/8-in-noise input/8-in-noise sa/8-in-noise vbyte rlz/8-in-noise-dict-self.rlzv

test_compression 8 32 input/8-in-permu dict/8-dict-permu sa/8-dict-permu 32x2 rlz/8-in-permu-dict-permu.rlz32
test_compression 8 32 input/8-in-permu dict/8-dict-permu sa/8-dict-permu 64x2 rlz/8-in-permu-dict-permu.rlz64
test_compression 8 32 input/8-in-permu dict/8-dict-permu sa/8-dict-permu vbyte rlz/8-in-permu-dict-permu.rlzv

