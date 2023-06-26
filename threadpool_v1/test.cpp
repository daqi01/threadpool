#include"threadpool.h"
#include<chrono>
#include<thread>
#include<iostream>

using namespace std;

using ULong = unsigned long long;

class MyTask :public Task
{
public:
	MyTask(int begin, int end) :begin_(begin), end_(end)
	{
	}
	//问题一，怎么设计run函数的返回值，可以表示任意类型
	Any run()
	{
		cout << "tid: " << std::this_thread::get_id() << " begin!" << endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));

		ULong sum = 0;
		for (ULong i = begin_; i <= end_; i++)
		{
			sum += i;
		}
		cout << "tid: " << std::this_thread::get_id() << " end!" << endl;

		return sum;
	}
private:
	int begin_;  
	int end_;
};

int main()
{
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start();

		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		pool.submitTask(std::make_shared<MyTask>(1, 100000000));

		ULong sum = res1.get().cast_<ULong>();
		cout << "sum: " << sum << endl;
	}

	cout << "main over" << endl;
	getchar();

#if 0
	//出作用域pool自动析构，怎样将线程池相关的线程资源全部回收
	{
		ThreadPool pool;

		//用户设置线程池工作模式
		pool.setMode(PoolMode::MODE_CACHED);

		pool.start();

		Result res1 = pool.submitTask(make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(make_shared<MyTask>(200000001, 300000000));

		ULong sum1 = res1.get().cast_<ULong>();
		ULong sum2 = res2.get().cast_<ULong>();
		ULong sum3 = res3.get().cast_<ULong>();

		//Master - slave线程模型
		cout << "total sum = " << (sum1 + sum2 + sum3) << endl;
	}

	getchar();
#endif

	return 0;
}