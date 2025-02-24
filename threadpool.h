//#pragma once 是vs下的防止头文件重复包含，在linux下不一定有用
//linux下使用ifndef  THREADPOOL_H #define THREADPOOL_H #endif  
//防止头文件重复包含
//SDK Software Development Kit 软件开发包
//OOP Object Oriented Programming 面向对象编程


//不带引用的智能指针
//auto_ptr有左值和右值的拷贝构造函数和赋值重载函数
//scope_ptr没有左值和右值的拷贝构造函数和赋值重载函数
//unique_ptr没有左值的拷贝构造函数和赋值构造函数，有右值的拷贝构造函数和赋值构造函数


//互斥锁
//mutx 没有左值和右值的拷贝构造函数和赋值重载函数  没有RAII机制，需要手动lock unlock
//unique_lock 没有左值和右值的拷贝构造函数和赋值重载函数，但是是RAII机制的，运行获取即初始化
//lock_guard 没有左值和右值的拷贝构造函数和赋值重载函数，但是是RAII机制的，运行获取即初始化












//example:
//ThreadPool pool;
//pool.start(4);
// 
//class MyTask:public Task{
//public:
//	//重写基类中的run方法
//	void run() {
//		//...
//	}
//};

//pool.submitTask(std::make_shared<MyTask>());












//实现线程池相关的类
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <thread>
#include <chrono>


//为了避免全局命名空间污染,在实际开发过程中不去使用using namespace std;
//使用 using namespace 会将指定的命名空间中的所有名称都引入当前的作用域，这可能导致名称冲突。








//c++ 提供了class+枚举类型 来防止有相同的枚举项在不同的枚举类型中出现，
// 当使用枚举项的时候不知道使用哪个枚举类型的下枚举项
//线程池的模式
enum class ThreadPoolMode {
	MODE_FIXED,//线程数量固定
	MODE_CACHED,//线程数量动态变化
};










//c++20提供了semaphore
//通过互斥锁和条件变量自定义实现一个信号量
class Semaphore {
public:
	//线程归还资源，资源计数++
	void post() {
		std::cout << "void post()" << std::endl;
		std::unique_lock<std::mutex> lock(mtx_);
		rescLimit_++;
		//通知在条件变量下等待获取资源的线程可以准备获取互斥锁，有资源了
		cv_.notify_all();
	}

	//线程获取资源，资源计数--
	void wait() {
		std::unique_lock<std::mutex> lock(mtx_);
		
		//判断是否有资源，没有资源则在条件变量下等待，等待归还资源的线程通知
		cv_.wait(lock, [this]()->bool {return rescLimit_ > 0; });

		rescLimit_--;
	}
private:
	std::mutex mtx_;//保证线程互斥的互斥锁
	std::condition_variable cv_;//用于线程通信的条件变量
	int rescLimit_;//资源的引用计数
};
















//实现Any类型接受任意类型的返回值
//将析构函数，构造函数实现为=default，编译代码指令会优化，表示为默认的析构函数，默认的构造函数
class Any {
public:
	
	//Any的构造函数接受任意类型的返回值
	//通过返回值的类型实例化出对应的派生类，数据存储在对应的派生类中
	//Any通过基类的指针来接受不同的派生类
	template <typename T>
	Any(T data)
		:base_(std::make_unique<Derive<T>>(data))
		//传入构造对象所需的参数，返回对象的堆指针
	{}


	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;


	//提供成员函数接口给用户获取返回值
	template <typename T>
	T cast_() {
		//将基类指针转化为派生类指针使用dynamic_cast;
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) {
			throw " type is incompatoble !!!";
		}
		return pd->data_;
	}
	//外部类可以访问内部类public,private的成员
	//内部类必须通过外部类的对象可以访问外部类的成员
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
//实现提交完任务返回的类
//通过该类对象可以获取到 任务执行完返回的Any ,从Any中获取返回值
//该类必须支持没有Any对象，任务没有执行完，阻塞获取返回值
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);

	Any get();//获取Any的返回值

	void setVal(Any any);//消费线程执行完任务的返回值

private:

	Any any_;//包含返回值的Any类型
	Semaphore sem_;//什么时候任务执行完，有返回值，线程通信使用的信号量，信号记录的就是带有返回值any对象的数量
	std::shared_ptr<Task> task_;//什么任务的返回值
	std::atomic_bool isValid_;//返回值是否有效
};








//线程池需要给用户提供上传任务的接口，但是用户的任务是多种多样的
//只能提供一个接口，如何接受不同的任务呢？通过多态来实现）
//在任务的抽象基类中声明纯虚函数，用户通过派生类重写纯虚函数，来实现自己想要线程池处理的任务
//线程池给用户提供传入任务的接口，该接口函数通过任务的基类指针接受用户的任务，从而实现多态

class Task {
public:
	Task();
	void exec();
	void setResult(Result* res);
	virtual Any run() = 0;
private:
	Result* result_;
	//不能使用智能指针，否则成交叉引用了
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
	//为什么线程函数不定义在Thread？要定义在ThreadPool中，
	// 因为线程访问访问的共享资源（任务队列）在ThreadPool中
	//以及保证线程安全和线程通信的互斥锁和条件变量在ThreadPool中
	//定义在ThreadPool中方便访问共享资源和使用互斥锁及条件变量
	//为线程提供线程函数，线程函数执行任务队列中的任务，线程通过线程函数消费任务
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
	void setTaskQueMaxThreshHold(int maxThreadHold);

	//cached 模式下 为用户提供设置线程数量最大阈值的接口，用户可以根据自己的硬件条件合理的设置
	void setThreadSizeMaxThreshHold(int threadhold);


	//为用户提供开启线程池的接口
	//线程开启，用户可以传入初始线程的数量来设置初始线程数量，用户可以根据硬件条件进行合理的设置
	//默认以cpu的核心数量初始线程的个数
	void start(int size=std::thread::hardware_concurrency());

	//为用户提供提交线程任务的接口
	Result submitTask(std::shared_ptr<Task> sp);


	//用户不能进行拷贝和复制  因为互斥锁和条件变量不支持拷贝和复制
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator = (const ThreadPool&) = delete;
private:
	//使用vector容器存储线程  用线程指针实例化vector，
	//vector析构会调用元素的析构函数，但是Thread*是指针类型，
	//没有析构函数，只能手动delete指针，调用指针指向对象的析构函数
	//为了可以自动delete指针，可以使用智能指针，vector析构，调用元素的析构函数
	// 元素就是智能指针，智能指针有析构函数，会在析构函数中delete封装的线程指针
	// vetor存储线程的智能指针
	//std::vector<std::unique_ptr<Thread>> threads_;
	//需要通过线程Id找到对应的线程
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;

	//初始线程的数量  也就是vector中线程的初始数量
	int initThreadSize_;


	//线程数量的最大阈值,记录最多可以创建的线程数量
	int threadSizeMaxThreshHold_;


	//记录创建了的线程的数量，也就是vector中线程的大小
	//但是vector不是线程安全，所以使用atomic_int记录空闲线程的大小
	std::atomic_int curThreadSize_;

	//记录空闲线程的数量,
	
	std::atomic_int idleThreadSize_; //在多线程下使用保证线程安全使用atomic


	//用队列来存储任务，用户提交任务接口是使用多态原理实现的，用任务的基类指针实例化queue，
	//但是使用裸基类指针不安全，如果用户提交的任务是临时对象，那么任务上传完任务对象就析构了
	//等到线程去处理任务时，发现任务指针是一个野指针，这是不合理的。
	//所以需要使用智能基类指针来实例化queue，queue中存储任务的智能基类指针
	std::queue<std::shared_ptr<Task>> tasks_;

	//任务队列中任务的数量，由于任务队列被用户线程访问也被线程池中的线程访问，所以任务队列是线程共享的，
	// 同理队列的大小也是线程共享的。为了保证线程安全，队列大小的修改应该是原子的，所以是使用atomic_int保证taskSize的原子性
	std::atomic_int taskSize_;

	//任务队列中任务数量的最大阈值,防止提交的任务过多将内存撑爆了
	int taskQueMaxThreshHold_;

	//任务队列是线程共享的，所以需要保证访问任务队列是线程安全的，使用互斥锁保证线程安全
	std::mutex taskQueMtx_;

	//为了实现线程间通信，需要使用条件变量和互斥锁 ，
	// 当任务队列没有任务时，消费任务的线程应该加入条件变量no，将锁释放，并进入等待状态
	std::condition_variable notFull_;//任务队列不满
	std::condition_variable notEmpty_;//任务队列不空
	ThreadPoolMode poolMode_;//当前线程池的线程模式

	std::atomic_bool isPoolRunning_;//记录线程池是否启动，可能需要在多个线程中使用，所以使用atomic
	
	std::condition_variable exitCond_;


};




















#endif

