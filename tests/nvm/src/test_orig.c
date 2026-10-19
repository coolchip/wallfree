/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Leser fuer den Originalbereich (Sektor 3), SOLL-NVM-10 */
#include <zephyr/ztest.h>
#include <wallfree/nvm/orig.h>
#include "flash_sim.h"
#include "orig_image.h"

static struct fsim fs;

static void before(void *f)
{
	ARG_UNUSED(f);
	fsim_init(&fs, 16384);
}

ZTEST_SUITE(nvm_orig, NULL, NULL, before, NULL, NULL);

ZTEST(nvm_orig, test_youngest_wins)
{
	uint16_t v;

	orig_image_init(fs.mem);
	orig_image_add(fs.mem, 525, 500);
	orig_image_add(fs.mem, 390, 1);
	orig_image_add(fs.mem, 525, 1200);
	zassert_equal(nvm_orig_read(&fs.fl, 525, &v), NVM_OK);
	zassert_equal(v, 1200);
	zassert_equal(nvm_orig_read(&fs.fl, 390, &v), NVM_OK);
	zassert_equal(v, 1);
	zassert_equal(nvm_orig_read(&fs.fl, 526, &v), NVM_E_NOENT);
	zassert_equal(fs.bytes, 0, "nur gelesen");
	zassert_equal(fs.erases, 0);
}

ZTEST(nvm_orig, test_invalid_header)
{
	uint16_t v;

	/* geloeschter Bereich (Kopf 0xFFFF) und Zwischenzustand 0xEEEE */
	zassert_equal(nvm_orig_read(&fs.fl, 525, &v), NVM_E_STATE);
	fs.mem[0] = 0xEE;
	fs.mem[1] = 0xEE;
	zassert_equal(nvm_orig_read(&fs.fl, 525, &v), NVM_E_STATE);
}

ZTEST(nvm_orig, test_partial_entry_ignored)
{
	uint16_t v;

	orig_image_init(fs.mem);
	orig_image_add(fs.mem, 527, 3000);
	/* naechster Eintrag nur halb geschrieben: Wert ohne Kennung */
	uint32_t off = orig_image_add(fs.mem, 527, 4000);

	fs.mem[off + 2] = 0xFF;
	fs.mem[off + 3] = 0xFF;
	zassert_equal(nvm_orig_read(&fs.fl, 527, &v), NVM_OK);
	zassert_equal(v, 3000);
}

ZTEST(nvm_orig, test_last_word_of_sector)
{
	uint16_t v;

	orig_image_init(fs.mem);
	/* Eintrag im letzten Wort des Sektors */
	fs.mem[16380] = 0x34;
	fs.mem[16381] = 0x12;
	fs.mem[16382] = 0x09; /* 521 = 0x0209 */
	fs.mem[16383] = 0x02;
	zassert_equal(nvm_orig_read(&fs.fl, 521, &v), NVM_OK);
	zassert_equal(v, 0x1234);
}
