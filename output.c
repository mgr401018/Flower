#include <stdio.h>
#include <stdbool.h>
#include <string.h>

int main(void) {
    char a[256];
    strcpy(a , "hello");
    scanf("%s", &a);
    printf("%s", a);
    double r;
    r  = 3.18;
    printf("%lf", r);
    scanf("%lf", &r);
    printf("%lf", r);
    return 0;
}
