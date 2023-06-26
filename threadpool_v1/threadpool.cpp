#include"threadpool.h"
#include<functional>
#include<iostream>
#include<thread>

const int TASK_MAX_THRESHOLD = 2;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10;      //��λs
//�̳߳ع���
ThreadPool::ThreadPool():initThreadSize_(0),taskSize_(0), idleThreadSize_(0),threadSizeThreshold_(THREAD_MAX_THRESHOLD),
taskQueMaxThresHold_(TASK_MAX_THRESHOLD),poolMode_(PoolMode::MODE_FIXED),isPoolRunning_(false), curThreadSize_(0)
{
}

//�̳߳�����
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	//�ȴ��̳߳�����������̷߳��� ���߳�������״̬������ & ִ������ing
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

//����task�������������ֵ
void ThreadPool::setTaskQueMaxThresHold(int threshold)
{
	if (checkRunningState())
		return;
	taskQueMaxThresHold_ = threshold;
}

//�����̳߳�cachedģʽ���߳���ֵ
void ThreadPool::setThreadSizeThresHold(int threshold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshold_ = threshold;
	}
}

//���̳߳��ύ����  �û����øýӿڣ��������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//�߳�ͨ�ţ��ȴ���������п��ࡣ�û��ύ��������ʱ�䲻�ܳ���1s�������ڷ��������ύʧ��
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

	//����п��࣬����������������
	taskQue_.emplace(sp);
	taskSize_++;

	//����������������в��գ���notEmpty_��֪ͨ, �����߳�ִ������
	notEmpty_.notify_all();

	//cachedģʽ������С�ȽϽ�����������С��������񡣸������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��߳�����
	if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshold_)
	{
		std::cout << ">>>> create new thread ... " << std::endl;
		//�������̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		//threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));

		threads_[threadId]->start();

		curThreadSize_++;
		idleThreadSize_++;
	}
	//���������result����
	return Result(sp);
}

//�����̳߳�
void ThreadPool::start(int initThreadSize) 
{
	//�����̳߳ص�����״̬
	isPoolRunning_ = true;

	//��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	//�����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		//����thread�̶߳����ʱ�򣬽��̺߳�������thread�̶߳���
		//threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc,this)));    //��ֹ�����ڴ�й¶��ʹ��unique_ptr
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		//threads_.emplace_back(std::move(ptr));                             //unique_ptrɾ����ֵ���ÿ������˴�ʹ��move�����ֵ����
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	//���������߳� std::vector<Thread*> threads_
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++;         //��¼��ʼ�����̵߳�����
	}
}

//�̺߳��� ���������������������
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();       //�߳��״�ִ��ʱ��

	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			//�Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << " ���Ի�ȡ����..." << std::endl;

			//cachedģʽ�������Ѿ������˺ܶ��̣߳����ǿ���ʱ�䳬��60s��Ӧ�ý�������̻߳���(���ճ���InitThreadSize_�������߳�Ҫ����)
			//��ǰʱ�� - �ϴ��߳�ִ��ʱ�� > 60s
				//ÿ�뷵��һ�� ��ô���֣���ʱ���أ������������ִ�з��أ�
			//�� + ˫���ж�
			while (taskQue_.size() == 0)
			{
				//�̳߳ؽ����������߳���Դ
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return;             //�̺߳����������߳̽���
				}

				if (poolMode_ == PoolMode::MODE_CACHED){
					//����������ʱ����
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
						{
							//��ʼ���յ�ǰ�߳�
							//��¼�߳���������ر���ֵ���޸�
							//���̶߳�����߳��б�������ɾ��
							//threadid -> thread���� -> ɾ��
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
					//�ȴ�notEmpty����
					notEmpty_.wait(lock);
				}
			}
			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id() << " ��ȡ����ɹ�..." << std::endl;

			//�����������һ���������,��֪ͨ
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			
			//���ȡ������������������֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}
			//֪ͨ������������
			notFull_.notify_all();
		}   //�ֲ�������������Զ���������ʱ�����ͷŵ�

		if (task != nullptr)
		{
			//��ǰ�̸߳���ִ���������,ͨ������ķ���ֵ��ʹ��setval����Result
			//task->run();
			task->exec();
		}

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();       //�����߳�ִ��������ʱ��
	}

}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

//--------------------------Thread�̷߳���ʵ��---------------------
int Thread::generateId_ = 0;
//�̹߳���
Thread::Thread(ThreadFunc func):func_(func),threadId_(generateId_++)
{
}

//�߳�����
Thread::~Thread()
{
}

//�����߳�
void Thread::start()
{
	//����һ���߳���ִ���̺߳���
	std::thread t(func_,threadId_);
	t.detach();       //���÷����߳�
}

int Thread::getId() const
{
	return threadId_;
}

//----------------------Task����ʵ��-------------------------------
Task::Task():result_(nullptr)
{
}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());  //�˴�����run()��̬����
	}
}
	
void Task::setResult(Result *res)
{
	result_ = res;
}

//-------------------Result������ʵ��--------------------
Result::Result(std::shared_ptr<Task> task, bool isvalid):task_(task), isvalid_(isvalid)
{
	task_->setResult(this);
}

//�û�����
Any Result::get()
{
	if (!isvalid_)
	{
		return "";
	}
	sem_.wait();          //�������û��ִ���꣬����������û����߳�
	return std::move(any_);
}

//
void Result::setVal(Any any)
{
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();      //�Ѿ���ȡ����ķ���ֵ�������ź�������Դ
}