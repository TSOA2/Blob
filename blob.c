#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

const char *PROMPT = ": ";

void *MALLOC(size_t s)
{
	void *ret = malloc(s);
	if (ret == NULL) {
		perror("ed: malloc\n");
		exit(EXIT_FAILURE);
	}
	return ret;
}

/* Macros for linked list appending, allocating */
#define ALLOC_LL(t) (t) MALLOC(sizeof(t));
#define APPEND_LL(x) \
	do { \
		x->next = ALLOC_LL(typeof(x)); \
		x->next->prev = x; \
		x->next->next = NULL; \
	} while (0);

struct character {
	unsigned char c;
	struct character *next;
	struct character *prev;
};

struct line {
	struct character *data;
	struct line *next;
	struct line *prev;
};

/* Set to 1 by the signal handler if (ctrl+c) is hit. This stops the insertion loop.
 * Note: sig_atomic_t (despite it's name) is not atomic.
 */
sig_atomic_t stop_insertion;

void usage()
{
        (void) fprintf(stderr,
			"\nBlob | TSOA, 2023\n"
			"A line-oriented text editor, which aims to be simple and effective.\n"
			"As of now, it has only been tested on GNU/Linux.\n\n"
			"'n' (next): go to the next line.\n"
			"'b' (back): go to the previous line.\n"
			"'p' (print): print the current line.\n"
			"'i' (insert): insert a single line, after the current line.\n"
			"'l' (list): list the contents of the file.\n"
			"'d' (delete): delete the current line.\n"
			"'q' (quit): quit the editor.\n"
			"'w' (write): write buffer to file.\n"
			"'h' (help): print this message.\n"
			"\nYou can string together commands, like so: 'npi' (next, print, insert).\n"
	);
}

/* Convert a char array to a line */
void charray_to_line(struct line *dest, char *src)
{
	struct character *chars;
	struct character *idx;

	chars = ALLOC_LL(struct character *);
	idx = chars;

	for (; *src != '\n' && *src != '\0'; src++) {
		idx->c = *src;
		APPEND_LL(idx);
		idx = idx->next;
	}

	if ((*src == '\n') || (*src == '\0')) {
		idx->c = ' ';
		idx->next = NULL;
	} else {
		idx = idx->prev;
		free(idx->next);
		idx->next = NULL;
	}

	dest->data = chars;
}

/* Convert a line to a char array */
char *lines_to_charray(struct line *line)
{
	struct character *chars;
	struct character *idx;
	
	char *buffer;
	size_t buffer_size = 0;
	
	chars = line->data;
	idx = chars;

	while (idx != NULL) {
		buffer_size++;
		idx = idx->next;
	}

	buffer = (char *) MALLOC(buffer_size + 2);
	idx = chars;

	for (size_t i = 0; idx != NULL; i++) {
		buffer[i] = (char) idx->c;
		idx = idx->next;
	}

	buffer[buffer_size] = '\n';
	buffer[buffer_size + 1] = '\0';
	return buffer;
}

FILE *create_empty_file(const char *fname)
{
	FILE *create_file;

	create_file = fopen(fname, "w+");
	if (create_file == NULL) {
		perror("ed: create_empty_file");
		exit(EXIT_FAILURE);
	}

	return create_file;
}

/* Read lines from file 'fname', store in dest */
void read_lines(const char *fname, struct line **dest)
{
	FILE *file;

	struct line *idx;

	char *current_line;
	size_t line_size;

	short lines_read;

	file = fopen(fname, "r");
	if (file == NULL) {
		/* No such file or directory */
		if (errno == ENOENT) {
			file = create_empty_file(fname);
		} else {
			perror("ed");
			exit(EXIT_FAILURE);
		}
	}

	*dest = ALLOC_LL(struct line *);
	(*dest)->prev = NULL;
	(*dest)->next = NULL;
	idx = *dest;

	current_line = NULL;
	lines_read = 0;
	while (getline(&current_line, &line_size, file) != -1) {
		charray_to_line(idx, current_line);
		free(current_line);
		APPEND_LL(idx);
		idx = idx->next;
		current_line = NULL;

		if (lines_read == 0) {
			lines_read = 1;
		}
	}

	free(current_line);

	/* If no lines were read, empty buffer */
	if (lines_read == 0) {
		free(*dest);
		*dest = NULL;
		(void) fclose(file);
		return ;
	}

	/* Allocated one extra line, get rid of it */
	idx = idx->prev;
	free(idx->next);
	idx->next = NULL;

	(void) fclose(file);

}

void print_chars(struct character *chars)
{
	while (chars != NULL) {
		(void) putc((int) chars->c, stdout);
		chars = chars->next;
	}
}

void print_line(struct line *line)
{
	if (line != NULL) {
		print_chars(line->data);
	}
	(void) fputc('\n', stdout);
}

void destroy_chars(struct character *chars)
{
	struct character *tmp;

	while (chars != NULL) {
		tmp = chars->next;
		free(chars);
		chars = tmp;
	}
}

void destroy_line(struct line *line)
{
	destroy_chars(line->data);
	free(line);
}

void destroy_lines(struct line *lines)
{
	struct line *tmp;

	while (lines != NULL) {
		tmp = lines->next;
		destroy_line(lines);
		lines = tmp;
	}
}

/* Writes lines to handle/stream */
void lines_to_handle(FILE *file, struct line *lines)
{
	char *line;

	while (lines != NULL) {
		line = lines_to_charray(lines);

		(void) fprintf(file, "%s", line);

		free(line);
		lines = lines->next;
	}
}

void write_lines(const char *fname, struct line *lines)
{
	FILE *file = fopen(fname, "w");

	if (file == NULL) {
		perror("ed");
		exit(EXIT_FAILURE);
	}

	lines_to_handle(file, lines);
	(void) fclose(file);
}

void insert_line(struct line **start, struct line **line)
{
	struct line *new_line;
	char *buffer;
	size_t buffer_size;

	stop_insertion = 0;
	while (!stop_insertion) {
		new_line = ALLOC_LL(struct line *);

		buffer = NULL;
		if (getline(&buffer, &buffer_size, stdin) < 0) {
			/* getline() man page says to free() buffer even if error occured */
			free(buffer);
			exit(EXIT_FAILURE);
		}

		/* Even if (ctrl+c) is hit, we won't know, if we are waiting for input */
		if (stop_insertion) {
			free(buffer);
			break;
		}

		charray_to_line(new_line, buffer);
		free(buffer);

		/* Nothing in the buffer */
		if (*line == NULL) {
			new_line->next = NULL;
			new_line->prev = NULL;
			*line = new_line;
			*start = *line;
			continue;
		}

		new_line->next = (*line)->next;
		new_line->prev = (*line);

		if ((*line)->next != NULL) {
			(*line)->next->prev = new_line;
		}


		(*line)->next = new_line;
		*line = new_line;
	}
}

struct line *delete_line(struct line **start, struct line *line)
{
	struct line *ret;

	if (line == NULL) {
		return NULL;
	}

	/* If first line, move the start of the buffer to the next line */
	if (*start == line) {
		*start = line->next;
	}

	if (line->next != NULL) {
		ret = line->next;
		line->next->prev = line->prev;
	}

	if (line->prev != NULL) {
		ret = line->prev;
		line->prev->next = line->next;
	}

	/* Last line of the file, buffer will be empty */
	if (line->next == NULL && line->prev == NULL) {
		ret = NULL;
	}

	destroy_line(line);

	return ret;
}

/* Runs a line of input from the user */
int run_instructions(const char *fname, struct line **start, struct line **lines, char *s)
{
	for (; *s != '\n' && *s != '\0'; s++) {
		switch (*s) {
			case 'n':; { /* Next line */
				if (*lines == NULL) {
					return -1;
				}

				if ((*lines)->next == NULL) {
					return -1;
				} else {
					(*lines) = (*lines)->next;
				}
				break;
			}
			case 'b':; { /* Back one line */
				if (*lines == NULL) {
					return -2;
				}

				if ((*lines)->prev == NULL) {
					return -2;
				} else {
					(*lines) = (*lines)->prev;
				}
				break;
			}
			case 'p':; {print_line(*lines); break;} /* Print current line */
			case 'i':; { /* Insert lines until user does (ctrl+c) */
				insert_line(start, lines);
				break;
			}
			case 'l':; { /* List contents of the buffer */
				lines_to_handle(stdout, *start);
				break;
			}
			case 'd':; {*lines = delete_line(start, *lines); break;} /* Delete the current line */
			case 'q':; {return -3;} /* Quit Blob */
			case 'w':; {write_lines(fname, *start); break;} /* Write buffer to the file */
			case 'h':; {usage(); break;} /* Print usage message */
			default:; {continue;}
		}
	}

	return 0;
}

/* Handles SIGINT (ctrl+c) */
void sigint_handler(int s)
{
	(void) s;

	/* Used to halt insertion mode */
	stop_insertion = 1;
}

/* Sets up signal handlers */
void handle_signals()
{
	(void) signal(SIGINT, sigint_handler);
}

/* Used to remove the newline at the end of the arg, which may interfere with opening it */
void remove_last_char(char **s)
{
	*((*s) + strlen(*s)) = '\0';
}

int main(int argc, char **argv)
{
	const char *FILE_NAME = (const char *) argv[1];
	struct line *lines;
	struct line *start;
	
	char *input;
	size_t input_size;
	ssize_t getline_ret;

	if (argc != 2) {
		usage();
		exit(EXIT_FAILURE);
	}

	handle_signals();

	remove_last_char(&argv[1]);
	
	read_lines(FILE_NAME, &lines);
	start = lines;

	input = NULL;
	for (;;) {
		(void) fputs(PROMPT, stdout);
		getline_ret = getline(&input, &input_size, stdin);
		if (getline_ret < 0) {
			free(input);
			exit(EXIT_FAILURE);
		}

		switch (run_instructions(FILE_NAME, &start, &lines, input)) {
			case -1:; {(void) fputs("EOF", stdout); break;}
			case -2:; {(void) fputs("START", stdout); break;}
			case -3:; {goto end;}
			case -4:; {write_lines(FILE_NAME, start); break;}
		}

		free(input);
		input = NULL;
	}

end:
	destroy_lines(start);
	free(input);
	exit(EXIT_SUCCESS);
}
