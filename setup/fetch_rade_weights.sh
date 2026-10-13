#!/bin/bash
# fetch_rade_weights.sh — fetches the weights of RADE's neural networks,
# which the mode RADE2 needs and the repository does not keep.
#
#     setup/fetch_rade_weights.sh
#
# modems/rade_c/README says what they are and why they are not in git.
# Each is fetched from one commit of its source, named here, and is kept
# only if its SHA-256 is the one written here; one that is already there
# and right is left alone. Moving to a newer RADE is a change to the two
# commits and the five sums. It needs curl and sha256sum.

cd "$(dirname "$0")/.." || exit 1

RADE_C=https://raw.githubusercontent.com/freedv/rade_c/c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9/src
OPUS_DNN=https://raw.githubusercontent.com/Rhizomatica/hermes-radio-daemon/8212db561ef36a37c884f2aff72e15fa1a84b3bd/vendor/opus_dnn/dnn

failed=0

fetch() {
	local from=$1 file=$2 sum=$3
	if [ -f "$file" ] && echo "$sum  $file" | sha256sum --check --status; then
		echo "have     $file"
		return
	fi
	echo "fetching $file"
	if ! curl --fail --silent --show-error --location -o "$file.part" "$from/$(basename "$file")"; then
		rm -f "$file.part"
		failed=1
		return
	fi
	if ! echo "$sum  $file.part" | sha256sum --check --status; then
		echo "         it is not the file expected, and is not kept"
		rm -f "$file.part"
		failed=1
		return
	fi
	mv "$file.part" "$file"
}

fetch $RADE_C modems/rade_c/rade_enc_data.c 1531c9cb3a9c28079d8ab01fc95802bdf58b8fe4edc927d114c5fb97fc178a27
fetch $RADE_C modems/rade_c/rade_dec_data.c b02b29406af9168a25539a2afee26a1f2eef76d84bd3bfb28bbb3771d94992ac
fetch $RADE_C modems/rade_c/rade_enc_v2_data.c 2b54ad912166f56b754044ef33021eb4da4679a1c752b9b2de9f8b023fd2c824
fetch $RADE_C modems/rade_c/rade_dec_v2_data.c f06f591c4fb5b3aa142acd54d79731bfc87abcc7bd73ad603098c4f9221149c5
fetch $OPUS_DNN modems/opus_dnn/dnn/fargan_data.c 68ba6e8731007f7184be862d7d1afe4cef096a7c9bcbfa115962340f725e004c

if [ $failed != 0 ]; then
	echo "not all the weights could be fetched"
	exit 1
fi
echo "the weights are all here"
