#include <lib.h>

int main()
{
    debugf("sending 5 to myself\n");
    kill(0, 5);
    debugf("the program will run as usual\n");
    
    return 0;
}