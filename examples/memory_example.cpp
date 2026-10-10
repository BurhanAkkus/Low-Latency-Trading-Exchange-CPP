#include "../common/memory-pool.h"
#include <algorithm>
#include <initializer_list>
#include <iostream>

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

int main(){
    using namespace Common;
    MemoryPool<double, 45> prim_pool;
    MemoryPool<MyStruct, 45> struct_pool;
    for(int i = 0; i < 50; i++){
        auto pret = prim_pool.allocate(i);
        auto sret = struct_pool.allocate({i,i+1,i+2});
        std::cout << "allocated prim element: " << *pret <<" at " << pret << std::endl;
        std::cout << "allocated struct element: " << *sret << " at "<< sret<< std::endl;
        if(i % 5 == 0){
            std::cout << "deallocating prim element: " << *pret << " at " << pret << std::endl;
            std::cout << "deallocating struct element: " << *sret << " at " << sret << std::endl;
            prim_pool.deallocate(pret);
            struct_pool.deallocate(sret);
        }
    }
    return 0;
}