#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <ncurses.h>
#include <locale.h>

#define GIST_ID ""
#define GITHUB_TOKEN ""
#define FILE_NAME ""
#define API_URL ""

struct string {
    char *ptr;
    size_t len;
};

void init_string(struct string *s) {
    s->len = 0;
    s->ptr = malloc(1);
    if (s->ptr == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s) {
    size_t new_len = s->len + size * nmemb;
    s->ptr = realloc(s->ptr, new_len + 1);
    if (s->ptr == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    memcpy(s->ptr + s->len, ptr, size * nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;
    return size * nmemb;
}

char *fetch_gist_content() {
    CURL *curl = curl_easy_init();
    struct string response;
    init_string(&response);

    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Authorization: token " GITHUB_TOKEN);
        headers = curl_slist_append(headers, "User-Agent: ATM-Simulator");

        curl_easy_setopt(curl, CURLOPT_URL, API_URL);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            fprintf(stderr, "CURL request failed: %s\n", curl_easy_strerror(res));
            free(response.ptr);
            return NULL;
        }
    }

    struct json_object *parsed_json = json_tokener_parse(response.ptr);
    if (parsed_json == NULL) {
        fprintf(stderr, "JSON parsing failed: %s\n", response.ptr);
        free(response.ptr);
        return NULL;
    }

    struct json_object *files, *file_content, *content;
    if (!json_object_object_get_ex(parsed_json, "files", &files) ||
        !json_object_object_get_ex(files, FILE_NAME, &file_content) ||
        !json_object_object_get_ex(file_content, "content", &content)) {
        fprintf(stderr, "Invalid JSON structure\n");
        json_object_put(parsed_json);
        free(response.ptr);
        return NULL;
    }

    char *data = strdup(json_object_get_string(content));
    json_object_put(parsed_json);
    free(response.ptr);
    return data;
}

static void escape_string(const char *src, char *dst, size_t dst_size) {
    size_t pos = 0;
    for (; *src && pos + 2 < dst_size; src++) {
        if (*src == '\\' || *src == '"') {
            dst[pos++] = '\\';
            dst[pos++] = *src;
        } else if (*src == '\n') {
            if (pos + 2 >= dst_size) break;
            dst[pos++] = '\\';
            dst[pos++] = 'n';
        } else {
            dst[pos++] = *src;
        }
    }
    dst[pos] = '\0';
}

// FUNCTION TO IGNORE JSON DUMP
static size_t discard_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return size * nmemb; 
}

void update_gist_content(const char *updated_content) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize CURL\n");
        return;
    }

    char escaped_content[8192];
    escape_string(updated_content, escaped_content, sizeof(escaped_content));

    char json_payload[16384];
    snprintf(json_payload, sizeof(json_payload),
             "{\"files\": {\"%s\": {\"content\": \"%s\"}}}",
             FILE_NAME, escaped_content);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Authorization: token " GITHUB_TOKEN);
    headers = curl_slist_append(headers, "User-Agent: ATM-Simulator");
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, API_URL);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);

    // ignore json response
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to update gist: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
}

// rebuild csv
void reload_data_and_update(const char *regno, int pin, const char *user_name, int new_balance)
{
    char *temp_data = fetch_gist_content();
    if (!temp_data) {
        fprintf(stderr, "Failed to retrieve data\n");
        return;
    }

    char final_data[8192] = "";
    char *line = strtok(temp_data, "\n");
    while (line) {
        char file_regno[20], file_name[50];
        int file_pin, file_balance;
        sscanf(line, "%[^,],%d,%[^,],%d", file_regno, &file_pin, file_name, &file_balance);

        if (strcmp(file_regno, regno) == 0) {
            snprintf(final_data + strlen(final_data),
                     sizeof(final_data) - strlen(final_data),
                     "%s,%d,%s,%d\n", regno, pin, user_name, new_balance);
        }
        else {
            strncat(final_data, line, sizeof(final_data) - strlen(final_data) - 1);
            strncat(final_data, "\n", sizeof(final_data) - strlen(final_data) - 1);
        }
        line = strtok(NULL, "\n");
    }

    update_gist_content(final_data);
    free(temp_data);
}

static int read_line_with_esc(char *buffer, int buffer_size) {
    memset(buffer, 0, buffer_size);
    int idx = 0;
    while (1) {
        int c = getch();
        if (c == 27) { // ESC
            buffer[0] = '\0';
            return 1; 
        } else if (c == '\n') {
            buffer[idx] = '\0';
            return 0; 
        } else if (c == KEY_BACKSPACE || c == 127) {
            if (idx > 0) {
                idx--;
                buffer[idx] = '\0';
                // visual remove
                int y, x;
                getyx(stdscr, y, x);
                if (x > 0) {
                    mvaddch(y, x - 1, ' ');
                    move(y, x - 1);
                }
            }
        } else if (isprint(c) && idx < buffer_size - 1) {
            buffer[idx++] = (char)c;
            addch(c);
        }
        refresh();
    }
}

// REMEMBER 1 if esc
static int read_line_with_esc_in_window(WINDOW *win, char *buffer, int buffer_size) {
    memset(buffer, 0, buffer_size);
    int idx = 0;
    while (1) {
        int c = wgetch(win);
        if (c == 27) { // ESC
            buffer[0] = '\0';
            return 1;
        } else if (c == '\n') {
            buffer[idx] = '\0';
            return 0;
        } else if (c == KEY_BACKSPACE || c == 127) {
            if (idx > 0) {
                idx--;
                buffer[idx] = '\0';
                // visually remove
                int y, x;
                getyx(win, y, x);
                if (x > 0) {
                    mvwaddch(win, y, x - 1, ' ');
                    wmove(win, y, x - 1);
                }
            }
        } else if (isprint(c) && idx < buffer_size - 1) {
            buffer[idx++] = (char)c;
            waddch(win, c);
        }
        wrefresh(win);
    }
}

void process_transaction(char *regno, int pin) {
    char *data = fetch_gist_content();
    if (!data) {
        fprintf(stderr, "Failed to retrieve data\n");
        return;
    }

    int found = 0, balance = 0;
    char user_name[50];
    char new_data[8192] = "";
    char *line = strtok(data, "\n");

    while (line) {
        char file_regno[20], file_name[50];
        int  file_pin, file_balance;
        sscanf(line, "%[^,],%d,%[^,],%d", file_regno, &file_pin, file_name, &file_balance);
        if (strcmp(file_regno, regno) == 0) {
            found = 1;
            if (file_pin != pin) {
                printf("Incorrect PIN.\n");
                free(data);
                return;
            }
            strcpy(user_name, file_name);
            balance = file_balance;
        }
        char temp[100];
        snprintf(temp, sizeof(temp), "%s,%d,%s,%d\n", file_regno, file_pin, file_name, file_balance);
        strcat(new_data, temp);
        line = strtok(NULL, "\n");
    }

    free(data);
    if (!found) {
        printf("User not found!\n");
        return;
    }

    // main menu LOOP
    while (1) {
        clear();
        const char *ascii_art[] = {
            "██████╗  █████╗ ███╗   ██╗██╗  ██╗         ██████╗ ███████╗         █████╗ ███╗   ███╗██████╗ ██╗████████╗ █████╗ ",
            "██╔══██╗██╔══██╗████╗  ██║██║ ██╔╝        ██╔═══██╗██╔════╝        ██╔══██╗████╗ ████║██╔══██╗██║╚══██╔══╝██╔══██╗",
            "██████╔╝███████║██╔██╗ ██║█████╔╝         ██║   ██║█████╗          ███████║██╔████╔██║██████╔╝██║   ██║   ███████║",
            "██╔══██╗██╔══██║██║╚██╗██║██╔═██╗         ██║   ██║██╔══╝          ██╔══██║██║╚██╔╝██║██╔══██╗██║   ██║   ██╔══██║",
            "██████╔╝██║  ██║██║ ╚████║██║  ██╗        ╚██████╔╝██║             ██║  ██║██║ ╚═╝ ██║██║  ██║██║   ██║   ██║  ██║",
            "╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═╝         ╚═════╝ ╚═╝             ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝╚═╝   ╚═╝   ╚═╝  ╚═╝"
        };
        int ascii_lines = sizeof(ascii_art) / sizeof(ascii_art[0]);
        // APPROXIMATE FIX THIS SHIT!!
        int ascii_width = 108; 
        // topline
        int ascii_start_y = (LINES - ascii_lines) / 4; // shift up
        int ascii_start_x = (COLS - ascii_width) / 2;

        for (int i = 0; i < ascii_lines; i++) {
            mvprintw(ascii_start_y + i, ascii_start_x, "%s", ascii_art[i]);
        }
        // refresh
        refresh();

        // box
        int menu_h = 15, menu_w = 60;
        int starty = ascii_start_y + ascii_lines + 2; // lines below the art
        int startx = (COLS - menu_w) / 2;
        WINDOW *menuwin = newwin(menu_h, menu_w, starty, startx);
        keypad(menuwin, TRUE);
        box(menuwin, 0, 0);

        mvwprintw(menuwin, 0, menu_w - 12, "ESC to Exit");
        mvwprintw(menuwin, 2, 2, "Welcome to Bank of Amrita, %s!", user_name);
        mvwprintw(menuwin, 4, 2, "Use ARROW KEYS to navigate, ENTER to select:");
        mvwprintw(menuwin, 6, 4, "1. Check Balance");
        mvwprintw(menuwin, 7, 4, "2. Withdraw");
        mvwprintw(menuwin, 8, 4, "3. Deposit");
        mvwprintw(menuwin, 9, 4, "4. Exit");
        mvwprintw(menuwin, 10, 4, "5. Account Settings");
        wrefresh(menuwin);

        int choice_idx = 0;
        int ch;
        while (1) {
            for (int i = 0; i < 5; i++) {
                if (i == choice_idx) wattron(menuwin, A_REVERSE);
                mvwprintw(menuwin, 6 + i, 4, "%d. ", i + 1);
                switch (i) {
                    case 0: wprintw(menuwin, "Check Balance");    break;
                    case 1: wprintw(menuwin, "Withdraw");         break;
                    case 2: wprintw(menuwin, "Deposit");          break;
                    case 3: wprintw(menuwin, "Exit");             break;
                    case 4: wprintw(menuwin, "Account Settings"); break;
                }
                wattroff(menuwin, A_REVERSE);
            }
            wrefresh(menuwin);

            ch = wgetch(menuwin);
            if (ch == KEY_UP) {
                choice_idx = (choice_idx == 0) ? 4 : choice_idx - 1;
            } else if (ch == KEY_DOWN) {
                choice_idx = (choice_idx == 4) ? 0 : choice_idx + 1;
            } else if (ch == 27) {
                // esc on main menu
                delwin(menuwin);
                clear();
                return;
            } else if (ch == '\n') {
                break;
            }
        }
        delwin(menuwin);
        refresh();

        // clear screen
        // create box
        int sub_h = 15, sub_w = 60;  // Same size if desired
        int sy = (LINES - sub_h) / 2;
        int sx = (COLS - sub_w) / 2;
        WINDOW *subwin = newwin(menu_h, menu_w, starty, startx);
        keypad(subwin, TRUE);
        box(subwin, 0, 0);
        mvwprintw(subwin, 0, sub_w - 15, "ESC to Go Back");
        wrefresh(subwin);

        int choice = choice_idx + 1;
        switch (choice) {
            case 1: // check balance
                mvwprintw(subwin, 2, 2, "Your balance: %d", balance);
                wrefresh(subwin);
                while ((ch = wgetch(subwin)) != 27) { }
                delwin(subwin);
                break;

            case 2: { // withdraw
                mvwprintw(subwin, 2, 2, "Enter amount to withdraw: ");
                wmove(subwin, 3, 2);
                wrefresh(subwin);
                echo();
                char amt_str[12];
                noecho();
                // esc to exit
                if (read_line_with_esc_in_window(subwin, amt_str, sizeof(amt_str))) {
                    delwin(subwin);
                    break;
                }
                int amount = atoi(amt_str);
                if (amount > balance) {
                    mvwprintw(subwin, 5, 2, "Insufficient balance!");
                } else {
                    balance -= amount;
                    mvwprintw(subwin, 5, 2, "Withdrawal successful! New balance: %d", balance);
                    reload_data_and_update(regno, pin, user_name, balance);
                }
                wrefresh(subwin);
                while ((ch = wgetch(subwin)) != 27) { }
                delwin(subwin);
                break;
            }

            case 3: { // deposit
                mvwprintw(subwin, 2, 2, "Enter amount to deposit: ");
                wmove(subwin, 3, 2);
                wrefresh(subwin);
                echo();
                char amt_str[12];
                noecho();
                if (read_line_with_esc_in_window(subwin, amt_str, sizeof(amt_str))) {
                    delwin(subwin);
                    break;
                }
                int amount = atoi(amt_str);
                balance += amount;
                mvwprintw(subwin, 5, 2, "Deposit successful! New balance: %d", balance);
                reload_data_and_update(regno, pin, user_name, balance);
                wrefresh(subwin);
                while ((ch = wgetch(subwin)) != 27) { }
                delwin(subwin);
                break;
            }

            case 4: // exit
                delwin(subwin);
                clear();
                mvprintw(0, 0, "Exiting...");
                refresh();
                return;

            case 5: { // account Settings
                mvwprintw(subwin, 2, 2, "1. Change Name");
                mvwprintw(subwin, 3, 2, "2. Change PIN");
                mvwprintw(subwin, 5, 2, "Choose (ESC to cancel):");
                wmove(subwin, 5, 27);
                wrefresh(subwin);
                echo();
                char settings_choice_str[3];
                noecho();
                if (read_line_with_esc_in_window(subwin, settings_choice_str, sizeof(settings_choice_str))) {
                    delwin(subwin);
                    break;
                }
                int settingsChoice = atoi(settings_choice_str);
                if (settingsChoice == 1) {
                    mvwprintw(subwin, 7, 2, "Enter your new name:");
                    wmove(subwin, 8, 2);
                    wrefresh(subwin);
                    echo();
                    char new_name[50];
                    noecho();
                    if (!read_line_with_esc_in_window(subwin, new_name, sizeof(new_name))) {
                        strcpy(user_name, new_name);
                        mvwprintw(subwin, 9, 2, "Name changed to: %s", user_name);
                        reload_data_and_update(regno, pin, user_name, balance);
                    }
                } else if (settingsChoice == 2) {
                    mvwprintw(subwin, 7, 2, "Enter your new PIN:");
                    wmove(subwin, 8, 2);
                    wrefresh(subwin);
                    echo();
                    char new_pin_str[10];
                    noecho();
                    if (!read_line_with_esc_in_window(subwin, new_pin_str, sizeof(new_pin_str))) {
                        pin = atoi(new_pin_str);
                        mvwprintw(subwin, 9, 2, "PIN changed to: %d", pin);
                        reload_data_and_update(regno, pin, user_name, balance);
                    }
                } else {
                    mvwprintw(subwin, 7, 2, "Invalid option!");
                }
                wrefresh(subwin);
                while ((ch = wgetch(subwin)) != 27) { }
                delwin(subwin);
                break;
            }

            default:
                delwin(subwin);
                break;
        }
    }
}

int main() {
    setlocale(LC_ALL, "");
    // init
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    // centre window
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int win_height = 10;
    int win_width = 40;
    int starty = (max_y - win_height) / 2;
    int startx = (max_x - win_width) / 2;
    WINDOW *loginwin = newwin(win_height, win_width, starty, startx);
    box(loginwin, 0, 0);

    // login
    mvwprintw(loginwin, 1, (win_width - 11) / 2, " LOGIN ");
    mvwprintw(loginwin, 3, 2, "Register No: ");
    mvwprintw(loginwin, 5, 2, "PIN: ");
    wmove(loginwin, 3, 15);
    wrefresh(loginwin);

    // regno
    char regno[20];
    wgetnstr(loginwin, regno, sizeof(regno) - 1);

    // move to pin
    wmove(loginwin, 5, 7);
    wrefresh(loginwin);

    // pin
    char pin_str[10];
    wgetnstr(loginwin, pin_str, sizeof(pin_str) - 1);
    int pin = atoi(pin_str);

    delwin(loginwin);
    clear();
    refresh();

    process_transaction(regno, pin);

    endwin();
    return 0;
}