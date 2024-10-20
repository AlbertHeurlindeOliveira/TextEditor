/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h> //Standard symbolic constants and types
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f) //Sets 3 upper bits to 0, mirrors Ctrl

enum editorKey{
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

// editor row
//POTENTIAL MEMORY LEAK IF *chars is not freed, check this
typedef struct erow {
    int size;
    char* chars;
} erow;

struct editorConfig {
    int cx, cy; //cursor position
    int rowoff; //row offset, for scrolling
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
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
int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){ //We expect 1-byte reads
        //EAGAIN often raised when "there is no data now, try again later"
        if(nread == -1 && errno != EAGAIN) die("read: Error on read");
    }

    /*
    If we read an escape character, we read two more bytes and store in seq
    If timeout, assume esc-press
    If arrow key was pressed return corresponding wsad keypress
    */
    if(c == '\x1b'){
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        /*
        Try to read escape sequence
        If second read byte is a ~ we read another byte expecting it to be a number
        In that case return corresponding keypress
        If second byte read is not a digit, parse it
        Home and End key can be many different escape sequences depending on OS
        */
        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~'){
                    switch(seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if(seq[0] == '0'){
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
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

/*** row operations ***/

/*
Appends another row to erow
*/
void editorAppendRow(char* s, size_t len){
    E.row = realloc(E.row, sizeof(erow)*(E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

/*** file i/o ***/

void editorOpen(char* filename){
    FILE* fp = fopen(filename, "r");
    if(!fp) die("fopen: Error on opening file");

    char* line = NULL;
    size_t linecap = 0;
    //signed size_t
    ssize_t linelen;
    //getline returns -1 when EOF
    while((linelen = getline(&line, &linecap, fp)) != -1){
        while(linelen > 0 && (line[linelen-1] == '\n' ||
                              line[linelen-1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
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

/*
To handle scrolling. Reduces rowoff if cursor moves up, and increases if cursor moves out of screen
Same for columns
*/
void editorScroll(){
    if(E.cy < E.rowoff){
        E.rowoff = E.cy;
    }
    if(E.cy >= E.rowoff + E.screenrows){
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if(E.cx < E.coloff){
        E.coloff = E.cx;
    }
    if(E.cx >= E.coloff + E.screencols){
        E.coloff = E.cx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf* ab){
    int y;
    for(y = 0; y < E.screenrows; y++){
        int filerow = y + E.rowoff;
        if(filerow >= E.numrows) {
            if(E.numrows == 0 && y == E.screenrows / 3){
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
        } else {
            int len = E.row[filerow].size - E.coloff;
            if(len < 0) len = 0;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }

        abAppend(ab, "\x1b[K", 3);
        if(y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(){
    editorScroll();

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

    //Mouse position
    //snprintf prints a string of specific size in specific format
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx -E.coloff) +1); //terminal uses 1-indexing
    abAppend(&ab, buf, strlen(buf));

    //abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6);
    
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key){
    switch(key){
        case ARROW_UP:
            if(E.cy > 0)
                E.cy--;
            break;
        case ARROW_DOWN:
            if(E.cy < E.numrows)
                E.cy++;
            break;
        case ARROW_LEFT:
            if(E.cx > 0)
                E.cx--;
            break;        
        case ARROW_RIGHT:
                E.cx++;
            break;    
    }
}

void editorProcessKeypress(){
    int c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        case PAGE_UP: 
        case PAGE_DOWN:
        {
            int times = E.screenrows;
            while(times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}


/*** init ***/

/*Initializes the fields in the struct E*/
void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;   
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize: Error on retrieving window size");
}

int main(int argc, char* argv[]){
    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }
    //read returns 0 at E0F
    while (1){ //Reads from FILENO to c, 1 byte
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}