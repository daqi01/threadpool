#include "threadpool.h"

const int THREAD_MAX_SIZE = 100;                    //�߳���������
const int TASK_MAX_SIZE = 2;                //������������
const int THREAD_MAX_IDLE_TIME = 10;                 //�߳̿���ʱ�䣨s��   

/*--------------------------- Thread -------------------------------*/

Thread::Thread(ThreadFunc func):func_(func),threadId_(generateId_++)     //���캯������ʼ����������
{
}

void Thread::start()                                                //�����߳�-> �����߳���ִ���̺߳���
{
	std::thread t(func_, threadId_);
	t.detach();
} 
int Thread::getId() const                                            //��ȡ�߳�id
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
void ThreadPool::setMode(PoolMode mode)                               //�����̳߳ع���ģʽ
{
	if (checkRunningstate())
		return;
	poolMode_ = mode;
}
void ThreadPool::setThreadMaxSize(int size)                           //�����߳���������
{
	if (checkRunningstate())
		return;
	if(poolMode_==PoolMode::MODE_CACHED)
		threadMaxSize_=size;
}
void ThreadPool::setTaskMaxSize(int size)                             //����������������
{
	if (checkRunningstate())
		return;
	taskMaxSize_ = size;
}
void ThreadPool::start(int initThreadSize)     //�����̳߳أ�Ĭ���߳�����ΪCPU������
{
	isPoolRunning_ = true;                                          //�����̳߳�����״̬Ϊtrue
	initThreadSize_ = initThreadSize;                               //���ó�ʼ�߳�����
	curThreadSize_ = initThreadSize;                                //���õ�ǰ�߳�����

	for (int i = 0; i < initThreadSize_; i++)                       //����initTreadSize_���̶߳���
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}
	for (int i = 0; i < initThreadSize_; i++)                       //���������߳�
	{
		threads_[i]->start();
		idleThreadSize_++;
	}

}

void ThreadPool::threadFunc(int threadid)                                       //�̺߳���
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
				if (!isPoolRunning_)                        //����������Ϊ0�����̳߳ػ���ʱ�������̱߳����߳������������ѣ��Զ�ɾ���߳�
				{
					threads_.erase(threadid);
					std::cout << " Tid ��" << std::this_thread::get_id() << " exit ..." << std::endl;
					exitCond_.notify_all();
					return;
				}

				if (poolMode_ == PoolMode::MODE_CACHED) {                      //��cachedģʽ�£��̳߳���ִ��ʱ��δ��������ʱ������̳߳���ɾ��
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) //����10sû��ִ���������̳߳��ڵ��߳��������ڳ�ʼ����
						{
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << " Tid ��" << std::this_thread::get_id() << " exit ..." << std::endl;
							return;
						}
					}
				}
				else
				{
					notEmpty_.wait(lock);                  //�������Ϊ�գ���������������ʱ��������
				}
			}
			//�������������������0��ֱ��ȡ����
			idleThreadSize_--;
			std::cout << " Tid : " << std::this_thread::get_id() << " get a task successfully..." << std::endl;

			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			if (taskQue_.size() > 0)      //ȡ������󣬻���ʣ������֪ͨ�����߳�ȡ����
				notEmpty_.notify_all();

			notFull_.notify_all();        //֪ͨ�����ύ����
		}
		if (task != nullptr)               //ȡ����Ϊ�գ���ִ������
			task();

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();
	}
}