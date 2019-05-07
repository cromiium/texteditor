/*** includes ***/			// Step 115
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>


/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // essentially let's me handle Ctrl + keypress
#define EDITOR_VERSION "1.0.0"
#define EDITOR_TAB_STOP 4

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/*** data ***/

typedef struct erow{ // used to store a row of text
	int size;
	int rsize;
	char *chars;
	char *render; // used for tab rendering
} erow;

struct editorConfig{
	int cx, cy; // cursor positions
	int rx;
	int rowOff; // row and col offsets
	int colOff;
	int screenRows, screenCols;
	int numRows;
	int dirty;
	erow *row;
	char *fileName;
	char statusMsg[80];
	time_t statusMsgTime;
	struct termios origTermios;
};

struct editorConfig E;

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);

/*** termios ***/

void kill(const char *s){ // error handling. will kill program if it comes across an error
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
  
    perror(s);
    exit(1);
}
void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.origTermios)) // saves current termios "settings"
        kill("tcsetattr");
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &E.origTermios) == -1)
        kill("tcgetattr");

    atexit(disableRawMode); // disableRawMode will call when program terminates

    struct termios raw = E.origTermios; // in general I'm disabling unnecesarry inputs and fixing some output issues
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP); // disables Ctrl-S Ctrl-Q
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG); // ECHO disables echo in terminal ICANON enables canonical mode 
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        kill("tcsetattr");
}

int editorReadKey(){ // waits for a keypress and then returns it
	int nread;
	char c;
	while((nread = read(STDIN_FILENO, &c, 1)) != 1){
		if(nread == -1 && errno != EAGAIN)
			kill("read");
	}
	
	if(c == '\x1b'){
		char seq[3];
		
		if(read(STDIN_FILENO, &seq[0], 1) != 1) 
			return '\x1b';
		if(read(STDIN_FILENO, &seq[1], 1) != 1) 
			return '\x1b';
	
		if(seq[0] == '['){ // this handles arrow keys
			if(seq[1] >= '0' && seq[1] <= '9'){
				if(read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if(seq[2] == '~'){
					switch(seq[1]){
						case '1': return HOME_KEY;	// home and end can be represented by a wide range of escape characters we cover them all
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			}
		
			else{
				switch(seq[1]){
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'F': return END_KEY;
					case 'H': return HOME_KEY;
				}
			}
        }
			else if(seq[0] == 'O'){
				switch(seq[1]){
					case 'F': return END_KEY;
					case 'H': return HOME_KEY;
                }
            }
		return '\x1b';
	}
	else
		return c;
}

int getWindowSize(int *rows, int * cols){
	struct winsize ws;
	
	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) // ioctl() places col & row size in a winsize struct
		return -1;
	else{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** row operations ***/

int cxToRx(erow *row, int cx){ // converts chars index to render index USED IN EDITORSCROLL
	int rx = 0;
	int i;
	for(i = 0; i < cx; i++){
		if(row->chars[i] == '\t')
			rx += (EDITOR_TAB_STOP - 1) - (rx % EDITOR_TAB_STOP);
		rx++;
	}
	return rx;
}

void editorUpdateRow(erow *row){
	int tabs = 0;
	int i;
	for(i = 0; i < row->size; i++){
		if(row->chars[i] == '\t')
			tabs++;
	}
	free(row->render);
	row->render = malloc(row->size + tabs*(EDITOR_TAB_STOP-1) + 1);
	
	int idx;
	for(i = 0; i < row->size; i++){
		if(row->chars[i] == '\t'){
			row->render[idx++] = row->chars[i];
			while(idx%EDITOR_TAB_STOP != 0) row->render[idx++] = ' ';
		}
		else{
			row->render[idx++] = row->chars[i];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorAppendRow(char *s, size_t len){
	E.row = realloc(E.row, sizeof(erow)*(E.numRows+1));
	
	int at = E.numRows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	
	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);
	
	E.numRows++;
	E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c){
	if(at < 0 || at > row->size)
		at = row->size;
	row->chars = realloc(row->chars, row->size+2);
	memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c){
	if(E.cy == E.numRows){
		editorAppendRow(" ", 0);
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}

/*** file i/o ***/

char *editorRowsToString(int *buflen){
	int totlen = 0;
	int i;
	for(i = 0; i < E.numRows; i++){
		totlen += E.row[i].size + 1;
	}
	*buflen = totlen;
	
	char *buf = malloc(totlen);
	char *p = buf;
	for(i = 0; i < E.numRows; i++){
		memcpy(p, E.row[i].chars, E.row[i].size);
		p += E.row[i].size;
		*p = '\n';
		p++;
	}
	return buf;
}

void editorOpen(char *filename){
	free(E.fileName);
	E.fileName = strdup(filename);
	
	FILE *fp = fopen(filename, "r");
	if(!fp)
		kill("fopen");
	
	char *line = NULL;
	size_t lineCap = 0;
	ssize_t lineLen;
	while((lineLen = getline(&line, &lineCap, fp)) != -1){
		while(lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')){
			lineLen--;
		}
		editorAppendRow(line, lineLen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave(){
	if(E.fileName == NULL)
		return;
	int len;
	char *buf = editorRowsToString(&len);
	
	int fd = open(E.fileName, O_RDWR | O_CREAT, 0644); // 0644 standard permission
	if(fd != -1){
		if(ftruncate(fd, len) != -1){
			if(write(fd, buf, len) == len){
				close(fd);
				free(buf);
				E.dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}
	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** append buffer ***/ // this is because orginally I had a lot of small write() functions instead I want one big write function

struct abuf{
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0} // essentially a constructor for abuf

void abAppend(struct abuf *ab, const char *s, int len){
	char *new = realloc(ab->b, ab->len + len); // allocating block of memory that is size of the current string s
	
	if(new == NULL)
		return;
	memcpy(&new[ab->len], s, len); // this is appending the string to the abuf
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab){ // abuf destructor
	free(ab->b);
}

/*** input ***/

void editorMoveCursor(int key) {
	erow *row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
	switch (key) {
		case ARROW_LEFT:
			if(E.cx != 0){
				E.cx--;
			}
			else if(E.cy > 0){
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if(row && E.cx < row->size){
				E.cx++;
			}
			else if(row && E.cx == row->size){
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if(E.cx != 0)	
				E.cy--;
			break;
		case ARROW_DOWN:
			if(E.cy != E.numRows)
				E.cy++;
			break;
	}
	
	row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
	int rowLen = row ? row->size : 0;
	if(E.cx > rowLen)
		E.cx = rowLen;
}

void editorProcessKey(){
	int c = editorReadKey();
	
	switch(c){
		case '\r':
			/* TODO */
			break;
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
			
		case CTRL_KEY('s'):
			editorSave();
			break;
			
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			if(E.cy < E.numRows)
				E.cx = E.row[E.cy].size;
			break;
			
		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			/* TODO */
			break;
			
		case PAGE_UP:
		case PAGE_DOWN:
			{
				if(c == PAGE_UP){
					E.cy = E.rowOff;
				}
				else if(c == PAGE_DOWN){
					E.cy = E.rowOff + E.screenRows - 1;
					if(E.cy > E.numRows)
						E.cy = E.numRows;
				}
				int times = E.screenRows;
				while(times --)
					editorMoveCursor(c = PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
		
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
		
		case CTRL_KEY('l'):
		case '\x1b':
			break;
			
		default:
			editorInsertChar(c);
			break;
	}
}

/*** output ***/

void editorScroll(){
	E.rx = 0;
	if(E.cy < E.numRows){
		E.rx = cxToRx(&E.row[E.cy], E.cx);
	}
	
	if(E.cy < E.rowOff){
		E.rowOff = E.cy;
	}
	if(E.cy >= E.rowOff + E.screenRows){
		E.rowOff = E.cy - E.screenRows+1;
	}
	if(E.rx < E.colOff){
		E.colOff = E.rx;
	}
	if(E.rx >= E.colOff + E.screenCols){
		E.colOff = E.rx - E.screenCols+1;
	}
}
void editorDrawRows(struct abuf *ab){ // some cosmetic niceties. Makes it look like vim with the ~'s
	int i;
	for(i = 0; i < E.screenRows; i++){
		int fileRow = i + E.rowOff;
		if(fileRow >= E.numRows){
			if(E.numRows == 0 && i == E.screenRows / 3){
				char welcome[90];
				int welcomeLen = snprintf(welcome, sizeof(welcome),
					"Editor -- version %s", EDITOR_VERSION);
				if(welcomeLen > E.screenCols)
					welcomeLen = E.screenCols;
				int padding = (E.screenCols - welcomeLen)/2;
				if(padding){
					abAppend(ab, "~", 1);
					padding--;
				}
				while(padding--)
					abAppend(ab, " ", 1);
			}
			else
				abAppend(ab, "~", 1);
		}
		else{
			int len = E.row[fileRow].rsize - E.colOff;
			if(len < 0)
				len = 0;
			if(len > E.screenCols)
				len = E.screenCols;
			abAppend(ab, &E.row[fileRow].render[E.colOff], len);		
		}
		
		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct abuf *ab){
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.fileName ? E.fileName : "[No Name]", E.numRows, E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numRows);
	if(len > E.screenCols)
		len = E.screenCols;
	abAppend(ab, status, len);
	while(len < E.screenCols){
		if(E.screenCols - len == rlen){
			abAppend(ab, rstatus, rlen);
			break;
		}
		else{
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusMsg);
	if(msglen > E.screenCols)
		msglen = E.screenCols;
	if(msglen && time(NULL) - E.statusMsgTime < 5)
		abAppend(ab, E.statusMsg, msglen);
}


void editorRefreshScreen(){ // Alternative is to use ncurses which would actually support more terminals. Instead using VT100 escape characters
	editorScroll();
	
	struct abuf ab = ABUF_INIT;
	
	abAppend(&ab, "\x1b[?25l", 6);
	//abAppend(&ab, "\x1b[2J", 4); // 4 means 4 bytes are being written out \x1b is the escape char //ADDED abAppend(ab, "\x1b[K", 3); to erase as it's drawing
								 // [2J, J clears the screen [ is needed after \x1b and 2 means the entire screen
								 // [1J clears up until the cursor and [0J or [J clears from the cursor until the end of the screen
	abAppend(&ab, "x1b[H", 3);   // H switches cursor position, default arguments are 1;1 but you can do [x;yH
	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);
	
	char buf[32];
	snprintf(buf,sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOff)+1, (E.rx - E.colOff)+1);
	abAppend(&ab, buf, strlen(buf));
	
	abAppend(&ab, "\x1b[?25h", 6);
	
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...){ // al the va stuff comes from stdarg.h this has to do with the variadic parameter
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusMsg, sizeof(E.statusMsg), fmt, ap);
	va_end(ap);
	E.statusMsgTime = time(NULL);
}
	
	
/*** init ***/

void initEditor(){ // initializes fields in E struct
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowOff = 0;
	E.colOff = 0;
	E.numRows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.fileName = NULL;
	E.statusMsg[0] = '\0';
	E.statusMsgTime = 0;
	
	if(getWindowSize(&E.screenRows, &E.screenCols) == -1)
		kill("getWindowSize");
	E.screenRows -= 2;
}

int main(int argc, char *argv[]){

    enableRawMode();
	initEditor();
	if(argc >= 2){
		editorOpen(argv[1]);
	}
	
	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");
	
    while(1){    
		editorRefreshScreen();
        editorProcessKey();
    } 

    return 0;
}
