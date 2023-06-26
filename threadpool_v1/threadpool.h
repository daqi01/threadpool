#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<vector>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>

//Any���ͣ����Խ����������ݵ�����
class Any
{
public:
	Any() = default;
	~Any() = default;
	//base_û����ֵ�������죬ֻ����ֵ�������죬����Anyɾ����ֵ�������죬Ĭ����ֵ��������
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//ʹ�û���ָ��ָ��һ�������࣬������ָ���ɴ�������data�������,data�ǽ��ܵ�������������
	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data))
	{
	}

	//��������ܰ�Any��������Ĵ洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		//��base_�ҵ���ָ��Derive���󣬲�������ȡ��data��Ա����
		//����ָ��->������ָ��
		Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is mismatch!";
		}
		return pd->data_;
	}
private:
	//��������
	class Base
	{
	public:
		virtual ~Base() = default;

	};

	//������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) :data_(data)
		{
		}
		//����������������
		T data_;
	};

private:
	//����һ�������ָ��
	std::unique_ptr<Base> base_;
};

//ʵ��һ���ź�����
class Semaphore
{
public:
	Semaphore(int limit=0):resLimit_(limit)
	{
	}
	~Semaphore() = default;
	//��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ��û����Դ��������
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}
	//����һ���ź�����Դ
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

class Task;   //Task����ǰ��������result�л�ʹ�õ�
//ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result��
class Result
{
public:
	Result(std::shared_ptr<Task> task,bool isvalid=true);
	~Result() = default;
	  
	//����һ��setval��������ȡ����ִ����ķ���ֵ
	void setVal(Any any);
	//�������get�������û��������������ȡtask�ķ���ֵ
	Any get();

private:
	Any any_;          //�洢���񷵻�ֵ
	Semaphore sem_;      //�߳�ͨ���ź���
	std::shared_ptr<Task> task_;      //ָ���Ӧ��ȡ����ֵ�Ķ���
	std::atomic_bool isvalid_;        //����ֵ�Ƿ���Ч���Ƿ��ύ����ɹ�
};

//����������
class Task
{
public:
	Task();
	~Task() = default;

	void exec();
	void setResult(Result *res);
	//�û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;
private:
	Result *result_;
};

//�̳߳�֧�ֵ�ģʽ
enum class PoolMode
{
	MODE_FIXED,     //�̶��������߳�
	MODE_CACHED,     //�߳��������Զ�̬����
};

/* example
ThreadPool pool;
pool.start(4);
class MyTask : public Task
{
    public:
	void run(){....}
} 
pool.submit(std::make_shared<MyTask>());
*/

//�߳�����
class Thread
{
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	//�̹߳���
	Thread(ThreadFunc func);
	//�߳�����
	~Thread();

	//�����߳�
	void start();
	//��ȡ�߳�id
	int getId() const;

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;      //�����߳�Id

};

//�̳߳�����
class ThreadPool
{
public:
	//�̳߳ع���
	ThreadPool();
	
	//�̳߳�����
	~ThreadPool();

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	//����task�������������ֵ
	void setTaskQueMaxThresHold(int threshold);

	//�����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThresHold(int threshold);

	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	//�����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());     //����cpu��������

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�����̺߳���
	void threadFunc(int threadid);
	//���pool������״̬
	bool checkRunningState() const;

private:
	//std::vector<std::unique_ptr<Thread>> threads_;     //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;    //�߳��б�  int Ϊ�߳�id

	size_t initThreadSize_;         //��ʼ�߳�����
	int threadSizeThreshold_;         //�߳�����������ֵ
	std::atomic_int curThreadSize_;       //��¼��ǰ�̳߳������̵߳�������
	std::atomic_int idleThreadSize_;       //��¼�����߳�����

	std::queue<std::shared_ptr<Task>> taskQue_;      //�������
	std::atomic_int taskSize_;           //��������
	int taskQueMaxThresHold_;         // �����������������ֵ

	std::mutex taskQueMtx_;               //��֤��������̰߳�ȫ
	std::condition_variable notFull_;     //��ʾ������в���
	std::condition_variable notEmpty_;   //��ʾ������в���
	std::condition_variable exitCond_;    //�ȴ��߳���Դȫ������
	 
	PoolMode poolMode_;     //�̳߳ص�ǰ�Ĺ���ģʽ
	std::atomic_bool isPoolRunning_;     //��ʾ��ǰ�̳߳ص�����״̬
};

#endif