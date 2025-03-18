	//线程池相关类成员函数的实现
	#include "threadpool.h"
	const int TASK_MAX_THRESH = 65535;
	const int THREAD_MAX_THRESH = 16;
	const int THREAD_MAX_IDLE_TIME = 60;


	//默认将初始线程数量设置为4，将任务数量设置为0，
	//将任务队列的最大阈值设置为65535，线程池的工作模式默认为fixed
	//线程池默认没有启动
	ThreadPool::ThreadPool()
		:initThreadSize_(4)
		, threadSizeMaxThreshHold_(THREAD_MAX_THRESH)
		, taskSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESH)
		, poolMode_(ThreadPoolMode::MODE_FIXED)
		, isPoolRunning_(false)
	{}


	//线程池的析构函数
	ThreadPool::~ThreadPool(){
		isPoolRunning_ = false;
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [this]()->bool {return curThreadSize_ == 0; });


		// //上面正确的最终版本
		// //下面为存在死锁的版本

		// isPoolRunning_ = false;
		// notEmpty_.notify_all();
		// std::unique_lock<std::mutex> lock(taskQueMtx_);
		// exitCond_.wait(lock, [this]()->bool {return curThreadSize_ == 0; });

	}


	//设置线程池的工作模式
	void ThreadPool::setMode(ThreadPoolMode mode) {
		//如果线程池处于运行状态不能设置状态
		if (checkRunningState())return;
		poolMode_ = mode;
	}


	//设置线程池中任务队列的最大阈值
	void ThreadPool::setTaskQueMaxThreshHold(int maxTaskHold) {
		if (checkRunningState())return;
		taskQueMaxThreshHold_ = maxTaskHold;
	}



	//设置线程池中线程数量的最大阈值
	void ThreadPool::setThreadSizeMaxThreshHold(int threadhold) {
		//线程池已经启动不允许设置线程数量的最大阈值
		if (checkRunningState())return;
		//线程模式为MODE_CACHED才能设置
		if(ThreadPoolMode::MODE_CACHED==poolMode_)threadSizeMaxThreshHold_ = threadhold;
	}


	//线程池对外提供上传任务的接口
	Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		bool ret=notFull_.wait_for(lock,
			std::chrono::seconds(1),
			[this]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; });
		//不满足条件或者没有获取锁
		if(ret==false){
			std::cerr << "taskQue is full or not get lock!!!" << std::endl;
			return Result(sp,false);
		}

		taskQue_.emplace(sp);
		taskSize_++;
		notEmpty_.notify_all();

		//提交完任务，根据空闲线程的数量和任务数量，
		//以及创建线程的数量是否到达线程数量的最大阈值是否需要创建新线程
		if (poolMode_ == ThreadPoolMode::MODE_CACHED
			&& idleThreadSize_ < taskSize_ 
			&& curThreadSize_ < threadSizeMaxThreshHold_) {

			std::unique_ptr<Thread> ptr = std::make_unique<Thread>
			(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
			
			int threadId = ptr->getId();
			threads_.emplace(threadId,std::move(ptr));
			threads_[threadId]->start();//启动线程
			curThreadSize_++;
			idleThreadSize_++;
			std::cout << "--------------------main thread create a new thread!!!!" << std::endl;
		}

		return Result(sp);
	}


	//为线程提供的线程函数,线程函数执行任务队列中的任务，消费任务
	void ThreadPool::threadFunc(int threadId) {
		//获取线程的开始空闲时间点（没有执行任务的时间点）
		auto lastTime = std::chrono::high_resolution_clock().now();

		while(isPoolRunning_==true) {
			std::shared_ptr<Task> task;
			{
				std::unique_lock <std::mutex> lock(taskQueMtx_);

				while (isPoolRunning_&&taskSize_ == 0) {


					if (poolMode_ == ThreadPoolMode::MODE_CACHED) {
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
							//等待一秒  超时返回 判断线程空闲时间是否超过60秒
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);

							if (dur.count() > THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
									//线程空闲时间超过60秒 回收线程  

								threads_.erase(threadId);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadId:" << std::this_thread::get_id()<< "exit!!!" << std::endl;
								return;
							}

						}
					}


					else {
						notEmpty_.wait(lock);
					}
				}
				//退出等待锁的循环  可能是获取了互斥锁 也可能是线程池析构 需要结束线程

				if (isPoolRunning_ == false) {
					break;
				}

				//线程状态改变，将空闲线程的数量--
				idleThreadSize_--;
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				std::cout << "thread-tid:" << std::this_thread::get_id() 
				<< " get a task success." << std::endl;

				if (taskQue_.size() > 0) {
					notEmpty_.notify_all();
				}
				notFull_.notify_all();
				//出作用域 释放互斥锁
			}

			//消费线程执行任务
			//根据基类指针指向的派生类任务，执行派生类任务
			if(task.get()!=nullptr)task->exec();

			//线程执行完任务将状态即将改变,空闲线程的数量++
			idleThreadSize_++;
			lastTime = std::chrono::high_resolution_clock().now();
		}
		//线程池在析构  需要结束线程

		threads_.erase(threadId);
		curThreadSize_--;
		std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
		exitCond_.notify_all();
		return;












		// //上面为最终正确版本
		// //下面为存在死锁版本


		// //获取线程的开始空闲时间点（没有执行任务的时间点）
		// auto lastTime = std::chrono::high_resolution_clock().now();

		// while(true) {
		// 	std::shared_ptr<Task> task;
		// 	{
		// 		std::unique_lock <std::mutex> lock(taskQueMtx_);

		// 		while (taskSize_ == 0) {


		// 			if (poolMode_ == ThreadPoolMode::MODE_CACHED) {
		// 				if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
		// 					//等待一秒  超时返回 判断线程空闲时间是否超过60秒
		// 					auto now = std::chrono::high_resolution_clock().now();
		// 					auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);

		// 					if (dur.count() > THREAD_MAX_IDLE_TIME
		// 						&& curThreadSize_ > initThreadSize_) {
		// 							//线程空闲时间超过60秒 回收线程  

		// 						threads_.erase(threadId);
		// 						curThreadSize_--;
		// 						idleThreadSize_--;
		// 						std::cout << "threadId:" << std::this_thread::get_id()<< "exit!!!" << std::endl;
		// 						return;
		// 					}

		// 				}
		// 			}


		// 			else {
		// 				notEmpty_.wait(lock);
		// 			}

		// 			if(isPoolRunning_==true){
		// 				threads_.erase(threadId);
		// 				curThreadSize_--;
		// 				std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
		// 				exitCond_.notify_all();
		// 				return;
		// 			}

		// 		}
		// 		//退出等待锁的循环  可能是获取了互斥锁 也可能是线程池析构 需要结束线程

		// 		//线程状态改变，将空闲线程的数量--
		// 		idleThreadSize_--;
		// 		task = taskQue_.front();
		// 		taskQue_.pop();
		// 		taskSize_--;

		// 		std::cout << "thread-tid:" << std::this_thread::get_id() 
		// 		<< " get a task success." << std::endl;

		// 		if (taskQue_.size() > 0) {
		// 			notEmpty_.notify_all();
		// 		}
		// 		notFull_.notify_all();
		// 		//出作用域 释放互斥锁
		// 	}

		// 	//消费线程执行任务
		// 	//根据基类指针指向的派生类任务，执行派生类任务
		// 	if(task.get()!=nullptr)task->exec();

		// 	//线程执行完任务将状态即将改变,空闲线程的数量++
		// 	idleThreadSize_++;
		// 	lastTime = std::chrono::high_resolution_clock().now();

		// 	if(isPoolRunning_==true){
		// 		threads_.erase(threadId);
		// 		curThreadSize_--;
		// 		std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
		// 		exitCond_.notify_all();
		// 		return;
		// 	}
		// }




	}


	//开启线程池
	//线程开启，用户可以传入初始线程的数量来设置初始线程数量，用户可以根据硬件条件进行合理的设置
	void ThreadPool::start(int initThreadSize){
		//线程池启动了
		isPoolRunning_ = true;
		//记录初始线程的个数
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize_;
		for (int i = 0; i < initThreadSize_; ++i) {
			std::unique_ptr<Thread> ptr = std::make_unique<Thread>
			(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
			//threads_.emplace_back(std::move(ptr));
			int threadId=ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}
		//启动线程,生成一个线程对象去执行线程函数
		for (int i = 0; i < initThreadSize_; ++i) {
			threads_[i]->start();
			//记录启动线程的数量
			idleThreadSize_++;

		}
	}



	//检查线程池的运行状态
	bool ThreadPool::checkRunningState() { return isPoolRunning_; }


	int Thread::generateId_ = 0;

	int Thread::getId()const {
		return threadId_;
	}

	Thread::Thread(ThreadFunc func)
		:func_(func)
		,threadId_(generateId_++)
	{}

	//线程的析构函数
	Thread::~Thread(){}

	void Thread::start() {
		std::thread t(func_,threadId_);
		t.detach();
	}


	//返回类型Result的方法的实现
	Result::Result(std::shared_ptr<Task> task, bool isValid)
		:task_(task), isValid_(isValid)
	{
		task_->setResult(this);
	}


	Any Result::get() {
		if (isValid_ == false) {
			return "";
		}
		sem_.wait();
		return std::move(any_);
	}


	void Result::setVal(Any any) {
		this->any_ = std::move(any);
		sem_.post();
	}

	//////////////////////////////////////////////

	Task::Task():result_(nullptr){}

	void Task::exec() {
		if (result_ != nullptr)
		{
			result_->setVal(run());
		}
	}


	void Task::setResult(Result* res) {
		result_ = res;
	}
