#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

#define GIST_ID getenv("GIST_ID")
#define GITHUB_TOKEN getenv("GITHUB_TOKEN")
#define FILE_NAME "nfc_data.csv"
#define API_URL getenv("API_URL") 

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
    } else {
        printf("Gist updated successfully\n");
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

void process_transaction(char *regno, int pin) {
    char *data = fetch_gist_content();
    if (data == NULL) {
        fprintf(stderr, "Failed to retrieve data\n");
        return;
    }

    char new_data[8192] = "";
    int found = 0, balance = 0;
    char user_name[50];

    char *line = strtok(data, "\n");
    while (line) {
        char file_regno[20], file_name[50];
        int file_pin, file_balance;
        sscanf(line, "%[^,],%d,%[^,],%d", file_regno, &file_pin, file_name, &file_balance);

        if (strcmp(file_regno, regno) == 0) {
            found = 1;
            if (file_pin != pin) {
                printf("Incorrect PIN.\n");
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

    if (!found) {
        printf("User not found!\n");
        return;
    }

    int choice, amount;
    while (1) {
        printf("\nWelcome to Bank of Amrita, %s!\n", user_name);
        printf("1. Check Balance\n2. Withdraw\n3. Deposit\n4. Exit\n5. Account Settings\nEnter your choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                printf("Your balance: %d\n", balance);
                break;
            case 2:
                printf("Enter amount to withdraw: ");
                scanf("%d", &amount);
                if (amount > balance) {
                    printf("Insufficient balance!\n");
                } else {
                    balance -= amount;
                    printf("Withdrawal successful! New balance: %d\n", balance);
                }
                break;
            case 3:
                printf("Enter amount to deposit: ");
                scanf("%d", &amount);
                balance += amount;
                printf("Deposit successful! New balance: %d\n", balance);
                break;
            case 4:
                printf("Exiting...\n");
                free(data);
                return;
            case 5:
                printf("ACCOUNT SETTINGS:\n");
                printf("1. Change Name\n");
                printf("2. Change PIN\n");
                printf("Enter your choice: ");

                int settingsChoice;
                scanf("%d", &settingsChoice);

                if (settingsChoice == 1) {
                    char new_name[50];
                    printf("Enter your new name: ");
                    scanf(" %[^\n]", new_name);  // reads until newline
                    strcpy(user_name, new_name);
                    printf("Name changed to: %s\n", user_name);
                } else if (settingsChoice == 2) {
                    int new_pin;
                    printf("Enter your new PIN: ");
                    scanf("%d", &new_pin);
                    pin = new_pin;
                    printf("PIN changed to: %d\n", pin);
                } else {
                    printf("Invalid option!\n");
                }

                // append instead of overwriting
                reload_data_and_update(regno, pin, user_name, balance);
                break;
            default:
                printf("Invalid choice! Try again.\n");
        }

        reload_data_and_update(regno, pin, user_name, balance);
    }
}

int main() {
    char regno[20];
    int pin;

    printf("==== ATM LOGIN ====\n");
    printf("Enter Register Number: ");
    scanf("%s", regno);
    printf("Enter PIN: ");
    scanf("%d", &pin);

    process_transaction(regno, pin);
    return 0;
}