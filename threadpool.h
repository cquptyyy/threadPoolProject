

//实现线程池相关的类
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <thread>
#include <chrono>


enum class ThreadPoolMode {
	MODE_FIXED,//线程数量固定
	MODE_CACHED,//线程数量动态变化
};


class Semaphore {
public:
	Semaphore(int limit=0)
	:rescLimit_(limit)
	,exit_(false)
	{}

	void post() {
		std::unique_lock<std::mutex> lock(mtx_);
		rescLimit_++;
		cv_.notify_all();
	}

	void wait() {
		if(exit_==false){
			std::unique_lock<std::mutex> lock(mtx_);
			cv_.wait(lock, [this]()->bool {return rescLimit_ > 0; });
	
			rescLimit_--;
		}
	}
	~Semaphore(){
		exit_=true;
	}
private:
	std::mutex mtx_;//保证线程互斥的互斥锁
	std::condition_variable cv_;//用于线程通信的条件变量
	int rescLimit_;//资源的引用计数
	bool exit_;
};



class Any {
public:
	template <typename T>
	Any(T data)
		:base_(std::make_unique<Derive<T>>(data))
	{}

	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template <typename T>
	T cast_() {
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		std::cout<<typeid(T).name()<<std::endl;
		if (pd == nullptr) {
			throw " type is incompatoble !!!";
		}
		return pd->data_;
	}
private:
	class Base {
	public:
		Base() = default;
		virtual ~Base() = default;
	};

	template <typename T>
	class Derive :public Base {
	public:
		Derive(T data)
			:data_(data) {}
		~Derive() = default;
		T data_;
	};

private:
	std::unique_ptr<Base> base_;
};






class Task;
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result()=default;
	Any get();//获取Any的返回值
	void setVal(Any any);//消费线程执行完任务的返回值

private:

	Any any_;
	Semaphore sem_;
	std::shared_ptr<Task> task_;
	std::atomic_bool isValid_;
};


class Task {
public:
	Task();
	~Task()=default;
	void exec();
	void setResult(Result* res);
	//虚函数重写接口 实现多态
	virtual Any run() = 0;
private:
	Result* result_;
};






//线程类
class Thread {
	//定义函数函数对象类型
	//使用function接受bind返回的线程函数对象
	using ThreadFunc = std::function<void(int)>;
public:
	//线程构造函数，将bind返回的线程函数对象初始化线程函数对象
	Thread(ThreadFunc func);
	//线程析构函数
	~Thread();
	//启动线程执行线程函数
	void start();
	//获取线程ID
	int getId()const;
private:
	//定义的线程函数对象
	ThreadFunc func_;
	int threadId_;
	static int generateId_;
};


//线程池类
class ThreadPool {
private:
	//为线程提供线程函数
	void threadFunc(int threadId);

	//检查线程池的运行状态
	bool checkRunningState();
public:
	//线程池构造
	ThreadPool();
	
	//线程池析构函数
	~ThreadPool();

	//为用户提供设置线程池工作模式的接口
	void setMode(ThreadPoolMode mode);

	//为用户提供设置任务队列最大阈值的接口，用户可以根据自己的硬件资源进行合理的设置
	void setTaskQueMaxThreshHold(int maxTaskHold);

	//cached 模式下 为用户提供设置线程数量最大阈值的接口，用户可以根据自己的硬件条件合理的设置
	void setThreadSizeMaxThreshHold(int maxThreadHold);

	//为用户提供开启线程池的接口
	//默认以cpu的核心数量初始线程的个数
	void start(int initThreadSize=std::thread::hardware_concurrency());

	//为用户提供提交线程任务的接口
	Result submitTask(std::shared_ptr<Task> sp);

	//用户不能进行拷贝和复制  因为互斥锁和条件变量不支持拷贝和复制
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator = (const ThreadPool&) = delete;
private:
	//需要通过线程Id找到对应的线程
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;

	//初始线程的数量  也就是vector中线程的初始数量
	int initThreadSize_;

	//线程数量的最大阈值,记录最多可以创建的线程数量
	int threadSizeMaxThreshHold_;

	//记录创建了的线程的数量
	std::atomic_int curThreadSize_;

	//记录空闲线程的数量
	std::atomic_int idleThreadSize_; 

	//用队列来存储任务
	std::queue<std::shared_ptr<Task>> taskQue_;

	//任务队列中任务的数量
	std::atomic_int taskSize_;

	//任务队列中任务数量的最大阈值,防止提交的任务过多将内存撑爆了
	int taskQueMaxThreshHold_;

	//任务队列是线程共享的，所以需要保证访问任务队列是线程安全的，使用互斥锁保证线程安全
	std::mutex taskQueMtx_;

	//为了实现线程间通信，需要使用条件变量和互斥锁 ，
	std::condition_variable notFull_;//任务队列不满
	std::condition_variable notEmpty_;//任务队列不空
	ThreadPoolMode poolMode_;//当前线程池的线程模式

	std::atomic_bool isPoolRunning_;//记录线程池是否启动，可能需要在多个线程中使用，所以使用atomic
	
	std::condition_variable exitCond_;//用于主线程与线程池线程通信所使用，当主线程想要调用Threadpool析构函数时使用
};




#endif
