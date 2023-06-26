#include"threadpool.h"
#include<functional>
#include<iostream>
#include<thread>

const int TASK_MAX_THRESHOLD = 2;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10;      //单位s
//线程池构造
ThreadPool::ThreadPool():initThreadSize_(0),taskSize_(0), idleThreadSize_(0),threadSizeThreshold_(THREAD_MAX_THRESHOLD),
taskQueMaxThresHold_(TASK_MAX_THRESHOLD),poolMode_(PoolMode::MODE_FIXED),isPoolRunning_(false), curThreadSize_(0)
{
}

//线程池析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	//等待线程池里面的所有线程返回 ，线程有两种状态：阻塞 & 执行任务ing
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

//设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThresHold(int threshold)
{
	if (checkRunningState())
		return;
	taskQueMaxThresHold_ = threshold;
}

//设置线程池cached模式下线程阈值
void ThreadPool::setThreadSizeThresHold(int threshold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshold_ = threshold;
	}
}

//给线程池提交任务  用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//线程通信，等待任务队列有空余。用户提交任务阻塞时间不能超过1s，否则在返回任务提交失败
	/*while (taskQue_.size() == taskQueMaxThresHold_)
	{
		notFull_.wait(lock);
	}*/
	//notFull_.wait(lock, [&]()->bool {return taskQue_.size() < taskQueMaxThresHold_; });
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{
		std::cerr << "task queue is full,submit task fail" << std::endl;
		return Result(sp,false);
	}

	//如果有空余，把任务放入任务队列
	taskQue_.emplace(sp);
	taskSize_++;

	//有了新任务，任务队列不空，在notEmpty_上通知, 分配线程执行任务
	notEmpty_.notify_all();

	//cached模式，任务小比较紧急，场景：小而块的任务。根据任务数量和空闲线程的数量，判断是否需要创建新的线程数来
	if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshold_)
	{
		std::cout << ">>>> create new thread ... " << std::endl;
		//创建新线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		//threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));

		threads_[threadId]->start();

		curThreadSize_++;
		idleThreadSize_++;
	}
	//返回任务的result对象
	return Result(sp);
}

//开启线程池
void ThreadPool::start(int initThreadSize) 
{
	//设置线程池的运行状态
	isPoolRunning_ = true;

	//记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	//创建线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		//创建thread线程对象的时候，将线程函数给到thread线程对象
		//threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc,this)));    //防止发生内存泄露，使用unique_ptr
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		//threads_.emplace_back(std::move(ptr));                             //unique_ptr删除左值引用拷贝，此处使用move后的右值引用
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	//启动所有线程 std::vector<Thread*> threads_
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++;         //记录初始空闲线程的数量
	}
}

//线程函数 从任务队列里面消费任务
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();       //线程首次执行时间

	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << " 尝试获取任务..." << std::endl;

			//cached模式，可能已经创建了很多线程，但是空闲时间超过60s，应该将多余的线程回收(回收超过InitThreadSize_数量的线程要回收)
			//当前时间 - 上次线程执行时间 > 60s
				//每秒返回一次 怎么区分：超时返回？还是有任务待执行返回？
			//锁 + 双重判断
			while (taskQue_.size() == 0)
			{
				//线程池结束，回收线程资源
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return;             //线程函数结束，线程结束
				}

				if (poolMode_ == PoolMode::MODE_CACHED){
					//条件变量超时返回
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
						{
							//开始回收当前线程
							//记录线程数量的相关变量值的修改
							//把线程对象从线程列表容器中删除
							//threadid -> thread对象 -> 删除
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
							return;
						}
					}
			    }
				else
				{
					//等待notEmpty条件
					notEmpty_.wait(lock);
				}
			}
			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id() << " 获取任务成功..." << std::endl;

			//从任务队列区一个任务出来,并通知
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			
			//如果取出任务后队列仍有任务，通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}
			//通知可以生产任务
			notFull_.notify_all();
		}   //局部对象出作用域自动析构，此时将锁释放掉

		if (task != nullptr)
		{
			//当前线程负责执行这个任务,通过任务的返回值，使用setval给到Result
			//task->run();
			task->exec();
		}

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();       //更新线程执行完任务时间
	}

}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

//--------------------------Thread线程方法实现---------------------
int Thread::generateId_ = 0;
//线程构造
Thread::Thread(ThreadFunc func):func_(func),threadId_(generateId_++)
{
}

//线程析构
Thread::~Thread()
{
}

//启动线程
void Thread::start()
{
	//创建一个线程来执行线程函数
	std::thread t(func_,threadId_);
	t.detach();       //设置分离线程
}

int Thread::getId() const
{
	return threadId_;
}

//----------------------Task方法实现-------------------------------
Task::Task():result_(nullptr)
{
}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());  //此处发生run()多态调用
	}
}
	
void Task::setResult(Result *res)
{
	result_ = res;
}

//-------------------Result方法的实现--------------------
Result::Result(std::shared_ptr<Task> task, bool isvalid):task_(task), isvalid_(isvalid)
{
	task_->setResult(this);
}

//用户调用
Any Result::get()
{
	if (!isvalid_)
	{
		return "";
	}
	sem_.wait();          //任务如果没有执行完，这里会阻塞用户的线程
	return std::move(any_);
}

//
void Result::setVal(Any any)
{
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();      //已经获取任务的返回值，增加信号量的资源
}