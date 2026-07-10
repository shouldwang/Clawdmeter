#pragma once
#include <Arduino.h>

#define MAX_STOCKS 5     // must match daemon/usage_core.py's MAX_STOCK_SYMBOLS
#define CHART_POINTS 24  // must match daemon/stock_quotes.py's CHART_POINTS

struct StockQuote {
    char symbol[10];   // display symbol, e.g. "TSLA", "0050" — daemon has
                        // already stripped any exchange prefix/suffix
    float price;
    float pct_change;  // today's % change, signed
    uint8_t chart[CHART_POINTS];  // today's session, pre-normalized 0-100 by
                                   // the daemon; only chart_points are valid
    uint8_t chart_points;         // 0 = no "ch" key this cycle (chart hidden)
};

struct UsageData {
    float session_pct;       // utilization 0-100 (5h window Pro/Max; spending % Enterprise)
    int session_reset_mins;  // minutes until reset
    float weekly_pct;        // 7-day utilization (Pro/Max only; 0 for Enterprise)
    int weekly_reset_mins;   // minutes until weekly reset (Pro/Max only)
    char status[16];         // "allowed", "limited", etc.
    char who[8];             // "Self"/"Work" (2-profile setups); empty when absent
    bool chime;              // play the session-reset chime; false unless daemon opts in
    bool enterprise;         // true = Enterprise spending-limit account
    int time_pct;            // 0-100: fraction of billing period elapsed (Enterprise)
    int period_days;         // total billing period length in days (Enterprise)
    char reset_date[12];     // formatted reset date e.g. "Jul 1" (Enterprise)
    long clock_epoch;        // local wall-clock epoch (s) from daemon; 0 = not provided
    int  clock_fmt;          // 12 or 24 (hour format from daemon); defaults to 24
    StockQuote stock[MAX_STOCKS];
    int stock_count;         // entries valid in stock[] this cycle; 0 = no "stock" key or empty array
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};
