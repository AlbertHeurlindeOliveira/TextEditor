#include <unistd.h> //Standard symbolic constants and types
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

struct termios orig_termios; //To keep track of initial terminal attributes

void disableRawMode(){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); //Resets terminal attributes
}

//Function to enable us to process inputs without them being submitted with 'Enter'
//Also stores initial terminal attributes
void enableRawMode(){

    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode); //Call disableRawMode automatically when program exits

    struct termios raw = orig_termios;

    //Some of these flags probably not necessary but used to be considered
    //necessary for raw input, but will probably not make any differregitgence now
    //These are BRKINT INPCK ISTRIP and CS8
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); //Disable Ctrl-S and Ctrl-Q
    raw.c_oflag &= ~(OPOST); //Output flag
    raw.c_cflag |= ~(CS8); //Sets character size to 8 bits per byte
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(){
    enableRawMode();

    char c;
    //read returns 0 at E0F
    while (1){ //Reads from FILENO to c, 1 byte

        //Prints character and and ASCII value if not control character
        if(iscntrl(c)){
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
    }
    return 0;
}