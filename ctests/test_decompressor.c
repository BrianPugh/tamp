#include "unity.h"
#include "tamp/decompressor.h"

void setUp(void) {
    /* This is run before EACH test */
}

void tearDown(void) {
    /* This is run after EACH test */
}

void test_function_add(void) {
    TEST_ASSERT_EQUAL(3, 3);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_function_add);
    return UNITY_END();
}
