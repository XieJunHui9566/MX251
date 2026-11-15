#include <stdio.h>

#ifndef TEST_ABI
#define TEST_ABI
#endif

extern int effect(void);

void plain(void) TEST_ABI { printf("hello\n"); }
void newline(void) TEST_ABI { printf("\n"); }
void multiline(void) TEST_ABI { printf("one\ntwo\n"); }
void crlf(void) TEST_ABI { printf("line\r\n"); }
void shared_unused(void) TEST_ABI { printf("shared\n"); }
int shared_used(void) TEST_ABI { return printf("shared\n"); }
void no_newline(void) TEST_ABI { printf("hello"); }
void escaped_percent(void) TEST_ABI { printf("100%%\n"); }
void formatted(unsigned int x) TEST_ABI { printf("%u\n", x); }
void variable(const char *s) TEST_ABI { printf(s); }
void embedded_zero(void) TEST_ABI { printf("a\0b\n"); }
void extra_argument(void) TEST_ABI { printf("extra\n", effect()); }
