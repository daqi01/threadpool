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
	MODE_FIXED,                  //�̶��߳�����
	MODE_CACHED,                 //��̬�߳�����
};
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;       //�̺߳����������ͣ����ڰ��̳߳���Threeadpool���̺߳���
	Thread(ThreadFunc);                                //���캯������ʼ����������
	~Thread() = default;                               //��������
	void start();                                      //�����߳�-> �����߳���ִ���̺߳���
	int getId() const;                                 //��ȡ�߳�id

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

	void setMode(PoolMode);                                    //�����̳߳ع���ģʽ
	void setThreadMaxSize(int size);                           //�����߳���������
	void setTaskMaxSize(int size);                             //����������������

	void start(int initThreadSize = std::thread::hardware_concurrency());     //�����̳߳أ�Ĭ���߳�����ΪCPU������

	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&...args)->std::future<decltype(func(args...))>     //autoΪ���ص�future���ͣ�future���ܵ�������decltype�Ƶ�
	{
		using RType = decltype(func(args...));                           //�Զ��Ƶ�ִ�е��̺߳����ķ���ֵ���� 
		//���һ������task��Ϊ��������packaged_task
		auto task = std::make_shared<std::packaged_task<RType()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...)); 
		std::future<RType> result = task->get_future();

		std::unique_lock<std::mutex> lock(taskQuemtx_);                             //��ȡ��
		//����������С��������������ŷ���true������1�󣬷���flase���ύ����ʧ��
		if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() ->bool {return taskQue_.size() < taskMaxSize_; }))
		{
			std::cerr << "Task Queue is Full, Submit Task Fail ! " << std::endl;
			auto task = std::make_shared < std::packaged_task<RType()>>([]()->RType {return RType(); });     //����0
			(*task)(); 
			return task->get_future();
		}

		taskQue_.emplace([task]() {(*task)(); });    //taskQue_����Ϊ��������function���˴���packaged_task��Ϊһ������
		taskSize_++;
		notEmpty_.notify_all();               //֪ͨ�߳�ȡ����

		if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadMaxSize_)
		{
			std::cout << " >>> New Thread Created >>>" << std::endl;  //���cachedģʽ�£���ǰʣ�����������ڿ����߳������������߳�
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
	bool checkRunningstate() const;                            //�����̳߳�����״̬
	void threadFunc(int threadid);                             //�̺߳���
	
private:
	PoolMode poolMode_;                                                   //��ǰ�̳߳ع���ģʽ
	std::atomic_bool isPoolRunning_;                                      //��ǰ�̳߳�����״̬

	std::unordered_map<int, std::unique_ptr<Thread>> threads_;            //�߳��б���ϣmap��Ϊ �߳�Id <-> �̶߳���ָ��
	size_t initThreadSize_;                                               //��ʼ�߳�����
	int threadMaxSize_;                                                   //�߳���������
	std::atomic_int curThreadSize_;                                       //��ǰ�߳�����
	std::atomic_int idleThreadSize_;                                      //��ǰ�����߳�����

	using Task = std::function<void()>;                                   //��������Ϊ��������Task
	std::queue<Task> taskQue_;                                            //�������
	std::atomic_int taskSize_;                                            //��ǰ��������
	int taskMaxSize_;                                                      //������������

	std::mutex taskQuemtx_;                                               //������л�����
	std::condition_variable notFull_;                                      //������в���
	std::condition_variable notEmpty_;                                     //������в���
	std::condition_variable exitCond_;                                    //�ȴ���Դ����
};
#endif