#include "../include/threadpool.h"
#include <iostream>
#include <thread>

/*
class MyTask : public Task
{
public:
private:
    int begin_;
    int end_;
};

int main()
{
    ThreadPool pool;
    pool.start(4);
    pool.setMode(PoolMode::MODE_CACHED);

    Result res1 = pool.submitTask(std::make_shared<MyTask>(1,100000000));
    Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001,200000000));
    Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001,300000000));
    int sum1 = res1.get().cast_<long>();
    int sum2 = res2.get().cast_<long>();
    int sum3 = res3.get().cast_<long>();

    return 0;
}
*/