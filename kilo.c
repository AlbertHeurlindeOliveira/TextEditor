/*** includes ***/

#include <unistd.h> //Standard symbolic constants and types
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

/*** data ***/

struct termios orig_termios; //To keep track of initial terminal attributes

/*** terminal ***/

void die(const char* s){
    perror(s); //looks at global errno variable and prints description for it
    exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) //Resets terminal attributes
        die("tcsetattr: Error on resetting terminal attributes\r\n");
}

//Function to enable us to process inputs without them being submitted with 'Enter'
//Also stores initial terminal attributes
void enableRawMode(){

    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr: Error on reading terminal attributes\r\n");
    atexit(disableRawMode); //Call disableRawMode automatically when program exits

    struct termios raw = orig_termios;

    //Some of these flags probably not necessary but used to be considered
    //necessary for raw input, but will probably not make any differregitgence now
    //These are BRKINT INPCK ISTRIP and CS8
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); //Disable Ctrl-S and Ctrl-Q
    raw.c_oflag &= ~(OPOST); //Output flag
    raw.c_cflag |= ~(CS8); //Sets character size to 8 bits per byte
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);


    raw.c_cc[VMIN] = 0; //Sets minimum number of bytes before read() can return
    raw.c_cc[VTIME] = 1; //How fast read() times out in tenth of seconds

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr: Error on setting raw struct\r\n");
}

/*** init ***/

int main(){
    enableRawMode();

    //read returns 0 at E0F
    while (1){ //Reads from FILENO to c, 1 byte
        char c = '\0';
        if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read\r\n");
        //Prints character and and ASCII value if not control character
        if(iscntrl(c)){
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if(c == 'q') break;
    }
    return 0;
}