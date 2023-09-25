#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"
#include <stdbool.h>

#define MAXARGLENGTH 100

int get_token(char *buf)
{
    char c;
    int i = 0;
    while (i != MAXARGLENGTH - 1 && read(0, &c, 1) == 1 && c != '\n')
    {
        if (strchr(" \r\t\v", c)) // skip
        {
            continue;
        }
        else
        {
            buf[i++] = c;
        }
    }
    buf[i] = '\0';
    return i;
}

int main(int argc, char *argv[])
{
    char *eargv[MAXARG];
    int eargc = argc - 1;
    for (int i = 0; i < eargc; i++)
    {
        eargv[i] = argv[i + 1];
    }
    char buf[MAXARGLENGTH];
    while (true)
    {
        int len = get_token(buf);
        if (buf[0] == 0)
        {
            break;
        }
        eargv[eargc] = malloc(len);
        strcpy(eargv[eargc], buf);
        eargc++;
    }
    eargv[eargc] = 0;
    exec(eargv[0], eargv);
    exit(0);
}
