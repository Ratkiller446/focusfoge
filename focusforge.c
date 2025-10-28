// === focusforge.c ===
// Build: gcc -std=c99 -Wall -Wextra -O2 focusforge.c -lncurses -o focusforge

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <ncurses.h>

/* Define constants */
#define MAX_TASKS 100
#define MAX_TASK_LEN 256
#define FOCUS_DURATION 1500  // 25 minutes in seconds
#define BREAK_DURATION 300   // 5 minutes in seconds
#define DATE_STR_LEN 11      // YYYY-MM-DD = 10 chars + null terminator
#define TIME_STR_LEN 6       // HH:MM = 5 chars + null terminator
#define MAX_INPUT_LEN 512    // Maximum length for user input
#define MAX_PATH_LEN PATH_MAX
#define FOCUSFORGE_VERSION "0.1.0"

/* UI Constants */
#define NOTIFICATION_HEIGHT 3
#define NOTIFICATION_MIN_WIDTH 10
#define HELP_WIDTH 35
#define MIN_TERMINAL_HEIGHT 10
#define MIN_TERMINAL_WIDTH 80

/* Session states */
#define SESSION_INACTIVE 0
#define SESSION_FOCUS 1
#define SESSION_BREAK 2

/* Error logging macros */
#define LOG_ERROR(msg) fprintf(stderr, "ERROR: %s:%d - %s\n", __FILE__, __LINE__, msg)
#define LOG_WARN(msg) fprintf(stderr, "WARNING: %s:%d - %s\n", __FILE__, __LINE__, msg)

/* Data structures */
typedef struct {
    char task[MAX_TASK_LEN];
    int done;  // 0 = not done, 1 = done
} Task;

typedef struct {
    char date[DATE_STR_LEN];
    char time[TIME_STR_LEN];
    int duration;
    char description[MAX_TASK_LEN];
} Session;

typedef struct {
    int streak_max;
    int streak_current;
} StreakData;

/* Function declarations */
void safe_strncpy(char *dest, const char *src, size_t dest_size);
int safe_strtol(const char *str, long *result);
int validate_task_number(const char *str, int *result);
void clear_input_buffer();
void start_command_input();
void finish_command_input();
void start_focus_session();
void start_break_session();
void stop_session();
void skip_session();
void save_settings();
void load_settings();
void handle_key_input(int ch);
void format_time(int total_seconds, char *buffer);
void add_task(const char *text);
void mark_task_done(int index);
void unmark_task(int index);
void remove_task(int index);
void display_tasks();
void save_tasks();
void load_tasks();
int validate_input(const char *input);
int parse_command(char *input);
void log_session();
void update_streaks();
int get_today_sessions_count();
int get_current_streak();
void display_sessions();
void display_help();
void initialize_directories();
void free_resources();
void signal_handler(int sig);
void cleanup_and_exit(int sig);
void resize_handler(int sig);
void run_timer();
void show_notification(const char *message, int duration);
void process_input(char *input);
void display_screen();
void setup_windows();
void destroy_windows();
void handle_resize();
void update_timer_display();
void update_input_display();
void show_notification_window(const char *message, int duration);
int parse_csv_line(const char *line, char *date_part, char *time_part, int *duration, char *task_part);
int is_date_valid(const char *date_str);

/* Global variables */
char focus_task[MAX_TASK_LEN] = "???";
Task tasks[MAX_TASKS];
int num_tasks = 0;
char focusforge_dir[MAX_PATH_LEN];
char tasks_file[MAX_PATH_LEN];
char sessions_file[MAX_PATH_LEN];
char meta_file[MAX_PATH_LEN];
char settings_file[MAX_PATH_LEN];
time_t session_start_time;
int session_state = SESSION_INACTIVE;  // Current session state
int timer_seconds = FOCUS_DURATION;
volatile sig_atomic_t running = 1;  // Use volatile for signal-safe access
volatile sig_atomic_t resize_pending = 0;  // Flag for pending resize
WINDOW *main_win = NULL;
WINDOW *timer_win = NULL;
WINDOW *tasks_win = NULL;
WINDOW *input_win = NULL;
WINDOW *help_win = NULL;
WINDOW *notification_win = NULL;
int show_help = 1;  // Help is always shown now
char input_buffer[MAX_INPUT_LEN] = {0};  // Buffer for incremental input
int input_pos = 0;  // Current position in input buffer
int input_mode = 0;  // 0 = normal, 1 = entering command
time_t notification_end_time = 0;  // When to hide notification
int current_task_index = 0;  // Currently selected task for quick operations

/* Display symbols for different modes - ASCII only */
const char *FOCUS_SYMBOLS = "[FOCUS";
const char *BREAK_SYMBOLS = "[BREAK";
const char *READY_SYMBOLS = "[READY";

/* Function implementations */
void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (dest == NULL || src == NULL || dest_size == 0) {
        return;
    }
    
    size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        src_len = dest_size - 1;
    }
    
    memcpy(dest, src, src_len);
    dest[src_len] = '\0';
}

int safe_strtol(const char *str, long *result) {
    if (str == NULL || result == NULL) {
        return 0;
    }
    
    char *endptr;
    errno = 0;
    *result = strtol(str, &endptr, 10);
    
    // Check for conversion errors
    if (errno != 0 || *endptr != '\0' || *result <= 0 || *result > INT_MAX) {
        return 0;
    }
    
    return 1;
}

int validate_task_number(const char *str, int *result) {
    if (str == NULL || result == NULL) {
        return 0;
    }
    
    char *endptr;
    errno = 0;
    *result = strtol(str, &endptr, 10);
    
    if (errno != 0 || endptr == str || *result <= 0 || *result > MAX_TASKS) {
        return 0;
    }
    
    return 1;
}

void clear_input_buffer() {
    memset(input_buffer, 0, sizeof(input_buffer));
    input_pos = 0;
}

void start_command_input() {
    input_mode = 1;
    clear_input_buffer();
}

void finish_command_input() {
    input_mode = 0;
    if (input_pos > 0) {
        input_buffer[input_pos] = '\0';
        process_input(input_buffer);
        clear_input_buffer();
    }
}

void start_focus_session() {
    if (session_state != SESSION_INACTIVE) {
        show_notification("Session already active", 2);
        return;
    }
    
    session_state = SESSION_FOCUS;
    session_start_time = time(NULL);
    timer_seconds = FOCUS_DURATION;
    show_notification("Focus session started", 2);
    display_screen();
}

void start_break_session() {
    if (session_state != SESSION_INACTIVE) {
        show_notification("Session already active", 2);
        return;
    }
    
    session_state = SESSION_BREAK;
    session_start_time = time(NULL);
    timer_seconds = BREAK_DURATION;
    show_notification("Break session started", 2);
    display_screen();
}

void stop_session() {
    if (session_state == SESSION_INACTIVE) {
        show_notification("No active session", 2);
        return;
    }
    
    if (session_state == SESSION_FOCUS) {
        log_session();
    }
    
    session_state = SESSION_INACTIVE;
    timer_seconds = FOCUS_DURATION;
    show_notification("Session stopped", 2);
    display_screen();
}

void skip_session() {
    if (session_state == SESSION_INACTIVE) {
        show_notification("No active session", 2);
        return;
    }
    
    if (session_state == SESSION_FOCUS) {
        log_session();
        session_state = SESSION_BREAK;
        timer_seconds = BREAK_DURATION;
        show_notification("Focus session completed. Break started.", 2);
    } else if (session_state == SESSION_BREAK) {
        session_state = SESSION_INACTIVE;
        timer_seconds = FOCUS_DURATION;
        show_notification("Break completed. Ready for next focus session.", 2);
    }
    display_screen();
}

void save_settings() {
    FILE *fp = fopen(settings_file, "w");
    if (fp) {
        if (fclose(fp) != 0) {
            LOG_ERROR("Failed to close settings file");
        }
    } else {
        LOG_ERROR("Error creating settings file");
    }
}

void load_settings() {
    FILE *fp = fopen(settings_file, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            // No settings to load anymore since we removed display mode
        }
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to close settings file");
        }
    }
}

void handle_key_input(int ch) {
    // Handle ESC key to cancel input
    if (ch == 27) {
        input_mode = 0;
        clear_input_buffer();
        display_screen();
        return;
    }
    
    // Handle direct quit only when not in input mode
    if (!input_mode && (ch == 'q' || ch == 'Q')) {
        running = 0;
        return;
    }
    
    // Handle help toggle
    if (!input_mode && (ch == '?' || ch == 'h' || ch == 'H')) {
        show_help = !show_help;
        display_screen();
        return;
    }
    
    // Only process input when in input mode
    if (!input_mode) {
        // Single-key commands for Finnish QWERTY keyboard optimization
        switch (ch) {
            // Session controls - left hand home row
            case 'a':  // Start focus session (A is in home row)
            case 'A':
                start_focus_session();
                return;
            case 's':  // Stop session (S is in home row)
            case 'S':
                stop_session();
                return;
            case 'd':  // Skip session (D is in home row)
            case 'D':
                skip_session();
                return;
            case 'f':  // Start break session (F is in home row)
            case 'F':
                start_break_session();
                return;
                
            // Task controls - right hand home row
            case 'j':  // Mark task done (J is in home row)
            case 'J':
                if (num_tasks > 0) {
                    mark_task_done(current_task_index);
                    // Move to next task if available
                    if (current_task_index < num_tasks - 1) {
                        current_task_index++;
                    }
                    display_screen();
                }
                return;
            case 'k':  // Unmark task (K is in home row)
            case 'K':
                if (num_tasks > 0) {
                    unmark_task(current_task_index);
                    display_screen();
                }
                return;
            case 'l':  // Remove task (L is in home row)
            case 'L':
                if (num_tasks > 0) {
                    remove_task(current_task_index);
                    // Adjust selection if needed
                    if (current_task_index >= num_tasks && current_task_index > 0) {
                        current_task_index--;
                    }
                    display_screen();
                }
                return;
                
            // Navigation
            case 'w':  // Move up in task list (W is in home row)
            case 'W':
                if (current_task_index > 0) {
                    current_task_index--;
                    display_screen();
                }
                return;
            case 'x':  // Move down in task list (X is near home row)
            case 'X':
                if (current_task_index < num_tasks - 1) {
                    current_task_index++;
                    display_screen();
                }
                return;
                
            // Quick add task (Enter key)
            case '\n':
            case '\r':
                start_command_input();
                return;
                
            // Quick set focus task (Space key)
            case ' ':
                if (num_tasks > 0) {
                    safe_strncpy(focus_task, tasks[current_task_index].task, MAX_TASK_LEN);
                    show_notification("Focus task updated", 2);
                    display_screen();
                }
                return;
        }
        
        return;
    }
    
    // Handle Enter to process command
    if (ch == '\n' || ch == '\r') {
        finish_command_input();
        return;
    }
    
    // Handle backspace
    if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
        if (input_pos > 0) {
            input_pos--;
            input_buffer[input_pos] = '\0';
            update_input_display();
        }
        return;
    }
    
    // Handle regular character input
    if (ch >= 32 && ch < 127 && input_pos < MAX_INPUT_LEN - 1) {
        input_buffer[input_pos] = ch;
        input_pos++;
        input_buffer[input_pos] = '\0';
        update_input_display();
    }
}

void format_time(int total_seconds, char *buffer) {
    if (buffer == NULL) {
        return;
    }
    
    int total_minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    
    snprintf(buffer, 10, "%02d:%02d", total_minutes % 100, seconds);
}

void add_task(const char *text) {
    if (text == NULL) {
        show_notification("Error: NULL task text", 2);
        return;
    }
    
    if (num_tasks >= MAX_TASKS) {
        show_notification("Maximum number of tasks reached", 2);
        return;
    }
    
    // Use safe string copy
    safe_strncpy(tasks[num_tasks].task, text, MAX_TASK_LEN);
    tasks[num_tasks].done = 0;
    num_tasks++;
    save_tasks();
    show_notification("Task added", 2);
}

void mark_task_done(int index) {
    if (index >= 0 && index < num_tasks) {
        tasks[index].done = 1;
        save_tasks();
        show_notification("Task marked as done", 2);
    } else {
        show_notification("Invalid task number", 2);
    }
}

void unmark_task(int index) {
    if (index >= 0 && index < num_tasks) {
        tasks[index].done = 0;
        save_tasks();
        show_notification("Task unmarked", 2);
    } else {
        show_notification("Invalid task number", 2);
    }
}

void remove_task(int index) {
    if (index >= 0 && index < num_tasks) {
        // Shift all tasks after the removed task up by one
        for (int i = index; i < num_tasks - 1; i++) {
            tasks[i] = tasks[i + 1];
        }
        num_tasks--;
        save_tasks();
        show_notification("Task removed", 2);
    } else {
        show_notification("Invalid task number", 2);
    }
}

void display_tasks() {
    if (tasks_win == NULL) {
        return;
    }
    
    werase(tasks_win);
    box(tasks_win, 0, 0);
    mvwprintw(tasks_win, 1, 1, "TASKS:");
    
    int max_y = getmaxy(tasks_win);
    if (max_y <= 3) {
        wrefresh(tasks_win);
        return;
    }
    
    for (int i = 0; i < num_tasks && i < max_y - 3; i++) {
        const char *status = tasks[i].done ? "X" : " ";
        const char *marker = (i == current_task_index) ? ">" : " ";
        
        // Highlight current task
        if (i == current_task_index) {
            wattron(tasks_win, A_REVERSE);
        }
        
        mvwprintw(tasks_win, i + 2, 1, "%s%d. [%s] %s", marker, i + 1, status, tasks[i].task);
        
        if (i == current_task_index) {
            wattroff(tasks_win, A_REVERSE);
        }
    }
    
    wrefresh(tasks_win);
}

void save_tasks() {
    FILE *fp = fopen(tasks_file, "w");
    if (fp == NULL) {
        LOG_ERROR("Failed to open tasks file for writing");
        return;
    }
    
    for (int i = 0; i < num_tasks; i++) {
        fprintf(fp, "[%c] %s\n", tasks[i].done ? 'X' : ' ', tasks[i].task);
    }
    
    if (fclose(fp) != 0) {
        LOG_ERROR("Failed to close tasks file");
    }
}

void load_tasks() {
    FILE *fp = fopen(tasks_file, "r");
    if (fp == NULL) {
        return;  // If file doesn't exist, just return with empty task list
    }
    
    char line[MAX_INPUT_LEN];
    num_tasks = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL && num_tasks < MAX_TASKS) {
        // Parse the line format: [ ] or [x] followed by task text
        if (strlen(line) >= 4 && line[0] == '[' && line[2] == ']') {
            tasks[num_tasks].done = (line[1] == 'X') ? 1 : 0;
            
            // Extract the task text (skip the "[X] " part)
            char *task_start = line + 4;
            // Remove newline if present
            char *newline = strchr(task_start, '\n');
            if (newline) {
                *newline = '\0';
            }
            
            if (task_start == NULL) continue;
            
            // Use safe string copy
            safe_strncpy(tasks[num_tasks].task, task_start, MAX_TASK_LEN);
            num_tasks++;
        }
    }
    
    if (fclose(fp) != 0) {
        LOG_WARN("Failed to close tasks file");
    }
}

int validate_input(const char *input) {
    if (input == NULL || strlen(input) == 0) {
        return 0;  // Invalid input
    }
    
    // Check for buffer overflow attempts
    if (strlen(input) >= MAX_INPUT_LEN) {
        return 0;  // Input too long
    }
    
    return 1;  // Valid input
}

// Simplified command parsing with better structure
typedef enum {
    CMD_NONE,
    CMD_ADD_TASK,
    CMD_SET_FOCUS,
    CMD_MARK_DONE,
    CMD_UNMARK,
    CMD_REMOVE,
    CMD_START_FOCUS,
    CMD_START_BREAK,
    CMD_STOP,
    CMD_SKIP,
    CMD_QUIT,
    CMD_HELP
} CommandType;

typedef struct {
    CommandType type;
    char argument[MAX_INPUT_LEN];
} ParsedCommand;

int parse_command_input(const char *input, ParsedCommand *cmd) {
    if (!validate_input(input) || !cmd) {
        return 0;
    }
    
    // Initialize command
    cmd->type = CMD_NONE;
    cmd->argument[0] = '\0';
    
    // Make a copy of input to work with
    char cmd_copy[MAX_INPUT_LEN];
    safe_strncpy(cmd_copy, input, sizeof(cmd_copy));
    
    // Remove trailing newline if present
    char *newline = strchr(cmd_copy, '\n');
    if (newline) *newline = '\0';
    newline = strchr(cmd_copy, '\r');
    if (newline) *newline = '\0';
    
    // Skip leading whitespace
    char *cmd_str = cmd_copy;
    while (*cmd_str == ' ' || *cmd_str == '\t') {
        cmd_str++;
    }
    
    if (*cmd_str == '\0') {
        return 1;  // Empty command, valid but no action
    }
    
    // Check for single character commands
    if (strlen(cmd_str) == 1) {
        switch (cmd_str[0]) {
            case 'f':
                cmd->type = CMD_START_FOCUS;
                return 1;
            case 'b':
                cmd->type = CMD_START_BREAK;
                return 1;
            case 's':
                cmd->type = CMD_STOP;
                return 1;
            case 'd':
                cmd->type = CMD_SKIP;
                return 1;
            case 'q':
                cmd->type = CMD_QUIT;
                return 1;
            case 'h':
            case '?':
                cmd->type = CMD_HELP;
                return 1;
        }
    }
    
    // Check for commands with arguments
    if (cmd_str[0] == 'a' && (cmd_str[1] == ' ' || cmd_str[1] == '\t')) {
        cmd->type = CMD_ADD_TASK;
        safe_strncpy(cmd->argument, cmd_str + 2, sizeof(cmd->argument));
        return 1;
    } else if (cmd_str[0] == 't' && (cmd_str[1] == ' ' || cmd_str[1] == '\t')) {
        cmd->type = CMD_SET_FOCUS;
        safe_strncpy(cmd->argument, cmd_str + 2, sizeof(cmd->argument));
        return 1;
    } else if (cmd_str[0] == 'd' && (cmd_str[1] == ' ' || cmd_str[1] == '\t')) {
        cmd->type = CMD_MARK_DONE;
        safe_strncpy(cmd->argument, cmd_str + 2, sizeof(cmd->argument));
        return 1;
    } else if (cmd_str[0] == 'u' && (cmd_str[1] == ' ' || cmd_str[1] == '\t')) {
        cmd->type = CMD_UNMARK;
        safe_strncpy(cmd->argument, cmd_str + 2, sizeof(cmd->argument));
        return 1;
    } else if (cmd_str[0] == 'r' && (cmd_str[1] == ' ' || cmd_str[1] == '\t')) {
        cmd->type = CMD_REMOVE;
        safe_strncpy(cmd->argument, cmd_str + 2, sizeof(cmd->argument));
        return 1;
    }
    
    // If no specific command matched, treat as quick add task
    cmd->type = CMD_ADD_TASK;
    safe_strncpy(cmd->argument, cmd_str, sizeof(cmd->argument));
    return 1;
}

int execute_command(const ParsedCommand *cmd) {
    if (!cmd) {
        return 0;
    }
    
    switch (cmd->type) {
        case CMD_ADD_TASK:
            if (strlen(cmd->argument) > 0) {
                add_task(cmd->argument);
            }
            return 1;
            
        case CMD_SET_FOCUS:
            if (strlen(cmd->argument) > 0) {
                safe_strncpy(focus_task, cmd->argument, MAX_TASK_LEN);
                show_notification("Focus task updated", 2);
            }
            return 1;
            
        case CMD_MARK_DONE: {
            int task_num;
            if (validate_task_number(cmd->argument, &task_num) && task_num > 0 && task_num <= num_tasks) {
                mark_task_done(task_num - 1);
                return 1;
            }
            show_notification("Invalid task number", 2);
            return 0;
        }
            
        case CMD_UNMARK: {
            int task_num;
            if (validate_task_number(cmd->argument, &task_num) && task_num > 0 && task_num <= num_tasks) {
                unmark_task(task_num - 1);
                return 1;
            }
            show_notification("Invalid task number", 2);
            return 0;
        }
            
        case CMD_REMOVE: {
            int task_num;
            if (validate_task_number(cmd->argument, &task_num) && task_num > 0 && task_num <= num_tasks) {
                remove_task(task_num - 1);
                return 1;
            }
            show_notification("Invalid task number", 2);
            return 0;
        }
            
        case CMD_START_FOCUS:
            start_focus_session();
            return 1;
            
        case CMD_START_BREAK:
            start_break_session();
            return 1;
            
        case CMD_STOP:
            stop_session();
            return 1;
            
        case CMD_SKIP:
            skip_session();
            return 1;
            
        case CMD_QUIT:
            running = 0;
            return 1;
            
        case CMD_HELP:
            display_screen();
            return 1;
            
        default:
            return 0;
    }
}

int parse_command(char *input) {
    ParsedCommand cmd;
    int result = parse_command_input(input, &cmd);
    if (result) {
        return execute_command(&cmd);
    }
    return 0;
}

void log_session() {
    time_t end_time = time(NULL);
    int duration = (int)(end_time - session_start_time);
    
    // Get current date and time
    struct tm *start_tm = localtime(&session_start_time);
    if (start_tm == NULL) {
        show_notification("Error getting session time", 2);
        return;
    }
    
    char date_str[DATE_STR_LEN];
    char time_str[TIME_STR_LEN];
    
    strftime(date_str, DATE_STR_LEN, "%Y-%m-%d", start_tm);
    strftime(time_str, TIME_STR_LEN, "%H:%M", start_tm);
    
    // Open sessions file for appending
    FILE *fp = fopen(sessions_file, "a");
    if (fp == NULL) {
        show_notification("Error writing to sessions file", 2);
        return;
    }
    
    fprintf(fp, "%s,%s,%d,\"%s\"\n", date_str, time_str, duration, focus_task);
    
    if (fclose(fp) != 0) {
        show_notification("Error closing sessions file", 2);
    }
    
    // Update streaks
    update_streaks();
}

// Improved CSV parsing function
int parse_csv_line(const char *line, char *date_part, char *time_part, int *duration, char *task_part) {
    if (!line || !date_part || !time_part || !duration || !task_part) {
        return 0;
    }
    
    // Create a copy to work with
    char line_copy[512];
    safe_strncpy(line_copy, line, sizeof(line_copy));
    
    char *ptr = line_copy;
    
    // Extract date
    char *field = ptr;
    ptr = strchr(ptr, ',');
    if (!ptr) return 0;
    *ptr = '\0';
    safe_strncpy(date_part, field, DATE_STR_LEN);
    ptr++;
    
    // Extract time
    field = ptr;
    ptr = strchr(ptr, ',');
    if (!ptr) return 0;
    *ptr = '\0';
    safe_strncpy(time_part, field, TIME_STR_LEN);
    ptr++;
    
    // Extract duration
    field = ptr;
    ptr = strchr(ptr, ',');
    if (!ptr) return 0;
    *ptr = '\0';
    *duration = atoi(field);
    ptr++;
    
    // Extract task (inside quotes)
    if (*ptr == '"') {
        ptr++; // Skip opening quote
        field = ptr;
        ptr = strchr(ptr, '"');
        if (!ptr) return 0;
        *ptr = '\0';
        safe_strncpy(task_part, field, MAX_TASK_LEN);
        return 1;
    }
    
    return 0;
}

// Improved date validation
int is_date_valid(const char *date_str) {
    if (!date_str || strlen(date_str) != 10) {
        return 0;
    }
    
    // Check format YYYY-MM-DD
    if (date_str[4] != '-' || date_str[7] != '-') {
        return 0;
    }
    
    // Check if all other characters are digits
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        
        if (date_str[i] < '0' || date_str[i] > '9') {
            return 0;
        }
    }
    
    // Basic range checking
    int year = atoi(date_str);
    int month = atoi(date_str + 5);
    int day = atoi(date_str + 8);
    
    if (year < 2000 || year > 2100) return 0;
    if (month < 1 || month > 12) return 0;
    if (day < 1 || day > 31) return 0;
    
    return 1;
}

void update_streaks() {
    // Get today's date
    time_t now = time(NULL);
    struct tm *today_tm = localtime(&now);
    if (today_tm == NULL) {
        return;
    }
    
    char today_str[DATE_STR_LEN];
    strftime(today_str, DATE_STR_LEN, "%Y-%m-%d", today_tm);
    
    // Get yesterday's date
    time_t yesterday_time = now - 86400; // 24 hours in seconds
    struct tm *yesterday_tm = localtime(&yesterday_time);
    if (yesterday_tm == NULL) {
        return;
    }
    
    char yesterday_str[DATE_STR_LEN];
    strftime(yesterday_str, DATE_STR_LEN, "%Y-%m-%d", yesterday_tm);
    
    // Load current streak data
    StreakData streak_data = {0, 0};
    
    FILE *fp = fopen(meta_file, "r");
    if (fp != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strncmp(line, "streak_max=", 11) == 0) {
                streak_data.streak_max = atoi(line + 11);
            } else if (strncmp(line, "streak_current=", 15) == 0) {
                streak_data.streak_current = atoi(line + 15);
            }
        }
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to close meta file");
        }
    } else {
        // Non-fatal error, just continue
    }
    
    // Check if we had a session yesterday or today to continue the streak
    int had_session_yesterday = 0;
    int had_session_today = 0;
    
    fp = fopen(sessions_file, "r");
    if (fp != NULL) {
        char line[512];
        char date_part[DATE_STR_LEN];
        char time_part[TIME_STR_LEN];
        int duration;
        char task_part[MAX_TASK_LEN];
        
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (parse_csv_line(line, date_part, time_part, &duration, task_part)) {
                if (strcmp(date_part, yesterday_str) == 0) {
                    had_session_yesterday = 1;
                } else if (strcmp(date_part, today_str) == 0) {
                    had_session_today = 1;
                }
            }
        }
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to close sessions file");
        }
    }
    
    // Update streaks based on session history
    if (had_session_today) {
        // Don't update if already counted today
        return;
    }
    
    if (had_session_yesterday) {
        streak_data.streak_current++;
        if (streak_data.streak_current > streak_data.streak_max) {
            streak_data.streak_max = streak_data.streak_current;
        }
    } else {
        // No session yesterday means we break the streak
        streak_data.streak_current = 1;
    }
    
    // Write updated streak data back to file
    fp = fopen(meta_file, "w");
    if (fp != NULL) {
        fprintf(fp, "streak_max=%d\nstreak_current=%d\n", streak_data.streak_max, streak_data.streak_current);
        if (fclose(fp) != 0) {
            LOG_ERROR("Error closing meta file");
        }
    } else {
        LOG_ERROR("Error writing to meta file");
    }
}

int get_today_sessions_count() {
    time_t now = time(NULL);
    struct tm *today_tm = localtime(&now);
    if (today_tm == NULL) {
        return 0;
    }
    
    char today_str[DATE_STR_LEN];
    strftime(today_str, DATE_STR_LEN, "%Y-%m-%d", today_tm);
    
    int count = 0;
    FILE *fp = fopen(sessions_file, "r");
    if (fp != NULL) {
        char line[512];
        char date_part[DATE_STR_LEN];
        char time_part[TIME_STR_LEN];
        int duration;
        char task_part[MAX_TASK_LEN];
        
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (parse_csv_line(line, date_part, time_part, &duration, task_part)) {
                if (strcmp(date_part, today_str) == 0) {
                    count++;
                }
            }
        }
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to close sessions file");
        }
    }
    
    return count;
}

int get_current_streak() {
    StreakData streak_data = {0, 0};
    
    FILE *fp = fopen(meta_file, "r");
    if (fp != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strncmp(line, "streak_max=", 11) == 0) {
                streak_data.streak_max = atoi(line + 11);
            } else if (strncmp(line, "streak_current=", 15) == 0) {
                streak_data.streak_current = atoi(line + 15);
            }
        }
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to close meta file");
        }
    }
    
    return streak_data.streak_current;
}

void display_sessions() {
    time_t now = time(NULL);
    struct tm *today_tm = localtime(&now);
    if (today_tm == NULL) {
        return;
    }
    
    char today_str[DATE_STR_LEN];
    strftime(today_str, DATE_STR_LEN, "%Y-%m-%d", today_tm);
    
    // Create a new window for session display
    int height = LINES - 4;
    int width = COLS - 4;
    int start_y = 2;
    int start_x = 2;
    
    if (height <= 4 || width <= 4) {
        show_notification("Terminal too small for session display", 2);
        return;
    }
    
    WINDOW *session_win = newwin(height, width, start_y, start_x);
    if (session_win == NULL) {
        show_notification("Error creating session window", 2);
        return;
    }
    
    box(session_win, 0, 0);
    mvwprintw(session_win, 1, 1, "SESSION LOG(%s):", today_str);
    
    FILE *fp = fopen(sessions_file, "r");
    if (fp != NULL) {
        char line[512];
        int found_today = 0;
        int line_count = 0;
        
        // First pass: check if there are any entries for today
        while (fgets(line, sizeof(line), fp) != NULL) {
            char date_part[DATE_STR_LEN];
            char time_part[TIME_STR_LEN];
            int duration;
            char task_part[MAX_TASK_LEN];
            
            if (parse_csv_line(line, date_part, time_part, &duration, task_part)) {
                if (strcmp(date_part, today_str) == 0) {
                    found_today = 1;
                    break;
                }
            }
        }
        
        if (!found_today) {
            mvwprintw(session_win, 3, 1, "(No sessions today)");
        } else {
            // Second pass: read from the beginning to print all today's sessions
            rewind(fp);
            while (fgets(line, sizeof(line), fp) != NULL && line_count < height - 4) {
                char date_part[DATE_STR_LEN];
                char time_part[TIME_STR_LEN];
                int duration;
                char task_part[MAX_TASK_LEN];
                
                if (parse_csv_line(line, date_part, time_part, &duration, task_part)) {
                    if (strcmp(date_part, today_str) == 0) {
                        // Calculate end time from start time and duration
                        struct tm session_tm = {0};
                        if (sscanf(date_part, "%d-%d-%d", &session_tm.tm_year, &session_tm.tm_mon, &session_tm.tm_mday) != 3) {
                            continue;
                        }
                        if (sscanf(time_part, "%d:%d", &session_tm.tm_hour, &session_tm.tm_min) != 2) {
                            continue;
                        }
                        session_tm.tm_year -= 1900;  // Convert to years since 1900
                        session_tm.tm_mon -= 1;      // Convert to months since January (0-11)
                        
                        time_t session_start = mktime(&session_tm);
                        if (session_start == (time_t)-1) {
                            continue;
                        }
                        time_t session_end = session_start + duration;
                        
                        struct tm *end_tm = localtime(&session_end);
                        if (end_tm == NULL) {
                            continue;
                        }
                        char end_time_str[TIME_STR_LEN];
                        strftime(end_time_str, TIME_STR_LEN, "%H:%M", end_tm);
                        
                        // Print in the requested format
                        mvwprintw(session_win, line_count + 3, 1, "- %s–%s → %s", 
                                 time_part, end_time_str, strlen(task_part) > 0 ? task_part : "???");
                        line_count++;
                    }
                }
            }
        }
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to close sessions file");
        }
    } else {
        mvwprintw(session_win, 3, 1, "(No sessions logged)");
    }
    
    wrefresh(session_win);
    
    // Wait for any key press
    mvwprintw(session_win, height - 2, 1, "Press any key to continue...");
    wrefresh(session_win);
    getch();
    
    // Clean up
    delwin(session_win);
    display_screen();
}

void display_help() {
    if (help_win == NULL) {
        return;
    }
    
    // Clear help window
    werase(help_win);
    box(help_win, 0, 0);
    
    // Title
    mvwprintw(help_win, 1, (getmaxx(help_win) - 20) / 2, "FOCUSFORGE HELP");
    
    // Help content
    mvwprintw(help_win, 3, 2, "SESSION COMMANDS:");
    mvwprintw(help_win, 4, 2, "a          - Start focus session");
    mvwprintw(help_win, 5, 2, "f          - Start break session");
    mvwprintw(help_win, 6, 2, "s          - Stop/pause session");
    mvwprintw(help_win, 7, 2, "d          - Skip current session");
    
    mvwprintw(help_win, 9, 2, "TASK COMMANDS:");
    mvwprintw(help_win, 10, 2, "Enter      - Add new task");
    mvwprintw(help_win, 11, 2, "Space      - Set focus task");
    mvwprintw(help_win, 12, 2, "j          - Mark task done");
    mvwprintw(help_win, 13, 2, "k          - Unmark task");
    mvwprintw(help_win, 14, 2, "l          - Remove task");
    mvwprintw(help_win, 15, 2, "w/x        - Navigate tasks");
    
    mvwprintw(help_win, 17, 2, "OTHER:");
    mvwprintw(help_win, 18, 2, "q          - Quit");
    mvwprintw(help_win, 19, 2, "?          - Toggle help");
    
    mvwprintw(help_win, 21, 2, "TIPS:");
    mvwprintw(help_win, 22, 2, "• Work 25 min, break 5 min");
    mvwprintw(help_win, 23, 2, "• After 4 sessions, take");
    mvwprintw(help_win, 24, 2, "  a longer break (15-30 min)");
    mvwprintw(help_win, 25, 2, "• Stay focused on one task");
    mvwprintw(help_win, 26, 2, "• Avoid distractions");
    
    wrefresh(help_win);
}

void initialize_directories() {
    const char *home = getenv("HOME");
    if (home == NULL) {
        fprintf(stderr, "Error: HOME environment variable not set\n");
        exit(1);
    }
    
    int ret = snprintf(focusforge_dir, sizeof(focusforge_dir), "%s/.focusforge", home);
    if (ret < 0 || ret >= (int)sizeof(focusforge_dir)) {
        fprintf(stderr, "Error: Path too long for focusforge directory\n");
        exit(1);
    }
    
    ret = snprintf(tasks_file, sizeof(tasks_file), "%s/tasks.txt", focusforge_dir);
    if (ret < 0 || ret >= (int)sizeof(tasks_file)) {
        fprintf(stderr, "Error: Path too long for tasks file\n");
        exit(1);
    }
    
    ret = snprintf(sessions_file, sizeof(sessions_file), "%s/sessions.csv", focusforge_dir);
    if (ret < 0 || ret >= (int)sizeof(sessions_file)) {
        fprintf(stderr, "Error: Path too long for sessions file\n");
        exit(1);
    }
    
    ret = snprintf(meta_file, sizeof(meta_file), "%s/meta", focusforge_dir);
    if (ret < 0 || ret >= (int)sizeof(meta_file)) {
        fprintf(stderr, "Error: Path too long for meta file\n");
        exit(1);
    }
    
    ret = snprintf(settings_file, sizeof(settings_file), "%s/settings", focusforge_dir);
    if (ret < 0 || ret >= (int)sizeof(settings_file)) {
        fprintf(stderr, "Error: Path too long for settings file\n");
        exit(1);
    }
    
    // Create focusforge directory if it doesn't exist
    struct stat st = {0};
    if (stat(focusforge_dir, &st) == -1) {
        if (mkdir(focusforge_dir, 0755) == -1) {
            if (errno != EEXIST) {
                fprintf(stderr, "Error creating directory %s: %s\n", focusforge_dir, strerror(errno));
                exit(1);
            }
        }
    }
    
    // Create tasks.txt if it doesn't exist
    FILE *fp = fopen(tasks_file, "a");
    if (fp) {
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to close tasks file");
        }
    } else {
        LOG_WARN("Failed to create tasks file");
    }
    
    // Create sessions.csv if it doesn't exist
    fp = fopen(sessions_file, "a");
    if (fp) {
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to create sessions file");
        }
    } else {
        LOG_WARN("Failed to create sessions file");
    }
    
    // Create meta file if it doesn't exist with default values
    fp = fopen(meta_file, "r");
    if (!fp) {
        fp = fopen(meta_file, "w");
        if (fp) {
            fprintf(fp, "streak_max=0\nstreak_current=0\n");
            if (fclose(fp) != 0) {
                LOG_WARN("Failed to close meta file");
            }
        } else {
            LOG_WARN("Failed to create meta file");
        }
    } else {
        if (fclose(fp) != 0) {
            LOG_WARN("Failed to close meta file");
        }
    }
}

void free_resources() {
    // Free any allocated resources
    destroy_windows();
}

void signal_handler(int sig __attribute__((unused))) {
    // Just set the flag, don't do complex operations in signal handler
    running = 0;
}

void cleanup_and_exit(int sig) {
    // Save any pending data
    save_tasks();
    save_settings();
    
    // Free resources
    free_resources();
    
    // End ncurses mode
    endwin();
    
    // Exit with the signal number
    exit(sig);
}

void resize_handler(int sig __attribute__((unused))) {
    // Just set the flag to indicate resize is needed
    resize_pending = 1;
}

void run_timer() {
    // Main loop
    while (running) {
        // Check if resize is pending
        if (resize_pending) {
            handle_resize();
        }
        
        // Check if timer has expired
        if (timer_seconds <= 0 && session_state != SESSION_INACTIVE) {
            // Timer expired
            if (session_state == SESSION_FOCUS) {
                log_session();
                session_state = SESSION_BREAK;
                timer_seconds = BREAK_DURATION;
                show_notification("Focus session completed! Break started.", 3);
            } else if (session_state == SESSION_BREAK) {
                session_state = SESSION_INACTIVE;
                timer_seconds = FOCUS_DURATION;
                show_notification("Break completed! Ready for next focus session.", 3);
            }
        }
        
        // Update display
        update_timer_display();
        update_input_display();
        
        // Check for input (non-blocking)
        timeout(1000);  // Wait 1 second for input
        int ch = getch();
        
        if (ch == ERR) {
            // No input, just decrement timer if active
            if (session_state != SESSION_INACTIVE) {
                timer_seconds--;
            }
        } else {
            // Handle key input
            handle_key_input(ch);
        }
    }
}

// Missing function implementations
void show_notification(const char *message, int duration) {
    // Set notification end time
    notification_end_time = time(NULL) + duration;
    
    // Create notification window
    int height = NOTIFICATION_HEIGHT;
    int width = strlen(message) + 4;  // Add padding
    if (width < NOTIFICATION_MIN_WIDTH) {
        width = NOTIFICATION_MIN_WIDTH;
    }
    
    // Ensure window fits in terminal
    if (width > COLS - 4) {
        width = COLS - 4;
    }
    
    int start_y = LINES - height - 1;
    int start_x = (COLS - width) / 2;
    
    // Destroy existing notification window if any
    if (notification_win) {
        delwin(notification_win);
        notification_win = NULL;
    }
    
    // Create new notification window
    notification_win = newwin(height, width, start_y, start_x);
    if (notification_win == NULL) {
        return;
    }
    
    // Draw notification
    box(notification_win, 0, 0);
    mvwprintw(notification_win, 1, 2, "%s", message);
    wrefresh(notification_win);
}

void show_notification_window(const char *message, int duration) {
    show_notification(message, duration);
}

void process_input(char *input) {
    // Process the input command
    parse_command(input);
}

void display_screen() {
    // Clear screen
    clear();
    
    // Get terminal dimensions
    int height, width;
    getmaxyx(stdscr, height, width);
    
    // Display title
    char title[100];
    snprintf(title, sizeof(title), "FOCUSFORGE v%s", FOCUSFORGE_VERSION);
    mvprintw(0, (width - strlen(title)) / 2, "%s", title);
    
    // Display timer
    char time_str[10];
    format_time(timer_seconds, time_str);
    
    const char *symbol;
    if (session_state == SESSION_FOCUS) {
        symbol = FOCUS_SYMBOLS;
    } else if (session_state == SESSION_BREAK) {
        symbol = BREAK_SYMBOLS;
    } else {
        symbol = READY_SYMBOLS;
    }
    
    mvprintw(2, (width - strlen(time_str) - strlen(symbol) - 3) / 2, "%s %s]", symbol, time_str);
    
    // Display focus task
    mvprintw(4, 2, "Focus: %s", focus_task);
    
    // Display streak info
    int streak = get_current_streak();
    int today_sessions = get_today_sessions_count();
    mvprintw(6, 2, "Streak: %d day(s) | Today: %d session(s)", streak, today_sessions);
    
    // Display tasks
    mvprintw(8, 2, "Tasks:");
    for (int i = 0; i < num_tasks && i < height - 12; i++) {
        const char *status = tasks[i].done ? "X" : " ";
        const char *marker = (i == current_task_index) ? ">" : " ";
        
        // Highlight current task
        if (i == current_task_index) {
            attron(A_REVERSE);
        }
        
        mvprintw(9 + i, 4, "%s%d. [%s] %s", marker, i + 1, status, tasks[i].task);
        
        if (i == current_task_index) {
            attroff(A_REVERSE);
        }
    }
    
    // Display help
    mvprintw(height - 2, 2, "Press '?' for help, 'q' to quit");
    
    // Refresh screen
    refresh();
    
    // Update all windows
    update_timer_display();
    display_tasks();
    if (show_help) {
        display_help();
    }
    update_input_display();
    
    // Check if notification should be hidden
    if (notification_win && time(NULL) >= notification_end_time) {
        delwin(notification_win);
        notification_win = NULL;
    }
}

void setup_windows() {
    // Get terminal dimensions
    int height, width;
    getmaxyx(stdscr, height, width);
    
    // Create main window (full screen)
    main_win = newwin(height, width, 0, 0);
    
    // Create timer window
    int timer_height = 5;
    int timer_width = 30;
    int timer_y = 1;
    int timer_x = (width - timer_width) / 2;
    timer_win = newwin(timer_height, timer_width, timer_y, timer_x);
    
    // Create tasks window
    int tasks_height = height - 10;
    int tasks_width = width - 4;
    int tasks_y = 7;
    int tasks_x = 2;
    tasks_win = newwin(tasks_height, tasks_width, tasks_y, tasks_x);
    
    // Create input window
    int input_height = 3;
    int input_width = width - 4;
    int input_y = height - 4;
    int input_x = 2;
    input_win = newwin(input_height, input_width, input_y, input_x);
    
    // Create help window
    int help_height = height - 4;
    int help_width = HELP_WIDTH;
    int help_y = 2;
    int help_x = width - help_width - 2;
    help_win = newwin(help_height, help_width, help_y, help_x);
}

void destroy_windows() {
    if (main_win) {
        delwin(main_win);
        main_win = NULL;
    }
    
    if (timer_win) {
        delwin(timer_win);
        timer_win = NULL;
    }
    
    if (tasks_win) {
        delwin(tasks_win);
        tasks_win = NULL;
    }
    
    if (input_win) {
        delwin(input_win);
        input_win = NULL;
    }
    
    if (help_win) {
        delwin(help_win);
        help_win = NULL;
    }
    
    if (notification_win) {
        delwin(notification_win);
        notification_win = NULL;
    }
}

void handle_resize() {
    // Clear resize flag
    resize_pending = 0;
    
    // Destroy existing windows
    destroy_windows();
    
    // Setup new windows
    setup_windows();
    
    // Redisplay screen
    display_screen();
}

void update_timer_display() {
    if (timer_win == NULL) {
        return;
    }
    
    werase(timer_win);
    box(timer_win, 0, 0);
    
    char time_str[10];
    format_time(timer_seconds, time_str);
    
    const char *symbol;
    if (session_state == SESSION_FOCUS) {
        symbol = FOCUS_SYMBOLS;
    } else if (session_state == SESSION_BREAK) {
        symbol = BREAK_SYMBOLS;
    } else {
        symbol = READY_SYMBOLS;
    }
    
    mvwprintw(timer_win, 2, (getmaxx(timer_win) - strlen(time_str) - strlen(symbol) - 3) / 2, 
              "%s %s]", symbol, time_str);
    
    wrefresh(timer_win);
}

void update_input_display() {
    if (input_win == NULL) {
        return;
    }
    
    werase(input_win);
    box(input_win, 0, 0);
    
    if (input_mode) {
        mvwprintw(input_win, 1, 1, "Add task: %s", input_buffer);
    } else {
        mvwprintw(input_win, 1, 1, "Enter: add task | Space: set focus | ?: help");
    }
    
    wrefresh(input_win);
}

int main() {
    // Set up signal handlers for clean exit
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    // Set up resize handler
    struct sigaction resize_sa;
    resize_sa.sa_handler = resize_handler;
    sigemptyset(&resize_sa.sa_mask);
    resize_sa.sa_flags = SA_RESTART;
    
    if (sigaction(SIGWINCH, &resize_sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    // Initialize directories and files
    initialize_directories();
    
    // Load settings
    load_settings();
    
    // Load existing tasks
    load_tasks();
    
    // Initialize ncurses
    if (initscr() == NULL) {
        fprintf(stderr, "Error initializing ncurses\n");
        exit(1);
    }
    
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // Hide cursor for cleaner interface
    
    // Check terminal size
    int height, width;
    getmaxyx(stdscr, height, width);
    if (height < MIN_TERMINAL_HEIGHT || width < MIN_TERMINAL_WIDTH) {
        endwin();
        fprintf(stderr, "Terminal too small. Minimum size: %dx%d\n", MIN_TERMINAL_HEIGHT, MIN_TERMINAL_WIDTH);
        exit(1);
    }
    
    // Setup windows
    setup_windows();
    
    // Clear screen and display initial screen
    clear();
    refresh();
    display_screen();
    
    // Run the timer
    run_timer();
    
    // Clean up and exit
    cleanup_and_exit(0);
    
    return 0;
}
