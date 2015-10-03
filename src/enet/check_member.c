#include "check_member.h"
static void pass() {}
int main() { struct TEST_STRUCT test; pass(test.TEST_FIELD); return 0; }
