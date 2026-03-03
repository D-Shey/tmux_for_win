#include <windows.h>
#include <stdio.h>
int main() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hOut, &mode);
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, mode)) {
        printf("SetConsoleMode Failed\n");
    }
    printf("\x1b[31mRed Text\x1b[0m\n");
    return 0;
}
