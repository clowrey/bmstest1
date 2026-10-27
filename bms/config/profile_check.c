// We build this file with -pedantic-errors so that we can catch profiles that
// change a setting without #undef-ing it first.
#include BMS_PROFILE_HEADER

// ISO C forbids an empty translation unit
typedef int profile_check_t;
