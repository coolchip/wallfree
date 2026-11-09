/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Lesen von Flash, OTP und Optionsbytes (SOLL-SH-24) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/flashread.h>

ZTEST_SUITE(api_flashread, NULL, NULL, NULL, NULL, NULL);

ZTEST(api_flashread, test_ranges)
{
	/* Sektor 0..6 */
	zassert_equal(wf_flashread_check(0x08000000u, 1024), 0);
	zassert_equal(wf_flashread_check(0x0805FC00u, 1024), 0, "bis zum Ende von Sektor 6");
	zassert_equal(wf_flashread_check(0x0805FFFFu, 1), 0);
	/* Sektor 7 mit den Zugangsdaten nicht, auch nicht teilweise */
	zassert_equal(wf_flashread_check(0x0805FFFFu, 2), -2);
	zassert_equal(wf_flashread_check(0x08060000u, 1), -2);
	zassert_equal(wf_flashread_check(0x0807FFFFu, 1), -2);
	/* OTP und Optionsbytes */
	zassert_equal(wf_flashread_check(0x1FFF7800u, 528), 0);
	zassert_equal(wf_flashread_check(0x1FFF7800u, 529), -2);
	zassert_equal(wf_flashread_check(0x1FFFC000u, 16), 0);
	zassert_equal(wf_flashread_check(0x1FFFC000u, 17), -2);
	/* anderes: RAM, Peripherie, Systemspeicher, vor dem Flash */
	zassert_equal(wf_flashread_check(0x20000000u, 4), -2);
	zassert_equal(wf_flashread_check(0x40023C00u, 4), -2);
	zassert_equal(wf_flashread_check(0x1FFF0000u, 4), -2);
	zassert_equal(wf_flashread_check(0x07FFFFFFu, 2), -2);
	/* Laenge */
	zassert_equal(wf_flashread_check(0x08000000u, 0), -1);
	zassert_equal(wf_flashread_check(0x08000000u, 1025), -1);
	/* Ueberlauf am Adressende */
	zassert_equal(wf_flashread_check(0xFFFFFFFFu, 2), -2);
}

ZTEST(api_flashread, test_base64)
{
	char out[16];

	/* RFC 4648 Abschnitt 10 */
	zassert_equal(wf_base64((const uint8_t *)"", 0, out, sizeof(out)), 0);
	zassert_str_equal(out, "");
	zassert_equal(wf_base64((const uint8_t *)"f", 1, out, sizeof(out)), 4);
	zassert_str_equal(out, "Zg==");
	wf_base64((const uint8_t *)"fo", 2, out, sizeof(out));
	zassert_str_equal(out, "Zm8=");
	wf_base64((const uint8_t *)"foo", 3, out, sizeof(out));
	zassert_str_equal(out, "Zm9v");
	wf_base64((const uint8_t *)"foobar", 6, out, sizeof(out));
	zassert_str_equal(out, "Zm9vYmFy");

	const uint8_t bin[] = {0x00, 0xFF, 0xFE};

	wf_base64(bin, sizeof(bin), out, sizeof(out));
	zassert_str_equal(out, "AP/+");
	/* zu kleiner Puffer */
	zassert_equal(wf_base64((const uint8_t *)"foobar", 6, out, 8), 0);
}
