#include "../include/threadpool.h"
#include <thread>
#include <iostream>

const size_t TASK_MAX_THRESHHOLD = INT32_MAX;
const size_t THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; // 单位:秒

//          ThreadPool方法的实现    
ThreadPool::ThreadPool() 
        : initThreadSize_(0)
        , taskSize_(0)
        , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
        , poolMode_(PoolMode::MODE_FIXED)
        , isPoolRunning_(false)
        , idleThreadSize_(0)
        , threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
        , curThreadSize_(0)
{}

ThreadPool::~ThreadPool() // ThreadPool对象析构后,要把线程池相关的线程资源全部回收
{
    isPoolRunning_ = false;
    notEmpty_.notify_all();

    // 
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0;});
}

// 设置任务队列中任务数量上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
    if(chackRunningState()) return;
    taskQueMaxThreshHold_ = threshhold;
}

// 设置cached模式下线程数量上限阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
    if(chackRunningState()) return;
    if(poolMode_ == PoolMode::MODE_CACHED)
    {
        threadSizeThreshHold_ = threshhold;
    }
}

// 设置线程池工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if(chackRunningState()) return;
    poolMode_ = mode;
}

// 给线程池提交任务  用户调用该接口,传入任务对象
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    // 线程通信,等待任务队列有空余 
    // 用户提交任务,最长不能阻塞超过1s,否则判断提交任务失败,返回  wait(一直等)   wait_for(+等待持续时间)   wait_until(+等待终止时间)
    if(notFull_.wait_for(lock, std::chrono::seconds(1), 
        [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_;}) == false)
    {
        // 表示notFull条件变量等待1s后条件依然不满足
        std::cerr << "task queue is full, submit task fail." << std::endl;
        return Result(sp, false);
    }

    // 如果可以放,把任务放进任务队列
    taskQue_.emplace(sp);
    taskSize_++;

    // 刚放进了任务,在notEmpty_上通知
    notEmpty_.notify_all();

    // 需要根据任务数量和空闲线程数量, 判断是否需要创建新的线程出来
    if(taskSize_ > idleThreadSize_
        && poolMode_ == PoolMode::MODE_CACHED
        && curThreadSize_ < threadSizeThreshHold_)
    {
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        // threads_.insert(std::move(ptr));
        threads_.insert({threadId, std::move(ptr)});
        // 启动线程
        threads_[threadId]->start();
        // 修改线程个数相关的参数
        curThreadSize_++;
        idleThreadSize_++;
    }

    // return Task.getResult();   // 随着task执行完,task对象被释放,依赖于task对象的getResult方法也被释放
    return Result(sp); // task的生命周期要一直到用户把Result拿走
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
    isPoolRunning_ = true;

    // 记录初始线程个数
    initThreadSize_ = initThreadSize;

    // 创建线程对象
    for(int i=0; i<initThreadSize_; i++)
    {
        // 线程池创建一个线程后肯定要执行线程函数(任务),执行什么线程函数是由线程池规定,而且线程函数所要访问的变量也都是在ThreadPool对象中,
        // 所以将线程函数定义在线程池对象中，创建线程时给他绑定线程函数
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        // threads_.emplace_back(std::move(ptr));
        threads_.emplace(threadId, std::move(ptr));
        curThreadSize_++;
    }

    // 启动所有线程
    for(int i=0; i<initThreadSize_; i++)
    {
        threads_[i]->start();
        idleThreadSize_++;
    }
}

// 定义线程函数  线程池中的线程从任务队列中拿任务
void ThreadPool::threadFunc(int threadId)
{
    while(isPoolRunning_)
    {
        auto lastTime = std::chrono::high_resolution_clock().now();
        std::shared_ptr<Task> task;
        {
            // 获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);
  
            while(taskQue_.size() == 0)
            {
                // cached模式下, 有可能已经创建了很多线程, 但空闲时间超过60s应该把多余的线程结束回收掉 
                if(poolMode_ == PoolMode::MODE_CACHED)
                {
                    // 每一秒返回一次, 区分超时返回和有任务待执行返回
                    // 条件变量超时返回
                    if(std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now-lastTime);
                        if(dur.count() >= THREAD_MAX_IDLE_TIME
                            && curThreadSize_ > initThreadSize_)
                        {
                            // 开始回收线程
                            // 1.修改记录线程数量相关变量的值
                            // 2.在线程列表容器中删除线程对象   无法确定threadFunc 对应的是哪个 thread =》建立映射关系
                            // threadId  《=》 thread
                            threads_.erase(threadId); // std::this_thread::get_id(); 这个是C++系统为线程生成的id
                            curThreadSize_--;
                            idleThreadSize_--;

                            std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl; 
                            return;
                        }
                    }
                }
                else
                {
                    // 等待notEmpty_条件
                    notEmpty_.wait(lock);
                }

                // 线程池要结束，回收线程资源
                if(isPoolRunning_ == false)
                {
                    threads_.erase(threadId);
                    std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl; 
                    return;
                }
            }
            idleThreadSize_--;

            // 从任务队列中取一个任务出来
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            // 取完如果还有任务,发送notEmpty_通知
            if(taskQue_.size() > 0)
            {
                notEmpty_.notify_all();
            }
            // 取完任务发送notFull_通知
            notFull_.notify_all();
        }
        // 当前线程执行该任务
        if(task != nullptr)
        {
            // 1.执行任务; 2.把任务执行的返回值用setVal给到Result
            // task->run(); // run方法是纯虚函数，所以重新写一个普通方法，在普通方法中调用run方法
            task->exec();
        }
        idleThreadSize_++;
        lastTime = std::chrono::high_resolution_clock().now();
    }

    threads_.erase(threadId);
    std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl; 
}

// 判断线程池运行状态
bool ThreadPool::chackRunningState() const
{
    return isPoolRunning_;
}


//          Task方法的实现
void Task::exec()
{
    if(result_ != nullptr)
    {
        result_->setVal(run());
    }
}

void Task::setResult(Result* res)
{
    result_ = res;
}

Task::Task() : result_(nullptr)
{}


//          Thread方法的实现
// 静态成员变量类外初始化
int Thread::generateId_ = 0;
// 构造
Thread::Thread(ThreadFunc func) 
    : func_(func)
    , threadId_(generateId_++)
{}
// 析构
Thread::~Thread()
{}

// 启动线程
void Thread::start()
{
    // 创建一个线程来执行线程函数
    std::thread t(func_, threadId_);
    t.detach(); // 分离线程对象和线程函数,出了作用域后线程对象会析构,分离后不影响线程函数的执行
}

// 获取线程id
int Thread::getId()
{
    return threadId_;
}

//          Result方法的实现
Result::Result(std::shared_ptr<Task> task, bool isValid = true) : task_(task), isValid_(isValid)
{
    task_->setResult(this);
}

// setVal方法, 获取任务执行完的返回值
void Result::setVal(Any any)
{
    any_ = std::move(any);
    sem_.post(); // 拿到任务执行完的返回值, 增加信号量
}

// get方法, 用户 调用这个方法获取task的返回值
Any Result::get()
{
    if(isValid_ == false)
    {
        return "";
    }
    // 任务如果还没执行完, 这里会阻塞, 任务执行完后post信号量, 这里收到后继续往下执行
    sem_.wait();
    return std::move(any_);
}