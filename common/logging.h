#pragma once

#include <iostream>
#include <fstream>
#include "lf-queue.h"
#include "thread-utils.h"
#include "constants.h"
namespace Common{
    enum class LOG_TYPE : int8_t {
        CHAR = 0, INTEGER = 1, LONG_INTEGER = 2, LONG_LONG_INTEGER = 3,
        UNSIGNED_INTEGER = 4, UNSIGNED_LONG_INTEGER = 5, UNSIGNED_LONG_LONG_INTEGER = 6,
        FLOAT = 7, DOUBLE = 8
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
        } u_;
    };

    class Logger final{
        std::ofstream os_;
        const std::string file_name_;
        LFQueue<LogElement> log_q_;
        std::atomic<bool> running_= {true};
        std::thread * logger_thread_ = nullptr;

        auto flushQueue() noexcept {
            while (running_) {
            for (auto next = log_q_.getNextReadTo();log_q_.size() && next; next = log_q_.getNextReadTo()) {
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
        
        auto pushValue(const LogElement& element) noexcept{
            *(log_q_.getNextWriteTo()) = element;
            log_q_.updateWriteIndex();
        }
        // single Char
        auto pushValue(const char value) noexcept{
            pushValue(LogElement{LOG_TYPE::CHAR,{.c=value}});
        }
        // ToDo
        // Improve efficiency, loops through every element..
        // string / char[]
        auto pushValue(const char* value) noexcept{
            while(*value){
                pushValue(*value);
                value++;
            }
        }
        // const str
        auto pushValue(const std::string& value) noexcept{
            pushValue(value.c_str());
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
        explicit Logger(const std::string& filename, int core_id = -1): file_name_{filename},log_q_{LOG_QUEUE_SIZE}{
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

        template<typename T,typename ...A>
        auto log(const char* s, const T& value, A...args) noexcept{
            while(*s){
                if(UNLIKELY(*s == '%')){
                    if(UNLIKELY(*(s+1) == '%')){
                        pushValue('%');
                        return log(s+2,args...);
                    }
                    else{
                        pushValue(value);
                        log(s+1,args...);
                        return;
                    }
                }
                else{
                    pushValue(*s++);
                }
            }
            FATAL("Too many arguments provided to log");
        }
        auto log(const char* s) noexcept{
            while(*s){
                if(UNLIKELY(*s == '%')){
                    if(UNLIKELY(*(s+1) != '%')){
                        FATAL("Too few arguments provided for Log");}
                    else{
                        s++;
                    }
                }
                pushValue(*s++);
            }
        }
    };
    
}