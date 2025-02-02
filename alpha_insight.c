/**
 * 
 * 
 * @file alpha_insight.c
 * @brief Financial Data Viewer - A tool for fetching and analyzing stock data.
 * 
 * This program fetches stock data using MarketStack and FMP APIs, visualizes
 * the data as a price chart, performs basic and advanced analysis, and provides
 * financial metrics.
 * 
 * @version 1.0
 * @date 2025-01-26
 * 
 * @dependencies:
 * - libcurl
 * - libjson-c
 * 
 * @compilation:
 * For Mac:
 * gcc -o fmp fmp.c -I/opt/homebrew/opt/json-c/include -L/opt/homebrew/opt/json-c/lib -lcurl -ljson-c -lm
 * 
 * For Linux:
 * gcc -o stock_wise stock_wise.c -lcurl -ljson-c -lm
 * 
 * @run:
 * ./stock_wise
 */

// Required header files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>

// Program configuration constants
#define MAX_HOURS 24
#define CHART_WIDTH 80
#define CHART_HEIGHT 20
#define PRICE_MARGIN 10
#define TIME_MARGIN 2
#define URL_SIZE 512
#define BUFFER_SIZE 16384
#define AUTH0_DOMAIN "dev-0dm11qnmxfq40m5k.us.auth0.com"
#define AUTH0_CLIENT_ID "vlzyuHacV8TKS18rEiDpxO3r2ILRhqaK"            // Replace with your Auth0 client ID
#define AUTH0_CLIENT_SECRET "rgISeRQJ51OrUs8xLVvSaBaIHiHo__bg9CQ6ab6am71pKBBbeVwJGRV-F5LZJeSx"    // Replace with your Auth0 client secret
#define GROQ_API_KEY "gsk_somevalidgroqapikey"  // Replace with actual Groq API key
#define GROQ_API_URL "https://api.groq.com/openai/v1/chat/completions"
#define GROQ_MODEL "llama2-70b-4096"  // Groq's LLama2 model
#define GROQ_MAX_TOKENS 4096
#define ANALYSIS_BUFFER_SIZE 8192



// API configuration
//1fff94c91b8e7d884bef5f0b53ca2035
#define MARKETSTACK_API_KEY "70aa32e769b9eb7a7b44bcf6cdcbbe65"
//Rq6khXXAlBK1G1q4C3qOY2eDNoKNCcf0
#define FMP_API_KEY "CUXsKprAdSyGQMhmxaJrxGaj5gAonXmP"
#define GPT_API_KEY "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"
#define GPT_API_URL "https://api.openai.com/v1/chat/completions"

// ANSI color codes
#define RESET "\033[0m"
#define GREEN "\033[32m"
#define RED "\033[31m"
#define BLUE "\033[34m"
#define BOLD "\033[1m"
#define YELLOW "\033[33m"

// Structures
/**
 * @struct TimeInterval
 * @brief Represents a time interval for historical price comparison.
 */
struct TimeInterval {
    const char *label; ///< Label for the time interval (e.g., "1 hour").
    int hours_ago;     ///< Number of hours ago from the current time.
};

/**
 * @struct MemoryStruct
 * @brief Structure to hold data fetched from the API.
 */
struct MemoryStruct {
    char *memory; ///< Pointer to the memory holding the fetched data.
    size_t size;  ///< Size of the fetched data.
};

typedef struct {
    char access_token[1024];
    char user_id[256];
    time_t expires_at;
} auth_session;

// Add this to the existing structures section
struct GroqResponse {
    char *text;
    size_t size;
};



// Function prototypes
void display_menu();
void basic_analysis(const char* symbol, float prices[], int num_points, char timestamps[][9]);
void advanced_analysis(const char* symbol);
int perform_gpt_analysis(const char* symbol, char* response_buffer);
void fetch_and_display(const char* stock, const char* api_key, const char* endpoint, 
                      const char* columns[], int num_columns, const char* label);
void display_table(struct json_object* root, const char* columns[], int num_columns, const char* label);
void print_overview(const char* symbol, float prices[], int num_points, char timestamps[][9]);
void print_historical_comparison(const char* symbol, float current_price);
void format_number(double num, char *buffer, size_t size);
char* fetch_data(const char* url);
void parse_financial_data(const char* json_str, char* output);
auth_session* perform_auth0_login(const char* username, const char* password);
bool verify_auth_session(auth_session* session);
void cleanup_auth_session(auth_session* session);
int perform_groq_analysis(const char* symbol, char* response_buffer);
void advanced_analysis_with_groq(const char* symbol);
char* prepare_groq_prompt(const char* symbol);


/**
 * @brief Callback function to write fetched data into memory.
 * 
 * @param contents Pointer to the data fetched from the API.
 * @param size Size of each element.
 * @param nmemb Number of elements.
 * @param userp Pointer to the MemoryStruct to store the data.
 * @return Size of the data written.
 */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        printf("Not enough memory\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/**
 * @brief Clears the console screen.
 */
void clear_screen() {
    printf("\033[2J");
    printf("\033[H");
}

/**
 * @brief Calculates the percentage change between two prices.
 * 
 * @param current_price The current price.
 * @param past_price The past price.
 * @return The percentage change.
 */
double calculate_percentage_change(double current_price, double past_price) {
    if (past_price > 0) {
        return ((current_price - past_price) / past_price) * 100.0;
    }
    return 0.0;
}

/**
 * @brief Prints the price analysis for a given stock symbol.
 * 
 * @param symbol The stock symbol.
 * @param prices Array of historical prices.
 * @param num_points Number of price points.
 */
void print_price_analysis(const char* symbol, float prices[], int num_points) {
    float current_price = prices[num_points-1];
    float opening_price = prices[0];
    float high = prices[0];
    float low = prices[0];
    
    for(int i = 0; i < num_points; i++) {
        if(prices[i] > high) high = prices[i];
        if(prices[i] < low) low = prices[i];
    }

    printf("\n%s%sStock Analysis for %s%s\n", BLUE, BOLD, symbol, RESET);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    printf("Current Price: ");
    printf("%s$%.2f%s\n", current_price >= opening_price ? GREEN : RED, current_price, RESET);

    float change = current_price - opening_price;
    float change_pct = calculate_percentage_change(current_price, opening_price);
    printf("Price Change: ");
    if(change >= 0) {
        printf("%s+$%.2f (+%.2f%%)%s\n", GREEN, change, change_pct, RESET);
    } else {
        printf("%s-$%.2f (%.2f%%)%s\n", RED, -change, change_pct, RESET);
    }

    printf("Day's Range: ");
    printf("%s$%.2f%s - %s$%.2f%s\n", 
           YELLOW, low, RESET, 
           YELLOW, high, RESET);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int hour = t->tm_hour;
    int is_weekend = (t->tm_wday == 0 || t->tm_wday == 6);
    printf("Market Status: ");
    if (is_weekend) {
        printf("%sClosed (Weekend)%s\n", RED, RESET);
    } else if (hour < 9 || hour >= 16) {
        printf("%sClosed%s\n", RED, RESET);
    } else {
        printf("%sOpen%s\n", GREEN, RESET);
    }

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

/**
 * @brief Fetches stock data from the MarketStack API.
 * 
 * @param symbol The stock symbol.
 * @param prices Array to store fetched prices.
 * @param timestamps Array to store timestamps.
 * @return The number of data points fetched.
 */
int fetch_stock_data(const char *symbol, float prices[], char timestamps[][9]) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    char url[URL_SIZE];
    time_t now;
    struct tm *local_time;
    char end_date[11], start_date[11];

    // Get current date
    time(&now);
    // Add 30 days to ensure we have enough historical data to work with
    time_t end = now;
    time_t start = now - (30 * 24 * 60 * 60); // Go back 30 days

    local_time = localtime(&end);
    strftime(end_date, sizeof(end_date), "%Y-%m-%d", local_time);
    
    local_time = localtime(&start);
    strftime(start_date, sizeof(start_date), "%Y-%m-%d", local_time);

    chunk.memory = malloc(1);
    chunk.size = 0;

    // Use EOD endpoint with a larger date range and limit
    snprintf(url, URL_SIZE, 
        "http://api.marketstack.com/v1/eod?"
        "access_key=%s&symbols=%s"
        "&date_from=%s&date_to=%s"
        "&limit=100&sort=desc",  // Sort by descending date to get most recent first
        MARKETSTACK_API_KEY, symbol, start_date, end_date);
    
    printf("Debug - Fetching data from: %s\n", url);

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);   // Skip SSL verification
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);        //  20 second timeout

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "API request failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return 0;
        }

        struct json_object *parsed_json = json_tokener_parse(chunk.memory);
        if (!parsed_json) {
            printf("Failed to parse API response\n");
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return 0;
        }

        // Check for error message in response
        struct json_object *error_obj;
        if (json_object_object_get_ex(parsed_json, "error", &error_obj)) {
            const char *error_msg = json_object_get_string(error_obj);
            printf("API Error: %s\n", error_msg);
            json_object_put(parsed_json);
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return 0;
        }

        struct json_object *data_array;
        if (!json_object_object_get_ex(parsed_json, "data", &data_array) ||
            !json_object_is_type(data_array, json_type_array)) {
            printf("No valid data found in response\n");
            json_object_put(parsed_json);
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return 0;
        }

        int available_points = json_object_array_length(data_array);
        if (available_points == 0) {
            printf("No data points available for %s\n", symbol);
            json_object_put(parsed_json);
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return 0;
        }

        // Use minimum between available points, MAX_HOURS, and what we need
        int count = (available_points < MAX_HOURS) ? available_points : MAX_HOURS;
        printf("Processing %d most recent data points\n", count);

        for(int i = 0; i < count; i++) {
            struct json_object *data = json_object_array_get_idx(data_array, i);
            struct json_object *close_obj, *date_obj;

            if (!json_object_object_get_ex(data, "close", &close_obj) ||
                !json_object_object_get_ex(data, "date", &date_obj)) {
                continue;
            }

            // Store the price
            prices[i] = json_object_get_double(close_obj);

            // Format the timestamp (use market close time 16:00:00)
            const char *date_str = json_object_get_string(date_obj);
            if (strlen(date_str) >= 10) { // Ensure we have a valid date string
                strncpy(timestamps[i], "16:00:00", 8);
                timestamps[i][8] = '\0';
            }
        }

        json_object_put(parsed_json);
        curl_easy_cleanup(curl);
        free(chunk.memory);

        return count;
    }

    free(chunk.memory);
    return 0;
}

/**
 * @brief Formats a number into a human-readable string with suffixes (K, M, B).
 * 
 * @param num The number to format.
 * @param buffer The buffer to store the formatted string.
 * @param size The size of the buffer.
 */
void format_number(double num, char *buffer, size_t size) {
    if (num >= 1e9) {
        snprintf(buffer, size, "%.2f B", num / 1e9);
    } else if (num >= 1e6) {
        snprintf(buffer, size, "%.2f M", num / 1e6);
    } else if (num >= 1e3) {
        snprintf(buffer, size, "%.2f K", num / 1e3);
    } else {
        snprintf(buffer, size, "%.2f", num);
    }
}

/**
 * @brief Displays financial data in a formatted table.
 * 
 * @param root Pointer to the JSON object containing the data.
 * @param columns Array of column names to display.
 * @param num_columns Number of columns.
 * @param label Title for the table.
 */
void display_table(struct json_object *root, const char *columns[], int num_columns, const char *label) {
    printf("\n%s:\n", label);
    printf("==========================================================================================\n");

    // Print column headers
    for (int i = 0; i < num_columns; i++) {
        printf("| %-20s ", columns[i]);
    }
    printf("|\n");
    printf("==========================================================================================\n");

    int array_len = json_object_array_length(root);

    // Loop through the JSON array and print rows
    for (int i = 0; i < array_len; i++) {
        struct json_object *row = json_object_array_get_idx(root, i);
        
        for ( int j = 0; j < num_columns; j++) {
            struct json_object *field;
            if (json_object_object_get_ex(row, columns[j], &field)) {
                if (json_object_is_type(field, json_type_int) || 
                    json_object_is_type(field, json_type_double)) {
                    char formatted_num[32];
                    format_number(json_object_get_double(field), formatted_num, sizeof(formatted_num));
                    printf("| %-20s ", formatted_num);
                } else if (json_object_is_type(field, json_type_string)) {
                    printf("| %-20s ", json_object_get_string(field));
                } else {
                    printf("| %-20s ", "N/A");
                }
            } else {
                printf("| %-20s ", "N/A");
            }
        }
        printf("|\n");
    }
    printf("==========================================================================================\n");
}

/**
 * @brief Draws a price chart visualization based on historical prices.
 * 
 * @param prices Array of historical prices.
 * @param timestamps Array of timestamps corresponding to the prices.
 * @param num_points Number of price points.
 * @param symbol The stock symbol.
 */
void draw_chart(float prices[], char timestamps[][9], int num_points, const char *symbol) {
    float min_price = prices[0];
    float max_price = prices[0];
    for (int i = 0; i < num_points; i++) {
        if (prices[i] < min_price) min_price = prices[i];
        if (prices[i] > max_price) max_price = prices[i];
    }
    
    min_price = floor(min_price);
    max_price = ceil(max_price);
    float price_range = max_price - min_price;
    
    float scale = (CHART_HEIGHT - TIME_MARGIN) / price_range;
    
    char chart[CHART_HEIGHT][CHART_WIDTH];
    memset(chart, ' ', sizeof(chart));
    
    // Draw grid
    for (int i = 0; i < CHART_HEIGHT - TIME_MARGIN; i++) {
        if (i % 4 == 0) {
            for (int j = PRICE_MARGIN; j < CHART_WIDTH; j++) {
                chart[i][j] = '-';
            }
        }
    }
    
    for (int j = PRICE_MARGIN; j < CHART_WIDTH; j += 10) {
        for (int i = 0; i < CHART_HEIGHT - TIME_MARGIN; i++) {
            chart[i][j] = '|';
        }
    }
    
    // Plot the price line
    for (int i = 0; i < num_points - 1; i++) {
        float x1 = (i * (CHART_WIDTH - PRICE_MARGIN)) / (float)(num_points - 1);
        float x2 = ((i + 1) * (CHART_WIDTH - PRICE_MARGIN)) / (float)(num_points - 1);
        float y1 = (max_price - prices[i]) * scale;
        float y2 = (max_price - prices[i + 1]) * scale;
        
        float slope = (y2 - y1) / (x2 - x1);
        for (float x = x1; x < x2; x += 0.25) {
            float y = y1 + slope * (x - x1);
            int plot_x = (int)round(x) + PRICE_MARGIN;
            int plot_y = (int)round(y);
            
            if (plot_x >= PRICE_MARGIN && plot_x < CHART_WIDTH && 
                plot_y >= 0 && plot_y < CHART_HEIGHT - TIME_MARGIN) {
                if (slope > 0.1) chart[plot_y][plot_x] = '/';
                else if (slope < -0.1) chart[plot_y][plot_x] = '\\';
                else chart[plot_y][plot_x] = '-';
            }
        }
    }
    
    // Print chart
    printf("%sPrice Chart - Last %d Hours%s\n\n", BOLD, num_points, RESET);
    
    for (int i = 0; i < CHART_HEIGHT - TIME_MARGIN; i++) {
        float price = max_price - (i * price_range / (CHART_HEIGHT - TIME_MARGIN - 1));
        printf("$%-7.2f ", price);
        
        for (int j = PRICE_MARGIN; j < CHART_WIDTH; j++) {
            if (chart[i][j] == '/' || chart[i][j] == '\\' || chart[i][j] == '-') {
                if (prices[num_points-1] > prices[0]) {
                    printf(GREEN "%c" RESET, chart[i][j]);
                } else {
                    printf(RED "%c" RESET, chart[i ][j]);
                }
            } else {
                printf("%c", chart[i][j]);
            }
        }
        printf("\n");
    }
    
    printf("        ");
    for (int i = 0; i < num_points; i += num_points / 6) {
        printf("%-10s ", timestamps[i]);
    }
    printf("\n\n");
}

/**
 * @brief Fetches and displays financial data from the Financial Modeling Prep API.
 * 
 * @param stock The stock symbol.
 * @param api_key The API key for authentication.
 * @param endpoint The API endpoint to fetch data from.
 * @param columns Array of column names to display.
 * @param num_columns Number of columns.
 * @param label Title for the table.
 */
void fetch_and_display(const char *stock, const char *api_key, const char *endpoint, 
                      const char *columns[], int num_columns, const char *label) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    char url[512];

    chunk.memory = malloc(1);
    chunk.size = 0;

    snprintf(url, sizeof(url), 
        "https://financialmodelingprep.com/api/v3/%s/%s?period=annual&apikey=%s", 
        endpoint, stock, api_key);

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "Failed to fetch data: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return;
        }

        struct json_object *parsed_json = json_tokener_parse(chunk.memory);
        if (!parsed_json) {
            printf("Failed to parse JSON response\n");
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return;
        }

        display_table(parsed_json, columns, num_columns, label);

        json_object_put(parsed_json);
        free(chunk.memory);
        curl_easy_cleanup(curl);
    }
}

/**
 * @brief Prints a historical price comparison table for a given stock symbol.
 * 
 * @param symbol The stock symbol.
 * @param current_price The current price of the stock.
 */
void print_historical_comparison(const char* symbol, float current_price) {
    struct TimeInterval intervals[] = {
        {"1 hour", 1},
        {"4 hours", 4},
        {"8 hours", 8},
        {"24 hours", 24},
        {"1 week", 24 * 7},
        {"1 month", 24 * 30},
        {"3 months", 24 * 30 * 3}
    };

    printf("\n%sHistorical Price Comparison%s\n", BOLD, RESET);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Interval          | Percentage Change\n");
    printf("------------------|-------------------\n");

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    for (int i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
        char date[11];
        time_t past = now - (intervals[i].hours_ago * 3600);
        struct tm *past_tm = localtime(&past);
        strftime(date, sizeof(date), "%Y-%m-%d", past_tm);

        char url[URL_SIZE];
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
        chunk.size = 0;

        snprintf(url, URL_SIZE, 
            "http://api.marketstack.com/v1/eod?"
            "access_key=%s&symbols=%s&date_from=%s&date_to=%s&limit=1",
            MARKETSTACK_API_KEY, symbol, date, date);

        CURL *curl = curl_easy_init();
        if(curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

            CURLcode res = curl_easy_perform(curl);
            if(res == CURLE_OK) {
                struct json_object *parsed_json;
                struct json_object *data;
                parsed_json = json_tokener_parse(chunk.memory);

                if (json_object_object_get_ex(parsed_json, "data", &data) &&
                    json_object_array_length(data) > 0) {
                    struct json_object *first_entry = json_object_array_get_idx(data, 0);
                    struct json_object *close_price;

                    if (json_object_object_get_ex(first_entry, "close", &close_price)) {
                        double past_price = json_object_get_double(close_price);
                        double change = calculate_percentage_change(current_price, past_price);
                        
                        printf("%-16s | ", intervals[i].label);
                        if (change > 0) {
                            printf(GREEN "+%8.2f%%\n" RESET, change);
                        } else {
                            printf(RED "%8.2f%%\n" RESET, change);
                        }
                    }
                    json_object_put(parsed_json);
                }
            }
            curl_easy_cleanup(curl);
            free(chunk.memory);
        }
    }
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

/**
 * @brief Fetches data from a given URL.
 * 
 * @param url The URL to fetch data from.
 * @return Pointer to the fetched data as a string.
 */
char* fetch_data(const char* url) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return NULL;
        }
        curl_easy_cleanup(curl);
        return chunk.memory;
    }
    free(chunk.memory);
    return NULL;
}

/**
 * @brief Parses financial data from a JSON string into a readable format.
 * 
 * @param json_str The JSON string containing financial data.
 * @param output The buffer to store the parsed output.
 */
void parse_financial_data(const char* json_str, char* output) {
    struct json_object *parsed_json = json_tokener_parse(json_str);
    if (!parsed_json || !json_object_is_type(parsed_json, json_type_array)) {
        strcat(output, "No data available\n");
        return;
    }

    int len = json_object_array_length(parsed_json);
    if (len > 0) {
        struct json_object *latest = json_object_array_get_idx(parsed_json, 0);
        json_object_object_foreach(latest, key, val) {
            if (json_object_is_type(val, json_type_double) || 
                json_object_is_type(val, json_type_int)) {
                char formatted_num[32];
                format_number(json_object_get_double(val), formatted_num, sizeof(formatted_num));
                char line[256];
                snprintf(line, sizeof(line), "%s: %s\n", key, formatted_num);
                strcat(output, line);
            }
        }
    }

    json_object_put(parsed_json);
}

/**
 * @brief Performs advanced analysis using GPT for a given stock symbol.
 * 
 * @param symbol The stock symbol to analyze.
 * @param response_buffer The buffer to store the GPT response.
 * @return 1 if successful, 0 otherwise.
 */
int perform_gpt_analysis(const char* symbol, char* response_buffer) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    char payload[2048];
    snprintf(payload, sizeof(payload),
        "{\"model\": \"gpt-3.5-turbo\", \"messages\": [{\"role\": \"user\", \"content\": "
        "\"Analyze the financial health of the stock %s by implementing a retrieval-based approach to gather and assess data. "
        "Follow these steps:\\n"
        "1. **Data Retrieval:**\\n"
        "- Use real-time APIs or web scraping to fetch key financial data, including income statement, balance sheet, cash flow statement, and performance metrics (e.g., P/E ratio, revenue growth, debt-to-equity ratio).\\n"
        "- Ensure that the retrieval process accounts for the most recent data, filtering for accuracy and relevance.\\n"
        "2. **Data Summarization:**\\n"
        "- Summarize the retrieved data as bullet points, emphasizing key insights such as revenue trends, profitability, liquidity, valuation ratios, and any growth metrics.\\n"
        "3. **Grading System:**\\n"
        "- Develop a stock grading system that evaluates the financial health and investment potential on a scale of 0-100.\\n"
        "- Base the grading on weighted criteria such as profitability, growth rates, and financial stability relative to industry benchmarks.\\n"
        "4. **Investment Recommendation:**\\n"
        "- Provide a clear investment recommendation (Buy, Hold, or Sell ) with a detailed justification based on the stock score and other qualitative insights.\\n"
        "- Include a risk assessment and any potential growth opportunities.\\n"
        "5. **New Retrieval Features:**\\n"
        "- Suggest methods to improve the retrieval process, such as using machine learning to analyze historical data patterns, integrating multiple data sources to cross-verify metrics, and adding sentiment analysis from news and social media.\\n"
        "Ensure clarity, accuracy, and relevance throughout the analysis. Structure the output with distinct sections for data retrieval, summary, grading, and recommendations. Keep it more concise and include as many numericals and specifics of the sources over where the information is from.\"}]}",
        symbol);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", GPT_API_KEY);
    headers = curl_slist_append(headers, auth_header);

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, GPT_API_URL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return 0;
        }

        struct json_object *parsed_json = json_tokener_parse(chunk.memory);
        struct json_object *choices, *first_choice, *message, *content;
        
        json_object_object_get_ex(parsed_json, "choices", &choices);
        first_choice = json_object_array_get_idx(choices, 0);
        json_object_object_get_ex(first_choice, "message", &message);
        json_object_object_get_ex(message, "content", &content);
        
        strncpy(response_buffer, json_object_get_string(content), BUFFER_SIZE);
        
        json_object_put(parsed_json);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(chunk.memory);
        
        return 1;
    }
    return 0;
}

/**
 * @brief Performs basic analysis for a given stock symbol.
 * 
 * @param symbol The stock symbol to analyze.
 * @param prices Array of historical prices.
 * @param num_points Number of price points.
 * @param timestamps Array of timestamps corresponding to the prices.
 */
void basic_analysis(const char* symbol, float prices[], int num_points, char timestamps[][9]) {
    printf("\n%sBasic Analysis for %s%s\n", BOLD, symbol, RESET);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // Calculate basic statistics
    float current_price = prices[num_points - 1];
    float opening_price = prices[0];
    
    // Find high and low
    float high = prices[0];
    float low = prices[0];
    float sum = prices[0];
    float prev_price = prices[0];
    int up_movements = 0;
    int down_movements = 0;
    
    for(int i = 1; i < num_points; i++) {
        if(prices[i] > high) high = prices[i];
        if(prices[i] < low) low = prices[i];
        sum += prices[i];
        
        // Count price movements
        if(prices[i] > prev_price) up_movements++;
        else if(prices[i] < prev_price) down_movements++;
        prev_price = prices[i];
    }
    
    // Calculate average price
    float average = sum / num_points;
    
    // Calculate price volatility (standard deviation)
    float variance = 0;
    for(int i = 0; i < num_points; i++) {
        variance += pow(prices[i] - average, 2);
    }
    float volatility = sqrt(variance / num_points);
    
    // Calculate price momentum
    float momentum = ((current_price - opening_price) / opening_price) * 100;
    
    // Price movement trend
    float trend_strength = ((float)(up_movements - down_movements) / (up_movements + down_movements)) * 100;

    // Print Basic Price Statistics
    printf("\n%sPrice Statistics:%s\n", BLUE, RESET);
    printf("Current Price: $%.2f\n", current_price);
    printf("Opening Price: $%.2f\n", opening_price);
    printf("High: $%.2f\n", high);
    printf("Low: $%.2f \n", low);
    printf("Average Price: $%.2f\n", average);
    printf("Price Volatility: $%.2f\n", volatility);

    // Print Technical Indicators
    printf("\n%sTechnical Indicators:%s\n", BLUE, RESET);
    printf("Momentum: %s%.2f%%%s\n", 
           momentum >= 0 ? GREEN : RED, momentum, RESET);
    printf("Trend Strength: %s%.2f%%%s\n", 
           trend_strength >= 0 ? GREEN : RED, fabs(trend_strength), RESET);
    
    // Print Movement Analysis
    printf("\n%sPrice Movement Analysis:%s\n", BLUE, RESET);
    printf("Upward Movements: %d\n", up_movements);
    printf("Downward Movements: %d\n", down_movements);

    // Simple Trading Signals
    printf("\n%sTrading Signals:%s\n", BLUE, RESET);
    
    // Price relative to average
    if(current_price > average) {
        printf("• Price is %sABOVE%s average by %.2f%%\n", 
               GREEN, RESET, ((current_price - average) / average) * 100);
    } else {
        printf("• Price is %sBELOW%s average by %.2f%%\n", 
               RED, RESET, ((average - current_price) / average) * 100);
    }
    
    // Momentum signal
    if(momentum > 5) {
        printf("• Strong %sUPWARD%s momentum\n", GREEN, RESET);
    } else if(momentum < -5) {
        printf("• Strong %sDOWNWARD%s momentum\n", RED, RESET);
    } else {
        printf("• %sNEUTRAL%s momentum\n", YELLOW, RESET);
    }
    
    // Volatility assessment
    float volatility_percentage = (volatility / average) * 100;
    if(volatility_percentage > 2) {
        printf("• %sHIGH%s volatility (%.2f%%)\n", RED, RESET, volatility_percentage);
    } else if(volatility_percentage > 1) {
        printf("• %sMODERATE%s volatility (%.2f%%)\n", YELLOW, RESET, volatility_percentage);
    } else {
        printf("• %sLOW%s volatility (%.2f%%)\n", GREEN, RESET, volatility_percentage);
    }

    printf("\n%sNote:%s This is a basic technical analysis based on price action only.\n", YELLOW, RESET);
    printf("For a more comprehensive analysis, please use the Advanced Analysis option.\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

/**
 * @brief Performs advanced analysis for a given stock symbol using GPT.
 * 
 * @param symbol The stock symbol to analyze.
 */
void advanced_analysis(const char* symbol) {
    printf("\n%sPerforming Advanced Analysis with GPT...%s\n", BLUE, RESET);
    
    char response_buffer[BUFFER_SIZE];
    if(perform_gpt_analysis(symbol, response_buffer)) {
        printf("\n%sGPT Analysis Results:%s\n", BOLD, RESET);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("%s\n", response_buffer);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    } else {
        printf("%sFailed to perform GPT analysis%s\n", RED, RESET);
    }
}



// Function to perform analysis using Groq API
int perform_groq_analysis(const char* symbol, char* response_buffer) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    // Prepare the analysis prompt
    char* prompt = prepare_groq_prompt(symbol);
    
    // Prepare the JSON payload
    char payload[4096];
    snprintf(payload, sizeof(payload),
        "{"
        "\"model\": \"%s\","
        "\"messages\": ["
        "    {\"role\": \"user\", \"content\": \"%s\"}"
        "],"
        "\"temperature\": 0.7,"
        "\"max_tokens\": %d"
        "}", 
        GROQ_MODEL, prompt, GROQ_MAX_TOKENS);

    free(prompt);  // Free the prompt as it's no longer needed

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", GROQ_API_KEY);
    headers = curl_slist_append(headers, auth_header);

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, GROQ_API_URL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "Groq API request failed: %s\n", curl_easy_strerror(res));
            return 0;
        }

        // Parse the JSON response
        struct json_object *parsed_json = json_tokener_parse(chunk.memory);
        struct json_object *choices, *first_choice, *message, *content;
        
        if(json_object_object_get_ex(parsed_json, "choices", &choices) &&
           (first_choice = json_object_array_get_idx(choices, 0)) &&
           json_object_object_get_ex(first_choice, "message", &message) &&
           json_object_object_get_ex(message, "content", &content)) {
            
            strncpy(response_buffer, json_object_get_string(content), ANALYSIS_BUFFER_SIZE - 1);
            response_buffer[ANALYSIS_BUFFER_SIZE - 1] = '\0';  // Ensure null termination
            
            json_object_put(parsed_json);
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            free(chunk.memory);
            
            return 1;
        }
        
        json_object_put(parsed_json);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(chunk.memory);
    }
    
    return 0;
}

// Function to prepare the analysis prompt
char* prepare_groq_prompt(const char* symbol) {
    char* prompt = malloc(2048);  // Allocate memory for the prompt
    
    snprintf(prompt, 2048,
        "Analyze the financial health and investment potential of %s stock. "
        "Consider the following aspects:\\n"
        "1. Financial Performance: Current market performance, revenue growth, "
        "profitability metrics, and key financial ratios\\n"
        "2. Market Position: Competitive advantages, market share, and industry trends\\n"
        "3. Risk Assessment: Identify key risks, volatility analysis, and potential challenges\\n"
        "4. Future Outlook: Growth prospects, upcoming catalysts, and potential opportunities\\n"
        "5. Investment Recommendation: Provide a clear buy/hold/sell recommendation "
        "with supporting rationale\\n"
        "Please provide a concise, data-driven analysis with specific metrics "
        "and clear justification for your recommendations.",
        symbol);
    
    return prompt;
}

// Function to perform advanced analysis using Groq
void advanced_analysis_with_groq(const char* symbol) {
    printf("\n%sPerforming Advanced Analysis with Groq AI...%s\n", BLUE, RESET);
    
    char response_buffer[ANALYSIS_BUFFER_SIZE];
    if(perform_groq_analysis(symbol, response_buffer)) {
        printf("\n%sGroq AI Analysis Results:%s\n", BOLD, RESET);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("%s\n", response_buffer);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    } else {
        printf("%sFailed to perform Groq analysis%s\n", RED, RESET);
    }
}





/**
 * @brief Displays the main menu for the financial data viewer.
 */

// Modify the existing display_menu function to include Groq analysis option
void display_menu() {
    printf("\n");
    printf("=======================================================================\n");
    printf("| Financial Data Viewer |\n");
    printf("=======================================================================\n");
    printf("| %-20s | %-20s |\n", "Option", "Description");
    printf("=======================================================================\n");
    printf("| %-20s | %-20s |\n", "1", "Income Statement");
    printf("| %-20s | %-20s |\n", "2", "Balance Sheet");
    printf("| %-20s | %-20s |\n", "3", "Cash Flow Statement");
    printf("| %-20s | %-20s |\n", "4", "Key Metrics");
    printf("| %-20s | %-20s |\n", "5", "Ratios");
    printf("| %-20s | %-20s |\n", "6", "Growth Metrics");
    printf("| %-20s | %-20s |\n", "7", "Enterprise Values");
    printf("| %-20s | %-20s |\n", "8", "Basic Analysis");
    printf("| %-20s | %-20s |\n", "9", "GPT Analysis");
    printf("| %-20s | %-20s |\n", "10", "Groq Analysis");
    printf("| %-20s | %-20s |\n", "11", "Return to Stock Entry");
    printf("=======================================================================\n");
    printf("\nEnter your choice: ");
}


/**
 * @brief Main function to run the financial data viewer program.
 * 
 * This function initializes the CURL library, prompts the user for a stock symbol,
 * fetches stock data, and displays various analyses and financial data options.
 * The program continues until the user decides to quit.
 * 
 * @return 0 on successful execution.
 */









// Implementation of Auth0 authentication function
auth_session* perform_auth0_login(const char* username, const char* password) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    auth_session *session = NULL;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    // Correct URL format
    char url[512];
    snprintf(url, sizeof(url), "https://%s/oauth/token", AUTH0_DOMAIN);
    
    // Create JSON payload instead of form data
    char payload[1024];
    snprintf(payload, sizeof(payload),
        "{"
        "\"client_id\":\"%s\","
        "\"client_secret\":\"%s\","
        "\"username\":\"%s\","
        "\"password\":\"%s\","
        "\"grant_type\":\"password\","
        "\"audience\":\"https://%s/api/v2/\","
        "\"scope\":\"openid profile email\""
        "}",
        AUTH0_CLIENT_ID,
        AUTH0_CLIENT_SECRET,
        username,
        password,
        AUTH0_DOMAIN
    );
    
    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        res = curl_easy_perform(curl);
        if(res == CURLE_OK) {
            printf("Server response: %s\n", chunk.memory); // Add debug output
            
            struct json_object *parsed_json = json_tokener_parse(chunk.memory);
            if (parsed_json) {
                struct json_object *access_token_obj, *expires_in_obj;
                
                if (json_object_object_get_ex(parsed_json, "access_token", &access_token_obj)) {
                    session = malloc(sizeof(auth_session));
                    strncpy(session->access_token, 
                            json_object_get_string(access_token_obj),
                            sizeof(session->access_token) - 1);
                    session->access_token[sizeof(session->access_token) - 1] = '\0';
                    
                    if (json_object_object_get_ex(parsed_json, "expires_in", &expires_in_obj)) {
                        session->expires_at = time(NULL) + json_object_get_int(expires_in_obj);
                    } else {
                        session->expires_at = time(NULL) + 86400; // Default 24h expiry
                    }
                } else {
                    // Check for error message
                    struct json_object *error_obj, *error_description_obj;
                    if (json_object_object_get_ex(parsed_json, "error", &error_obj) &&
                        json_object_object_get_ex(parsed_json, "error_description", &error_description_obj)) {
                        fprintf(stderr, "Auth0 Error: %s - %s\n",
                                json_object_get_string(error_obj),
                                json_object_get_string(error_description_obj));
                    }
                }
                json_object_put(parsed_json);
            }
        } else {
            fprintf(stderr, "CURL Error: %s\n", curl_easy_strerror(res));
        }
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    free(chunk.memory);
    return session;
}








// Function to hide password input
void get_password(char* password, size_t size) {
    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    size_t i = 0;
    char c;
    while (i < size - 1 && (c = getchar()) != '\n') {
        password[i++] = c;
    }
    password[i] = '\0';

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    printf("\n");
}

// Function to verify if the session is still valid
bool verify_auth_session(auth_session *session) {
    if (!session) {
        return false;
    }
    
    if (strlen(session->access_token) == 0) {
        return false;
    }
    
    // Check if token has expired
    time_t current_time = time(NULL);
    if (current_time >= session->expires_at) {
        return false;
    }
    
    return true;
}

// Function to clean up auth session
void cleanup_auth_session(auth_session *session) {
    if (session != NULL) {
        memset(session->access_token, 0, sizeof(session->access_token));
        memset(session->user_id, 0, sizeof(session->user_id));
        session->expires_at = 0;
        free(session);
    }
}


int main() {
    char username[100];
    char password[100];
    auth_session* session = NULL;
    bool authenticated = false;
    
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_ALL);
    
    // Clear screen for login
    clear_screen();
    
    // Authentication loop
    while (!authenticated) {
        printf("\n%sFinancial Data Viewer - Login%s\n", BOLD, RESET);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Username: ");
        scanf("%s", username);
        printf("Password: ");
        get_password(password, sizeof(password));
        
        // Attempt authentication
        session = perform_auth0_login(username, password);
        if (session && verify_auth_session(session)) {
            authenticated = true;
            printf("\n%sLogin successful! Welcome to Financial Data Viewer%s\n", GREEN, RESET);
            sleep(2); // Brief pause to show success message
        } else {
            printf("\n%sLogin failed. Please check your credentials and try again.%s\n", RED, RESET);
            if (session) {
                cleanup_auth_session(session);
                session = NULL;
            }
            sleep(2); // Brief pause before retry
        }
        clear_screen();
    }

    
    
    // Main application loop
    char symbol[10];
    float prices[MAX_HOURS];
    char timestamps[MAX_HOURS][9];
    
    while(authenticated) {
        // Verify session is still valid
        if (!verify_auth_session(session)) {
            printf("\n%sYour session has expired. Please login again.%s\n", RED, RESET);
            cleanup_auth_session(session);
            return 1;
        }
        
        // Stock symbol input
        printf("\n%sFinancial Data Viewer%s\n", BOLD, RESET);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("%sLogged in as:%s %s\n", BLUE, RESET, username);
        printf("%sEnter stock symbol (or 'q' to quit):%s ", BOLD, RESET);
        scanf("%s", symbol);
        
        // Check for quit command
        if(strcmp(symbol, "q") == 0 || strcmp(symbol, "Q") == 0) {
            printf("\n%sThank you for using Financial Data Viewer!%s\n", GREEN, RESET);
            break;
        }

        // Convert symbol to uppercase
        for(int i = 0; symbol[i]; i++) {
            symbol[i] = toupper(symbol[i]);
        }

        // Fetch stock data
        printf("\n%sFetching data for %s...%s\n", BLUE, symbol, RESET);
        int num_points = fetch_stock_data(symbol, prices, timestamps);
        
        if(num_points > 0) {
            clear_screen();
            
            // Display basic information
            print_price_analysis(symbol, prices, num_points);
            draw_chart(prices, timestamps, num_points, symbol);
            print_historical_comparison(symbol, prices[num_points-1]);
            
            printf("\n\n");
            
            // Menu loop for current stock
            bool return_to_symbol = false;
            while(!return_to_symbol) {
                display_menu();
                int choice;
                scanf("%d", &choice);
                
                switch(choice) {
                    case 1: {
                        const char *income_columns[] = {"date", "revenue", "netIncome", "grossProfit"};
                        fetch_and_display(symbol, FMP_API_KEY, "income-statement", income_columns, 4, "Income Statement");
                        break;
                    }
                    case 2: {
                        const char *balance_columns[] = {"date", "totalAssets", "totalLiabilities", "totalStockholdersEquity"};
                        fetch_and_display(symbol, FMP_API_KEY, "balance-sheet-statement", balance_columns, 4, "Balance Sheet");
                        break;
                    }
                    case 3: {
                        const char *cashflow_columns[] = {"date", "netIncome", "dividendsPaid", "freeCashFlow"};
                        fetch_and_display(symbol, FMP_API_KEY, "cash-flow-statement", cashflow_columns, 4, "Cash Flow");
                        break;
                    }
                    case 4: {
                        const char *metrics_columns[] = {"date", "revenuePerShare", "peRatio", "debtToEquity"};
                        fetch_and_display(symbol, FMP_API_KEY, "key-metrics", metrics_columns, 4, "Key Metrics");
                        break;
                    }
                    case 5: {
                        const char *ratios_columns[] = {"date", "cashRatio", "currentRatio", "quickRatio"};
                        fetch_and_display(symbol, FMP_API_KEY, "ratios", ratios_columns, 4, "Financial Ratios");
                        break;
                    }
                    case 6: {
                        const char *growth_columns[] = {"date", "revenueGrowth", "grossProfitGrowth", "ebitgrowth", "epsgrowth"};
                        fetch_and_display(symbol, FMP_API_KEY, "financial-growth", growth_columns, 5, "Growth Metrics");
                        break;
                    }
                    case 7: {
                        const char *enterprise_columns[] = {"date", "enterpriseValue", "marketCapitalization", "debtToEnterpriseValue"};
                        fetch_and_display(symbol, FMP_API_KEY, "enterprise-values", enterprise_columns, 3, "Enterprise Values");
                        break;
                    }
                    case 8:
                        basic_analysis(symbol, prices, num_points, timestamps);
                        break;
                    case 9:
                        advanced_analysis(symbol);  // GPT Analysis
                        break;
                    case 10:
                        advanced_analysis_with_groq(symbol);  // New Groq Analysis
                        break;
                    case 11:
                        return_to_symbol = true;
                        break;
                    default:
                        printf("%sInvalid choice. Please try again.%s\n", RED, RESET);
                }
                
                if (!return_to_symbol) {
                    printf("\nPress Enter to continue...");
                    getchar(); // Consume newline
                    getchar(); // Wait for Enter
                    
                    // Refresh the display
                    clear_screen();
                    print_price_analysis(symbol, prices, num_points);
                    draw_chart(prices, timestamps, num_points, symbol);
                    print_historical_comparison(symbol, prices[num_points-1]);
                    printf("\n\n");
                }
            }
        } else {
            printf("%sFailed to fetch data for %s%s\n", RED, symbol, RESET);
            printf("Press Enter to continue...");
            getchar(); // Consume newline
            getchar(); // Wait for Enter
        }
        
        clear_screen();
    }
    
    // Cleanup
    if (session) {
        cleanup_auth_session(session);
    }
    curl_global_cleanup();
    return 0;
}
