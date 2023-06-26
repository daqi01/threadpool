// threadpoolV3.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include"threadpool.h"
#include <iostream>
#include<chrono>
#include<string>
using namespace std;

int sum1(int a, int b)
{
	std::this_thread::sleep_for(std::chrono::seconds(10));
	return a + b;
}

int sum2(int a, int b, int c)
{
	std::this_thread::sleep_for(std::chrono::seconds(10));
	return a + b + c;
}
string concat(string a, string b)
{
	std::this_thread::sleep_for(std::chrono::seconds(10));
	return a + b;
}

int main()
{
	ThreadPool pool;
	pool.setMode(PoolMode::MODE_CACHED);
	pool.start(3);
	future<int> r1 = pool.submitTask(sum1, 10, 20);
	future<int> r2 = pool.submitTask(sum2, 10, 20, 30);

	future<int> r3 = pool.submitTask([](int a, int b)->int {
		int sum = 0;
		for (int i = a; i <= b; i++)
		{
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(10));
		return sum;
	}, 1, 100);
	future<int> r4 = pool.submitTask([](int a, int b)->int {
		int sum = 0;
		for (int i = a; i <= b; i++)
		{
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(10));
		return sum;
	}, 1, 100);
	future<int> r5 = pool.submitTask([](int a, int b)->int {
		int sum = 0;
		for (int i = a; i <= b; i++)
		{
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(10));
		return sum;
	}, 1, 100);
	future<string> r6 = pool.submitTask(concat, "helo", "wolrd");

	cout << r1.get() << endl;
	cout << r2.get() << endl;
	cout << r3.get() << endl;
	cout << r4.get() << endl;
	cout << r5.get() << endl;
	cout << r6.get() << endl;
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
