#include "threadpool.h"

//不是所有的任务都不需要返回值的
//假设需要计算1到30000的he
//可以分为三个任务
//任务一：计算1到10000的和
//任务二：计算10001到20000的和
//任务三：计算20001到30000的和
//分别把三个任务交个线程池处理，将处理结果返回给主线程
//主线程将三个任务的返回值加起来
//所以需要每个任务处理完都需要返回值
//而且每种任务的返回值可能不一样，要怎么解决呢？
//通过模板是解决不了的，为了接受不同的任务需要使用多态
//多态中的基类虚函数，不能是模板函数
//编译器编译到基类时需要生成一个虚函数表，及对应的虚函数指针
//需要具体返回值类型，所以基类的虚函数不能是模板函数
//所以怎样设计任意类型的任务返回值呢
//再java，python中有Object是所有类型的基类，可以通过Object接受不同类型的返回值
//如果	C++中也有自己的Object就可以解决了 C++有类似Object的可以接受任意类型的类型
//那就是C++17提供的Any

using uLong = unsigned long long;
class MyTask :public Task {
public:
	MyTask(int begin,int end)
		:begin_(begin),end_(end){}
	Any run() {
		
		std::this_thread::sleep_for(std::chrono::seconds(2));
		
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; ++i)sum += i;
		std::cout << "thread-tid:" << std::this_thread::get_id() << " executed a task over" << std::endl;
		return sum;
	}
private: 
	int begin_;
	int end_;
};

int main() {
	{
		ThreadPool pool;
		pool.setMode(ThreadPoolMode::MODE_CACHED);
		pool.start(2);
		Result res = pool.submitTask(std::make_shared<MyTask>(1, 10000));
		Result res1 = pool.submitTask(std::make_shared<MyTask>(10001, 20000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(20001, 30000));
		pool.submitTask(std::make_shared<MyTask>(1, 10000));
		pool.submitTask(std::make_shared<MyTask>(10001, 20000));
		pool.submitTask(std::make_shared<MyTask>(20001, 30000));
		std::cout << "main over" << std::endl;
		std::cout << res.get().cast_<uLong>() << std::endl;
		//getchar();
	}
		
		getchar();

	return 0;
}
