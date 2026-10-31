#include "../src/queue_dispatch.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
int main(){try{
    std::mutex table;auto queue=std::make_shared<std::recursive_mutex>();
    std::promise<void> lookup_started,retirement_has_table;auto lookup=lookup_started.get_future();auto retiring=retirement_has_table.get_future();
    std::atomic<bool> acquired{false};
    std::thread retirement([&]{lookup.wait();std::lock_guard<std::mutex> lock(table);retirement_has_table.set_value();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(!queue->try_lock()){if(std::chrono::steady_clock::now()>=deadline)return;std::this_thread::yield();}
        acquired=true;queue->unlock();});
    {
        evrm::QueueDispatch present([&]{std::lock_guard<std::mutex> lock(table);return queue;},[&]{
            lookup_started.set_value();retiring.wait();std::lock_guard<std::mutex> lock(table);return 7u;});
        if(present.family!=7)throw std::runtime_error("family snapshot");
    }
    retirement.join();if(!acquired)throw std::runtime_error("queue/table inversion blocks completed old-swapchain retirement");
    std::cout<<"PASS concurrent presentation metadata and old-swapchain queue retirement\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
