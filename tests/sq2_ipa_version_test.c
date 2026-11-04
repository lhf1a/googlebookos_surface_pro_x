#include <assert.h>
#include <stdio.h>
#include "../probe/test2/sq2-ipa-version.h"
int main(void)
{
	assert(!sq2_ipa_version_valid(0));
	assert(!sq2_ipa_version_valid(~0U));
	assert(!sq2_ipa_version_valid(0x20010000));
	assert(sq2_ipa_version_valid(0x40010007));
	assert(sq2_ipa_major(0x40010007) == 4);
	assert(sq2_ipa_minor(0x40010007) == 1);
	assert(sq2_ipa_revision(0x40010007) == 7);
	assert(sq2_ipa_minor(0x4abc1234) == 0xabc);
	puts("PASS: version bit slices, zero/all-ones and invalid major refused");
	return 0;
}
