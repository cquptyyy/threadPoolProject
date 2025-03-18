#include "threadpool.h"
//using uLong = unsigned long long;
class MyTask :public Task {
public:
	MyTask(int begin,int end)
		:begin_(begin),end_(end){}
	Any run() {
		
		std::this_thread::sleep_for(std::chrono::seconds(2));
		
		int sum = 0;
		for (int i = begin_; i <= end_; ++i)sum += i;
		std::cout << "thread-tid:" << std::this_thread::get_id() << " executed a task over" << std::endl;
		return sum;
	}
private: 
	int begin_;
	int end_;
};

int main() {
	try {
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
			std::cout << res.get().cast_<int>() << std::endl;
			//getchar();
		}
	} catch (const char* msg) {
			std::cerr << "Caught exception: " << msg << std::endl;
			// 进行适当的错误处理
	} catch (...) {
			std::cerr << "Caught unknown exception" << std::endl;
			// 处理所有其他类型的异常
	}

		
		getchar();

	return 0;
}
