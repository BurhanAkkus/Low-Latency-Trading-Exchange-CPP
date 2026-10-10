#pragma once

#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include "lf-queue.h"
#include "thread-utils.h"
#include "constants.h"
namespace Common{
    enum class LOG_TYPE : int8_t {
        CHAR = 0, INTEGER = 1, LONG_INTEGER = 2, LONG_LONG_INTEGER = 3,
        UNSIGNED_INTEGER = 4, UNSIGNED_LONG_INTEGER = 5, UNSIGNED_LONG_LONG_INTEGER = 6,
        FLOAT = 7, DOUBLE = 8,
        STRING = 9 // up to 8 chars, null padded when shorter.
    };

    struct LogElement{
        LOG_TYPE type_ = LOG_TYPE::CHAR;
        union{
            char c;
            int i;
            long l;
            long long ll;
            unsigned int ui;
            unsigned long ul;
            unsigned long long ull;
            float f;
            double d;
            char s[8];
        } u_;
    };

    class Logger final{
        std::ofstream os_;
        const std::string file_name_;
        LFQueue<LogElement, LOG_QUEUE_SIZE> log_q_;
        std::atomic<bool> running_= {true};
        std::thread * logger_thread_ = nullptr;
        // Elements written by the current log() call but not published yet. Producer only.
        size_t pending_ = 0;

        auto flushQueue() noexcept {
            while (running_) {
            for (auto next = log_q_.getNextReadTo(); next; next = log_q_.getNextReadTo()) {
                switch (next->type_) {
                case LOG_TYPE::CHAR: os_ << next->u_.c; break;
                case LOG_TYPE::INTEGER: os_ << next->u_.i; break;
                case LOG_TYPE::LONG_INTEGER: os_ << next->u_.l; break;
                case LOG_TYPE::LONG_LONG_INTEGER: os_ << next->u_.ll; break;
                case LOG_TYPE::UNSIGNED_INTEGER: os_ << next->u_.ui; break;
                case LOG_TYPE::UNSIGNED_LONG_INTEGER: os_ <<next->u_.ul; break;
                case LOG_TYPE::UNSIGNED_LONG_LONG_INTEGER: os_<< next->u_.ull; break;
                case LOG_TYPE::FLOAT: os_ << next->u_.f; break;
                case LOG_TYPE::DOUBLE: os_ << next->u_.d; break;
                case LOG_TYPE::STRING: os_.write(next->u_.s, strnlen(next->u_.s, sizeof(next->u_.s))); break;
                }
                log_q_.updateReadIndex();
            }
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(1ms);
            }
        }

        // Delete Defaults
        Logger() = delete;// Default ctor
        Logger(const Logger&) = delete;// Copy ctor
        Logger(const Logger&&) = delete;// Move ctor
        Logger& operator=(const Logger&) = delete;// copy assignment
        Logger& operator=(const Logger&&) = delete;// move assignment
        
        // Writes the element without publishing it, log() publishes the whole line at once.
        auto pushValue(const LogElement& element) noexcept{
            *(log_q_.getNextWriteTo(pending_++)) = element;
        }
        // single Char
        auto pushValue(const char value) noexcept{
            pushValue(LogElement{LOG_TYPE::CHAR,{.c=value}});
        }
        // n chars, 8 per element.
        auto pushChars(const char* value, size_t n) noexcept{
            while(n){
                LogElement element{LOG_TYPE::STRING, {.s = {}}};
                const auto chunk = std::min(n, sizeof(element.u_.s));
                std::memcpy(element.u_.s, value, chunk);
                pushValue(element);
                value += chunk;
                n -= chunk;
            }
        }
        // string / char[]
        auto pushValue(const char* value) noexcept{
            pushChars(value, std::strlen(value));
        }
        // const str
        auto pushValue(const std::string& value) noexcept{
            pushChars(value.data(), value.size());
        }

        // int
        auto pushValue(const int value) noexcept{
            pushValue(LogElement{LOG_TYPE::INTEGER,{.i=value}});
        }
        // long
        auto pushValue(const long value) noexcept{
            pushValue(LogElement{LOG_TYPE::LONG_INTEGER,{.l=value}});
        }
        
        // long
        auto pushValue(const long long value) noexcept{
            pushValue(LogElement{LOG_TYPE::LONG_LONG_INTEGER,{.ll=value}});
        }

        // unsigned int
        auto pushValue(const unsigned int value) noexcept{
            pushValue(LogElement{LOG_TYPE::UNSIGNED_INTEGER,{.ui=value}});
        }

        // unsigned long
        auto pushValue(const unsigned long value) noexcept{
            pushValue(LogElement{LOG_TYPE::UNSIGNED_LONG_INTEGER,{.ul=value}});
        }

        // unsigned long long
        auto pushValue(const unsigned long long value) noexcept{
            pushValue(LogElement{LOG_TYPE::UNSIGNED_LONG_LONG_INTEGER,{.ull=value}});
        }

        // float
        auto pushValue(const float value) noexcept{
            pushValue(LogElement{LOG_TYPE::FLOAT,{.f=value}});
        }

        // double
        auto pushValue(const double value) noexcept{
            pushValue(LogElement{LOG_TYPE::DOUBLE,{.d=value}});
        }
        public:
        // explicit to block unwanted conversions.
        // core_id pins the logger thread to a core, -1 leaves it to the OS.
        explicit Logger(const std::string& filename, int core_id = -1): file_name_{filename}{
            os_.open(file_name_);
            ASSERT(os_.is_open(), "Log File " + file_name_ +  " couldn't be opened!");
            logger_thread_ = createAndStartThread(core_id,"CommonLogger", [this](){flushQueue();});
            ASSERT(logger_thread_ != nullptr, "Logger thread couldn't start!");
        }

        ~Logger(){
            std::cout << "Flushing and Closing logging thread: " + file_name_<< std::endl;
            while(log_q_.size() > 0){
                using namespace std::literals::chrono_literals;
                std::this_thread::sleep_for(1ms);
            };
            running_ = false;
            logger_thread_->join();
            delete logger_thread_;
            os_.close();
        }

        // '%' is replaced by the next argument, "%%" is a literal '%'.
        template<typename ...A>
        auto log(const char* s, const A&... args) noexcept{
            format(s, args...);
            log_q_.updateWriteIndex(pending_);
            pending_ = 0;
        }

        private:
        // Pushes the text up to the next '%' or the end, returns where it stopped.
        auto pushLiteral(const char* s) noexcept{
            auto end = s;
            while(*end && *end != '%') end++;
            pushChars(s, end - s);
            return end;
        }
        template<typename T,typename ...A>
        auto format(const char* s, const T& value, const A&... args) noexcept -> void{
            s = pushLiteral(s);
            if(UNLIKELY(!*s)) FATAL("Too many arguments provided to log");
            if(UNLIKELY(*(s+1) == '%')){
                pushValue('%');
                format(s+2, value, args...);
                return;
            }
            pushValue(value);
            format(s+1, args...);
        }
        auto format(const char* s) noexcept -> void{
            while(*(s = pushLiteral(s))){
                if(UNLIKELY(*(s+1) != '%')) FATAL("Too few arguments provided for Log");
                pushValue('%');
                s += 2;
            }
        }
    };
    
}