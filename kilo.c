/*** includes ***/

#include <unistd.h> //Standard symbolic constants and types
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f) //Sets 3 upper bits to 0, mirrors Ctrl

/*** data ***/

struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios; //To keep track of initial terminal attributes
};

struct editorConfig E;

/*** terminal ***/

void die(const char* s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s); //looks at global errno variable and prints description for it
    exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) //Resets terminal attributes
        die("tcsetattr: Error on resetting terminal attributes\r\n");
}

//Function to enable us to process inputs without them being submitted with 'Enter'
//Also stores initial terminal attributes
void enableRawMode(){

    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr: Error on reading terminal attributes\r\n");
    atexit(disableRawMode); //Call disableRawMode automatically when program exits

    struct termios raw = E.orig_termios;

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

/*Waits for keypress and returns it*/
char editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){ //We expect 1-byte reads
        //EAGAIN often raised when "there is no data now, try again later"
        if(nread == -1 && errno != EAGAIN) die("read: Error on read");
    }
    return c;
}

/*
* Tries to get window size and store it in the int* rows and cols.
* If ioctl fails of number of columns is 0, throws an error
* NOTE!! ioctl() might not work on all systems
* Alternative way: Position the cursor at bottom right of screen
* Then use escape sequences to find out rows and cols on screen (Cursor position report)
*/
int getWindowSize(int* rows, int* cols){
    struct winsize ws; //from sys/ioctl.h

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }

}

/*** append buffer ***/

/* 
* Append buffer consists of a pointer to the buffer in memory and a length
* The ABUF_INIT constant represents an empty buffer, acts as constructor for abuf struct
*/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* s, int len){
    //Allocates memory so that we can store our new string
    char* new = realloc(ab->b, ab->len + len);

    if(new == NULL) return;
    //copies s (len bytes) to &new[ab->len]
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf* ab){
    free(ab->b);
}


/*** output ***/

void editorDrawRows(struct abuf* ab){
    int y;
    for(y = 0; y < E.screenrows; y++){
        if(y == E.screenrows / 3){
            char welcome[80];
            //writes welcome message that fits on screen
            int welcomelen = snprintf(welcome, sizeof(welcome),
            "Kilo editor -- version %s", KILO_VERSION);

            //Ensures welcome message is in middle of screen
            if(welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if(padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while(padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if(y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;
    
    abAppend(&ab, "\x1b[?25l", 6); //hides cursor
    
    /* OLD COMMENT - no longer writes to terminal, but append buffer
    *Writes 4 bytes to the terminal
    *First byte is \x1b, escape character
    *The other three are [2J
    *Escape sequences have the format \x1b[..
    *Instructs the terminal to do text formatting tasks
    *J - erase in display, 2 - clear entire screen
    *H - cursor position, default [1;1H
    */
    //abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6);
    
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/
void editorProcessKeypress(){
    char c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}


/*** init ***/

/*Initializes the fields in the struct E*/
void initEditor(){
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize: Error on retrieving window size");
}

int main(){
    enableRawMode();
    initEditor();

    //read returns 0 at E0F
    while (1){ //Reads from FILENO to c, 1 byte
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}