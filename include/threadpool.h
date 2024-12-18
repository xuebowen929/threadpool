#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>


// 线程池支持的模式
enum class PoolMode
{
    MODE_FIXED, // 固定数量的线程
    MODE_CACHED, // 线程数量可动态增长
};

// Any类，可以接收任意数据类型
class Any
{
public:
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;


    template<typename T>
    Any(T date) : base_(std::make_unique<T>(date))
    {}

    template<typename T>
    T cast()
    {
        // 基类指针转化为派生类指针
        Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get()); // 拿到智能指针内的裸指针

        if(pd == nullptr)
        {
            throw "type is unmatch!";
        }
        else
        {
            return pd->date_;
        }
    }
private:
    // 基类类型
    class Base
    {
    public:
        virtual ~Base() = default;
    };
    // 派生类类型
    template<typename T>
    class Derive : public Base
    {
    public:
        Derive(T date) : date_(date);
        T date_;
    };

    std::unique_ptr<Base> base_;
};

// 线程信号量类
class Semaphore
{
public:
    Semaphore(int limit = 0) : resLimit_(0)
    {}
    ~Semaphore() = default;

    // 获取一个信号量资源
    void wait()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        // 等待信号量有资源,没有资源的话要阻塞线程
        cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
        resLimit_--;
    }

    // 增加一个信号量资源
    void post()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

// Task类提前声明
class Task;

// 返回值
class Result
{
public:
    Result(std::shared_ptr<Task> sp, bool isValid = true);
    ~Result() = default;

    // setVal方法, 获取任务执行完的返回值
    void setVal(Any any);
    // get方法, 用户调用这个方法获取task的返回值
    Any get();
private:
    Any any_;
    Semaphore sem_;
    std::shared_ptr<Task> task_; // 指向想要获取返回值的任务对象
    std::atomic_bool isValid_;
};

// 任务抽象基类
class Task
{
public:
    Task();
    ~Task() = default;
    void exec();
    void setResult(Result*);
    // 用户自定义任务类型，继承Task，重写run方法
    virtual Any run() = 0;

    // Result result_; // 不能在task中放result, 如果task析构了result也就没了
    Result* result_; // 不能用强智能指针, 否则Task对象和Result对象发生交叉引用, 资源永远无法释放
};

// 线程类型
class Thread
{
public:
    // 函数对象类型
    using ThreadFunc = std::function<void(int)>;
    // 构造
    Thread(ThreadFunc func);
    // 析构
    ~Thread();
    // 启动线程
    void start();
    // 获取线程id
    int getId();
private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_;
};

/*
exmple:

ThreadPool pool;
pool.start();

class MyTask : public Task
{
public:
    void run() { 线程代码... }
};

pool.submitTask(std::make_shared<MyTask>);
Result res = m1->result_->get();
*/

// 线程池类型
class ThreadPool
{
public:
    ThreadPool();

    ~ThreadPool();

    // 设置任务队列中任务数量上限阈值
    void setTaskQueMaxThreshHold(int threshhold);

    // 设置cached模式下线程数量上限阈值
    void setThreadSizeThreshHold(int threshhold);
    
    // 设置线程池工作模式
    void setMode(PoolMode mode);

    // 给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    // 开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());

    // 禁止对线程池对象进行拷贝/赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    void threadFunc(int threadId);
    bool chackRunningState() const;

    // std::vector<std::unique_ptr<Thread>> threads_; // 线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 线程列表
    int initThreadSize_; // 初始线程数量
    std::atomic_uint idleThreadSize_; // 空闲线程数量
    std::atomic_uint curThreadSize_; // 当前线程池中线程的总数量
    size_t threadSizeThreshHold_; // 线程数量上限阈值

    // 这里如果用裸指针Task*,万一用户传入一个临时任务Task(),出了submit语句就会析构,拿到一个已经析构的任务毫无意义
    // std::queue<Task*>
    std::queue<std::shared_ptr<Task>> taskQue_; // 任务队列

    // 线程池拿任务时--，用户放任务时++，考虑线程安全，用原子类型stomic
    std::atomic_uint taskSize_; // 任务队列中任务数量
    size_t taskQueMaxThreshHold_; // 任务队列中任务数量的上限阈值

    std::mutex taskQueMtx_; // 保证任务队列的线程安全
    std::condition_variable notEmpty_; // 表示任务队列不空
    std::condition_variable notFull_; // 表示任务队列不满
    std::condition_variable exitCond_; // 等待线程资源全部回收

    PoolMode poolMode_; // 当前线程池的工作模式

    std::atomic_bool isPoolRunning_; // 线程池的运行状态
};

#endif