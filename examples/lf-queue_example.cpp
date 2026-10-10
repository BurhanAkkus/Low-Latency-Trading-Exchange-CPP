#include "../common/thread-utils.h"
#include "../common/lf-queue.h"

using namespace Common;
struct MyStruct{
    int d_[3];
    public:
    MyStruct(int first,int second,int third):d_{first,second,third}
    {};
    MyStruct(std::initializer_list<int> list){
        std::copy(list.begin(), list.end(), d_);
    };
};

std::ostream& operator<<(std::ostream& os, const MyStruct& ms){
    os << "MyStruct{" << ms.d_[0] << ", " << ms.d_[1] << ", " << ms.d_[2] << "}";
    return os;
}

using MyQueue = LFQueue<MyStruct, 32>; // capacity must be a power of two.

auto ProducerFunction(MyQueue* q){
    for(int i = 0; i < 20; i++){
        auto w = q->getNextWriteTo();
        q->updateWriteIndex();
        *w = MyStruct{i,i*10,i*100};
        std::cout << "Wrote new MyStruct(" << *w << ") to: " << w << std::endl; 
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(0.1s);
    }
}   

auto ConsumerFunction(MyQueue* q){
    for(int i = 0; i < 20; i++){
        auto r = q->getNextReadTo();
        q->updateReadIndex();
        std::cout << "Read MyStruct(" << *r << ") at: " << r << std::endl;
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(0.5s);
    }
}

int main(){
    MyQueue q;
    auto writeThread = createAndStartThread(1,"ProduceThread", ProducerFunction,&q);
    auto readThread = createAndStartThread(2,"ConsumeThread", ConsumerFunction,&q);
    writeThread->join();
    readThread->join();
    std::cout << "MainExiting";
    return 0;
}