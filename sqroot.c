#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

int main(int argc, char *argv[]) {
    int num = atoi(argv[argc - 1]);

    if (num < 0) {
        printf("Unable to execute");
    } else {
        if (argc == 2) {
            int result = sqrt((int)num);
            printf("%d\n", result);
        } else {
            int result = sqrt((int)num);
            char resultStr[32]; 
            snprintf(resultStr, sizeof(resultStr), "%d", result);
            strncpy(argv[argc - 1], resultStr, strlen(resultStr) + 1);
            execv(argv[1], argv + 1);
        }
    }
    return 0;
}
