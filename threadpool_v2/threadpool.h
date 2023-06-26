#pragma once
#ifndef THREDA_H
#define THREAD_H

#include<iostream>
#include<queue>
#include<unordered_map>
#include<memory>
#include<functional>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<atomic>
#include<future>

enum class PoolMode
{
	MODE_FIXED,                  //固定线程数量
	MODE_CACHED,                 //动态线程数量
};
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;       //线程函数对象类型，用于绑定线程池类Threeadpool的线程函数
	Thread(ThreadFunc);                                //构造函数，初始化函数对象
	~Thread() = default;                               //析构函数
	void start();                                      //启动线程-> 创建线程来执行线程函数
	int getId() const;                                 //获取线程id

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};

class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	void setMode(PoolMode);                                    //设置线程池工作模式
	void setThreadMaxSize(int size);                           //设置线程数量上限
	void setTaskMaxSize(int size);                             //设置任务数量上限

	void start(int initThreadSize = std::thread::hardware_concurrency());     //开启线程池，默认线程数量为CPU核心数

	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&...args)->std::future<decltype(func(args...))>     //auto为返回的future类型，future接受的类型由decltype推导
	{
		using RType = decltype(func(args...));                           //自动推导执行的线程函数的返回值类型 
		//打包一个任务task，为函数对象packaged_task
		auto task = std::make_shared<std::packaged_task<RType()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...)); 
		std::future<RType> result = task->get_future();

		std::unique_lock<std::mutex> lock(taskQuemtx_);                             //获取锁
		//当任务数量小于最大任务数量才返回true，超过1后，返回flase，提交任务失败
		if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() ->bool {return taskQue_.size() < taskMaxSize_; }))
		{
			std::cerr << "Task Queue is Full, Submit Task Fail ! " << std::endl;
			auto task = std::make_shared < std::packaged_task<RType()>>([]()->RType {return RType(); });     //返回0
			(*task)(); 
			return task->get_future();
		}

		taskQue_.emplace([task]() {(*task)(); });    //taskQue_接受为函数对象function，此处将packaged_task作为一个函数
		taskSize_++;
		notEmpty_.notify_all();               //通知线程取任务

		if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadMaxSize_)
		{
			std::cout << " >>> New Thread Created >>>" << std::endl;  //如果cached模式下，当前剩余任务数大于空闲线程数，则新增线程
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));

			threads_[threadId]->start();
			curThreadSize_++;
			idleThreadSize_++;
		}
		return result;
	}

public:
	bool checkRunningstate() const;                            //设置线程池运行状态
	void threadFunc(int threadid);                             //线程函数
	
private:
	PoolMode poolMode_;                                                   //当前线程池工作模式
	std::atomic_bool isPoolRunning_;                                      //当前线程池启动状态

	std::unordered_map<int, std::unique_ptr<Thread>> threads_;            //线程列表，哈希map下为 线程Id <-> 线程对象指针
	size_t initThreadSize_;                                               //初始线程数量
	int threadMaxSize_;                                                   //线程数量上限
	std::atomic_int curThreadSize_;                                       //当前线程数量
	std::atomic_int idleThreadSize_;                                      //当前空闲线程数量

	using Task = std::function<void()>;                                   //任务类型为函数对象Task
	std::queue<Task> taskQue_;                                            //任务队列
	std::atomic_int taskSize_;                                            //当前任务数量
	int taskMaxSize_;                                                      //任务数量上限

	std::mutex taskQuemtx_;                                               //任务队列互斥锁
	std::condition_variable notFull_;                                      //任务队列不满
	std::condition_variable notEmpty_;                                     //任务队列不空
	std::condition_variable exitCond_;                                    //等待资源回收
};
#endif