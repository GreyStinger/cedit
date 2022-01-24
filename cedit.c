/*** Includes ***/
#include <ctype.h>
#include <errno.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

/*** Definitions ***/
#define clrscr() write(STDOUT_FILENO, "\033[2J\033[1;1H", 10)
#define CEDIT_VERSION "0.0.1"
#define CTRL_KEY(k) ((k)&0x1f)

enum editorKey
{
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

/*** Data ***/
struct editorConfig
{
	int cx, cy;
	int screen_rows;
	int screen_cols;
	struct termios orig_termios;
};

struct editorConfig E;

/*** Terminal ***/
void die(const char *s)
{
	clrscr();

	perror(s);
	exit(-1);
}

void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;

	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr");
}

int editorReadKey()
{
	int nread;
	char c;

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
	{
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}

	// If we read an escape character, we immediately read two more bytes into the seq buffer.
	if (c == '\033')
	{
		char seq[3];

		// If either of these reads time out (after 0.1 seconds), then we assume the user just pressed the Escape key and return that.
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\033';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\033';

		// Otherwise we look to see if the escape sequence is an arrow key escape sequence.
		if (seq[0] == '[')
		{
			if (seq[1] >= '0' && seq[1] <= '9')
			{
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\033';
				if (seq[2] == '~')
				{
					switch (seq[1])
					{
					case '1':
						return HOME_KEY;
					case '3':
						return DEL_KEY;
					case '4':
						return END_KEY;
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					case '7':
						return HOME_KEY;
					case '8':
						return END_KEY;
					}
				}
			}
			else if (seq[0] == 'O')
			{
				switch (seq[1])
				{
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
				}
			}
			else
			{
				switch (seq[1])
				{
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;
				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
				}
			}
		}

		// If it’s not an escape sequence we recognize, we just return the escape character.
		return '\033';
	}
	else
	{
		return c;
	}
}

int getCursorPosition(int *rows, int *cols)
{
	char buf[32];
	unsigned int i = 0;

	if (write(STDIN_FILENO, "\033[6n", 4) != 4)
		return -1;

	while (i < sizeof(buf) - 1)
	{
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		++i;
	}

	buf[i] = '\0';

	if (buf[0] != '\033' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols)
{
	struct winsize wsize;

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &wsize) == -1 || wsize.ws_col == 0)
	{
		if (write(STDIN_FILENO, "\033[999C\033[999B", 12) != 12)
			return -1;

		return getCursorPosition(rows, cols);
	}
	else
	{
		*rows = wsize.ws_row;
		*cols = wsize.ws_col;
		return 0;
	}
}

/*** Append Buffer ***/
struct abuf
{
	char *b;
	int len;
};

#define ABUF_INIT \
	{             \
		NULL, 0   \
	}

void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL)
		return;

	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab)
{
	free(ab->b);
}

/*** Input ***/
void editorMoveCursor(int key)
{
	switch (key)
	{
	case ARROW_LEFT:
		if (E.cx != 0)
			--E.cx;
		break;
	case ARROW_RIGHT:
		if (E.cx != E.screen_cols - 1)
			++E.cx;
		break;
	case ARROW_DOWN:
		if (E.cy != E.screen_rows - 1)
			++E.cy;
		break;
	case ARROW_UP:
		if (E.cy != 0)
			--E.cy;
		break;
	}
}

void editorProcessKeypress()
{
	int c = editorReadKey();

	switch (c)
	{
	case CTRL_KEY('q'):
		clrscr();

		exit(0);
		break;

	case PAGE_UP:
	case PAGE_DOWN:
	{
		int times = E.screen_rows;
		while (--times)
		{
			editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
		}
	}

	case HOME_KEY:
		E.cx = 0;
		break;

	case END_KEY:
		E.cx = E.screen_cols - 1;
		break;

	case ARROW_UP:
	case ARROW_LEFT:
	case ARROW_DOWN:
	case ARROW_RIGHT:
		editorMoveCursor(c);
		break;
	}
}

/*** Output ***/
void editorDrawRows(struct abuf *ab)
{
	for (int y = 0; y < E.screen_rows; ++y)
	{
		if (y == E.screen_rows / 3)
		{
			char welcome[64];
			int welcome_len = snprintf(welcome, sizeof(welcome),
									   "CEdit editor -- version %s", CEDIT_VERSION);
			if (welcome_len > E.screen_cols)
				welcome_len = E.screen_cols;

			int padding = (E.screen_cols - welcome_len) / 2;

			if (padding)
			{
				abAppend(ab, "~", 1);
				--padding;
			}

			while (--padding)
				abAppend(ab, " ", 1);
			abAppend(ab, welcome, welcome_len);
		}
		else
		{
			abAppend(ab, "~", 1);
		}

		abAppend(ab, "\033[K", 3);
		if (y < E.screen_rows - 1)
		{
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen()
{
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\033[?25l", 6);
	abAppend(&ab, "\033[H", 3);

	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\033[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\033[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);

	abFree(&ab);
}

/*** Init ***/
void initEditor()
{
	E.cx = 0;
	E.cy = 0;

	if (getWindowSize(&E.screen_rows, &E.screen_cols) == -1)
		die("getWindowSize");
}

int main()
{
	enableRawMode();
	initEditor();

	while (1)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
