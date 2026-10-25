#pragma once
#include <string>

// Thread-safe write of one line to stdout.
void send(const std::string& line);

void uci_loop();
void run_bench(int depth);
bool run_perft_suite(int max_depth);
