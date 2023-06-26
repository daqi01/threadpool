#include "threadpool.h"

const int THREAD_MAX_SIZE = 100;                    //线程数量上限
const int TASK_MAX_SIZE = 2;                //任务数量上限
const int THREAD_MAX_IDLE_TIME = 10;                 //线程空闲时间（s）   

/*--------------------------- Thread -------------------------------*/

Thread::Thread(ThreadFunc func):func_(func),threadId_(generateId_++)     //构造函数，初始化函数对象
{
}

void Thread::start()                                                //启动线程-> 创建线程来执行线程函数
{
	std::thread t(func_, threadId_);
	t.detach();
} 
int Thread::getId() const                                            //获取线程id
{
	return threadId_;
}
int Thread::generateId_ = 0;
/*-------------------------- ThreadPool ------------------------------*/

ThreadPool::ThreadPool() :
	poolMode_(PoolMode::MODE_FIXED), isPoolRunning_(false),
	initThreadSize_(0), threadMaxSize_(THREAD_MAX_SIZE),
	curThreadSize_(0), idleThreadSize_(0),
	taskSize_(0), taskMaxSize_(TASK_MAX_SIZE)
{
}
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	std::unique_lock<std::mutex> lock(taskQuemtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });

}

bool ThreadPool::checkRunningstate() const
{
	return isPoolRunning_;
}
void ThreadPool::setMode(PoolMode mode)                               //设置线程池工作模式
{
	if (checkRunningstate())
		return;
	poolMode_ = mode;
}
void ThreadPool::setThreadMaxSize(int size)                           //设置线程数量上限
{
	if (checkRunningstate())
		return;
	if(poolMode_==PoolMode::MODE_CACHED)
		threadMaxSize_=size;
}
void ThreadPool::setTaskMaxSize(int size)                             //设置任务数量上限
{
	if (checkRunningstate())
		return;
	taskMaxSize_ = size;
}
void ThreadPool::start(int initThreadSize)     //开启线程池，默认线程数量为CPU核心数
{
	isPoolRunning_ = true;                                          //设置线程池运行状态为true
	initThreadSize_ = initThreadSize;                               //设置初始线程数量
	curThreadSize_ = initThreadSize;                                //设置当前线程数量

	for (int i = 0; i < initThreadSize_; i++)                       //创建initTreadSize_个线程对象
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}
	for (int i = 0; i < initThreadSize_; i++)                       //启动所有线程
	{
		threads_[i]->start();
		idleThreadSize_++;
	}

}

void ThreadPool::threadFunc(int threadid)                                       //线程函数
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	for (;;)
	{
		Task task;
		{
			std::unique_lock<std::mutex> lock(taskQuemtx_);
			std::cout << " Tid : " << std::this_thread::get_id() << " try to get a task ..." << std::endl;

			while (taskQue_.size() == 0)             
			{
				if (!isPoolRunning_)                        //当任务数量为0，且线程池回收时，空闲线程被主线程析构函数唤醒，自动删除线程
				{
					threads_.erase(threadid);
					std::cout << " Tid ：" << std::this_thread::get_id() << " exit ..." << std::endl;
					exitCond_.notify_all();
					return;
				}

				if (poolMode_ == PoolMode::MODE_CACHED) {                      //当cached模式下，线程超过执行时长未被任务唤醒时，会从线程池中删除
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) //大于10s没有执行任务，且线程池内的线程数量大于初始数量
						{
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << " Tid ：" << std::this_thread::get_id() << " exit ..." << std::endl;
							return;
						}
					}
				}
				else
				{
					notEmpty_.wait(lock);                  //任务队列为空，被阻塞，有任务时，被唤醒
				}
			}
			//任务队列任务数量大于0，直接取任务
			idleThreadSize_--;
			std::cout << " Tid : " << std::this_thread::get_id() << " get a task successfully..." << std::endl;

			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			if (taskQue_.size() > 0)      //取完任务后，还有剩余任务，通知其他线程取任务
				notEmpty_.notify_all();

			notFull_.notify_all();        //通知可以提交任务
		}
		if (task != nullptr)               //取出不为空，则执行任务
			task();

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();
	}
}