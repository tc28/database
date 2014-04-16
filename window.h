#ifndef WINDOW_H
#define WINDOW_H

#include <sys/types.h>

typedef struct Window Window;

/* Creates a window with the given title
 * If script is not NULL, the corresponding file
 * will be used as input.  Otherwise, the window
 * will accept user input from stdin.
 */
Window *window_new(char *title, char *script);

/* Deletes the given window */
void window_delete(Window *window);

/* Gets a command from the given window and stores it
 * in the given buffer.
 * Returns 1 on success, 0 on EOF or error
 */
int get_command(Window *window, char *command);

/* Sends the given response string to the given window
 * Returns 1 on success, 0 on error
 */
int send_response(Window *window, char *response);

#endif
