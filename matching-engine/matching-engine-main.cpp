#include <csignal>
#include "matcher/matching-engine.h"

Common::Logger* logger = nullptr;
Exchange::MatchingEngine* matching_engine = nullptr;
// Only async-signal-safe work is allowed in a signal handler,
// so it just raises a flag and main() does the shutdown.
volatile std::sig_atomic_t shutdown_requested = 0;
void signal_handler(int) {
    shutdown_requested = 1;
}

int main(int, char **) {
    logger = new Common::Logger("exchange_main.log");
    std::signal(SIGINT, signal_handler);
    const int sleep_time = 100 * 1000;
    Exchange::ClientRequestLFQueue
    client_requests(ME_MAX_CLIENT_UPDATES);
    Exchange::ClientResponseLFQueue
    client_responses(ME_MAX_CLIENT_UPDATES);
    Exchange::MEMarketUpdateLFQueue
    market_updates(ME_MAX_MARKET_UPDATES);

    //ToDo
    // engine owns the queues so main can exit.
    matching_engine = new Exchange::MatchingEngine{
        &client_requests,
        &client_responses,
        &market_updates};
    std::string time_str;
        logger->log("%:% %() % Starting Matching Engine...\n",
            __FILE__, __LINE__, __FUNCTION__,
            Common::getCurrentTimeStr(&time_str));
    matching_engine->start();
    // Heartbeat
    // usleep returns early when SIGINT arrives, so shutdown is not delayed by the sleep.
    while (!shutdown_requested) {
        logger->log("%:% %() % Sleeping for a few milliseconds..\n",
             __FILE__, __LINE__, __FUNCTION__,
            Common::getCurrentTimeStr(&time_str));
        usleep(sleep_time * 1000);
    }
    // Destructor stops the engine thread and joins it before freeing the order books.
    delete matching_engine; matching_engine = nullptr;
    delete logger; logger = nullptr;
    return EXIT_SUCCESS;
}