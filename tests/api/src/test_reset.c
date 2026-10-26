/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Resetgrund (SOLL-RT-14) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/reset.h>

ZTEST_SUITE(api_reset, NULL, NULL, NULL, NULL, NULL);

ZTEST(api_reset, test_classify)
{
	const uint32_t pin = WF_RSTF_PIN;

	zassert_equal(wf_reset_classify(WF_RSTF_POR | WF_RSTF_BOR | pin, 0), WF_RST_POWER);
	zassert_equal(wf_reset_classify(WF_RSTF_BOR | pin, 0), WF_RST_BROWNOUT);
	zassert_equal(wf_reset_classify(pin, 0), WF_RST_PIN);
	zassert_equal(wf_reset_classify(WF_RSTF_IWDG | pin, 0), WF_RST_IWDG);
	zassert_equal(wf_reset_classify(WF_RSTF_IWDG | WF_RSTF_SFT | pin,
					wf_reset_bkp_value(WF_RST_USER)),
		      WF_RST_IWDG, "Watchdog hat Vorrang");
	zassert_equal(wf_reset_classify(WF_RSTF_WWDG, 0), WF_RST_WWDG);
	zassert_equal(wf_reset_classify(WF_RSTF_LPWR, 0), WF_RST_LOWPOWER);
	zassert_equal(wf_reset_classify(0, 0), WF_RST_UNKNOWN);

	/* Software-Reset mit und ohne Code */
	for (enum wf_reset r = WF_RST_LIMIT; r < WF_RST_COUNT; r++) {
		zassert_equal(wf_reset_classify(WF_RSTF_SFT | pin, wf_reset_bkp_value(r)), r);
	}
	zassert_equal(wf_reset_classify(WF_RSTF_SFT, 0), WF_RST_SOFT);
	zassert_equal(wf_reset_classify(WF_RSTF_SFT, 0x12340009), WF_RST_SOFT, "fremder Inhalt");
	zassert_equal(wf_reset_classify(WF_RSTF_SFT, wf_reset_bkp_value(WF_RST_POWER)), WF_RST_SOFT,
		      "nur Software-Codes");
	/* Code ohne Software-Reset (z. B. Stromausfall danach): ignoriert */
	zassert_equal(wf_reset_classify(WF_RSTF_POR, wf_reset_bkp_value(WF_RST_USER)), WF_RST_POWER);
}

ZTEST(api_reset, test_names)
{
	for (enum wf_reset r = 0; r < WF_RST_COUNT; r++) {
		zassert_not_equal(strlen(wf_reset_name(r)), 0);
	}
	zassert_str_equal(wf_reset_name(WF_RST_USER), "user_restart");
	zassert_str_equal(wf_reset_name(WF_RST_UPDATE), "update");
	zassert_str_equal(wf_reset_name(WF_RST_COUNT), "unknown");
}

/* SOLL-RT-14: genauerer Ausloeser im zweiten Backup-Register */
ZTEST(api_reset, test_detail)
{
	unsigned int extra;

	zassert_equal(wf_reset_detail_decode(0, &extra), WF_RSTD_NONE);
	zassert_equal(wf_reset_detail_decode(0xFA000003u, &extra), WF_RSTD_NONE, "fremde Kennung");
	zassert_equal(wf_reset_detail_decode(wf_reset_detail_bkp(WF_RSTD_COUNT, 0), &extra),
		      WF_RSTD_NONE);
	zassert_equal(wf_reset_detail_decode(wf_reset_detail_bkp(WF_RSTD_CHARGER, 0), &extra),
		      WF_RSTD_CHARGER);
	zassert_equal(wf_reset_detail_decode(wf_reset_detail_bkp(WF_RSTD_FAULT, 0x1234), &extra),
		      WF_RSTD_FAULT);
	zassert_equal(extra, 0x34, "Zusatz 8 Bit");
	zassert_str_equal(wf_reset_detail_name(WF_RSTD_BLOCKS), "measurement_blocks");
	zassert_str_equal(wf_reset_detail_name(WF_RSTD_NONE), "");
	zassert_str_equal(wf_reset_detail_name(WF_RSTD_COUNT), "");
}
