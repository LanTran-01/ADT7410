/**
 * @file    test_harness.h
 * @brief   Minimal host unit-test harness (test code, not flight code).
 *
 * Every test case declares a Test Case ID (TC-...) and the LLR clauses it
 * verifies, so each result line traces to the LLR document:
 *
 *   [PASS] TC-ADT-030  LLR-ADT-003.1,003.3,003.4  read 16-bit vectors
 *
 * Usage:
 *   th_begin("TC-ADT-001", "LLR-ADT-001.3", "init rejects NULL p_dev");
 *   TH_CHECK(ADT7410_ERR_PARAM == status);
 *   th_end();
 *   ...
 *   return th_summary("adt7410_driver");
 */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TH_CHECK(cond) th_check((cond), #cond, __FILE__, __LINE__)

static char const * th_tc_id = "";
static char const * th_tc_llr = "";
static char const * th_tc_title = "";
static uint32_t th_tc_fail_count = 0U;
static uint32_t th_tc_check_count = 0U;
static uint32_t th_total_tc = 0U;
static uint32_t th_failed_tc = 0U;
static uint32_t th_total_checks = 0U;


static void th_begin(char const * tc_id,
                     char const * llr,
                     char const * title)
{
    th_tc_id = tc_id;
    th_tc_llr = llr;
    th_tc_title = title;
    th_tc_fail_count = 0U;
    th_tc_check_count = 0U;
}


static void th_check(bool is_ok,
                     char const * expr,
                     char const * file,
                     int line)
{
    th_tc_check_count++;
    th_total_checks++;

    if (!is_ok)
    {
        th_tc_fail_count++;
        (void)printf("    FAIL %s:%d: %s\n", file, line, expr);
    }
    else
    {
        /* Check passed */
    }
}


static void th_end(void)
{
    th_total_tc++;

    if (0U == th_tc_fail_count)
    {
        (void)printf("[PASS] %-11s %-34s %s (%u checks)\n",
                     th_tc_id,
                     th_tc_llr,
                     th_tc_title,
                     (unsigned int)th_tc_check_count);
    }
    else
    {
        th_failed_tc++;
        (void)printf("[FAIL] %-11s %-34s %s (%u/%u checks failed)\n",
                     th_tc_id,
                     th_tc_llr,
                     th_tc_title,
                     (unsigned int)th_tc_fail_count,
                     (unsigned int)th_tc_check_count);
    }
}


static int th_summary(char const * suite_name)
{
    (void)printf("SUMMARY %s: %u test cases, %u passed, %u failed, "
                 "%u checks\n",
                 suite_name,
                 (unsigned int)th_total_tc,
                 (unsigned int)(th_total_tc - th_failed_tc),
                 (unsigned int)th_failed_tc,
                 (unsigned int)th_total_checks);

    return (0U == th_failed_tc) ? 0 : 1;
}

#endif /* TEST_HARNESS_H */
