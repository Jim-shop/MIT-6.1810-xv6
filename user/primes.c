#include "kernel/types.h"
#include "user.h"

void filter(int pin[2])
{
    int base, num;
    close(pin[1]);
    if (read(pin[0], (void *)&base, sizeof(int)) == 0)
    {
        return;
    }
    printf("prime %d\n", base);
    int pout[2];
    pipe(pout);
    if (fork() == 0)
    { // kid
        close(pin[0]);
        filter(pout);
    }
    else
    { // parent
        close(pout[0]);
        while (read(pin[0], (void *)&num, sizeof(int)) == sizeof(int))
        {
            if (num % base != 0)
            {
                write(pout[1], (void *)&num, sizeof(int));
            }
        }
        close(pout[1]);
        close(pin[0]);
        wait(0);
    }
}

int main(int argc, char *argv[])
{
    int pout[2];
    pipe(pout);
    if (fork() == 0)
    { // child
        filter(pout);
    }
    else
    { // parent
        close(pout[0]);
        for (int i = 2; i <= 35; i++)
        {
            write(pout[1], (void *)&i, sizeof(int));
        }
        close(pout[1]);
        wait(0);
    }

    exit(0);
}