//线程池相关类成员函数的实现
#include "threadpool.h"
const int TASK_MAX_THRESH = 1024;
const int THREAD_MAX_THRESH = 10;
const int THREAD_MAX_IDLE_TIME = 60;//最大空闲时间60秒


//线程池的构造
//默认将初始线程数量设置为4，将任务数量设置为0，
//将任务队列的最大阈值设置为1024，线程池的工作模式默认为fixed
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
}


//设置线程池的工作模式
void ThreadPool::setMode(ThreadPoolMode mode) {
	//如果线程池处于运行状态不能设置状态
	if (checkRunningState())return;
	poolMode_ = mode;
}


//设置线程池中任务队列的最大阈值
void ThreadPool::setTaskQueMaxThreshHold(int maxThresh) {
	taskQueMaxThreshHold_ = maxThresh;
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
	//要访问共享资源任务队列首先获取互斥锁
	// 若队列不满，向任务队列中添加任务，生产任务
	//若队列已经满了，需要在条件变量下等待，将锁释放线程处于等待状态
	// 等待消费线程消费任务，等待通知队列不为满可以获取锁，再从等待状态变为阻塞状态获取互斥锁
	// 获取锁成功，线程进入运行状态，不成功继续阻塞等待锁释放获取互斥锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	/*while (tasks_.size() == taskQueMaxThreshHold_) {
		notFull_.wait(lock);
	}*/


	////也可以使用条件变量下wait的另一个重载函数
	//notFull_.wait(lock, [this]()->bool {
	//	return tasks_.size() < taskQueMaxThreshHold_;
	//	});




	//实现用户线程等待超过1s没有获取锁或任务队列为空，则用户线程不再阻塞等待
	//
	//条件变量下的 三个成员函数wait wait_for wait_util
	//wait 有两个重载函数，需要等待条件成立获取锁才返回，否则一直等待
	//wait_for 不仅需要等待条件成立获取锁，还需等待一段时间段，在该段时间内条件成立获取锁了就返回true 否则返回false
	//wait_util 不仅需要等待条件成立获取锁，还需等待到一个时间点，在未到时间点条件成立获取锁了就返回true，否则返回false

	bool ret=notFull_.wait_for(lock,
		std::chrono::seconds(1),
		[this]()->bool {return tasks_.size() < taskQueMaxThreshHold_; });
	//不满足条件或者没有获取锁
	if(ret==false){
		std::cerr << "taskQue is empty or not get lock!!!" << std::endl;
		return Result(sp,false);
	}




	//生产任务
	tasks_.emplace(sp);
	taskSize_++;
	//通知在等待任务队列不为空条件变量下的线程可以准备获取锁消费任务了
	notEmpty_.notify_all();




	//cached模式的使用场景：任务多而小
	//提交完任务，根据空闲线程的数量和任务数量，以及创建线程的数量是否到达线程数量的最大阈值是否需要创建新线程
	if (poolMode_ == ThreadPoolMode::MODE_CACHED
		&& idleThreadSize_ < taskSize_ 
		&& curThreadSize_ < threadSizeMaxThreshHold_) {
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId,std::move(ptr));
		threads_[threadId]->start();//启动线程
		curThreadSize_++;
		idleThreadSize_++;
		std::cout << "--------------------main thread create a new thread!!!!" << std::endl;
	}




	//返回的任务对应的Result对象	
	//方法一 Task中包含Result对象 return task->getResult();
	//方法二 Result中包含Task  return Result(task);
	//不能使用方法一因为，消费线程消费任务时，在方法一中的task会被从队列中pop掉，task会被析构掉，task中的Result自然也被析构掉
	return Result(sp);

}


//为线程提供的线程函数,线程函数执行任务队列中的任务，消费任务
void ThreadPool::threadFunc(int threadId) {
	////std::cout << "ThreadFunc start... tid=" << std::this_thread::get_id() <<std::endl;
	////
	////消费线程消费一个任务，执行完一个任务需要继续执行其他的任务，所以需要循环


	////获取线程的开始空闲时间点（没有执行任务的时间点）
	//auto lastTime = std::chrono::high_resolution_clock().now();
	//while(isPoolRunning_==true) {

	//	//访问共享资源任务队列需要获取互斥锁保证线程安全


	//	std::shared_ptr<Task> task;

	//	{

	//		


	//		std::unique_lock <std::mutex> lock(taskQueMtx_);
	//		std::cout << "thread-tid:" << std::this_thread::get_id() << "try to get a task." << std::endl;

	//		//线程每执行完一个任务，检查是否有超时的空闲线程需要销毁
	//		//cached模式下，创建的线程过多，导致有很多超过60s没有获取任务的线程
	//		//需要将这些线程销毁掉
	//		//当没有任务
	//		while (isPoolRunning_==true&&taskSize_ == 0) {
	//			//等待任务就绪获取锁，wait返回有两种结果
	//			//1.超时返回，每过1秒返回一次检查是否有超时空闲线程
	//			//2.有任务到来且获取到了锁
	//			if (poolMode_ == ThreadPoolMode::MODE_CACHED) {
	//				if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
	//					//超时返回
	//					auto now = std::chrono::high_resolution_clock().now();
	//					auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
	//					if (dur.count() > THREAD_MAX_IDLE_TIME
	//						&& curThreadSize_ > initThreadSize_) {
	//						//当前线程空闲超时，回收
	//						//将当前线程信息从线程列表中删除
	//						//修改线程数量有关的变量
	//						threads_.erase(threadId);
	//						curThreadSize_--;
	//						idleThreadSize_--;
	//						std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
	//						//线程函数执行线程就释放了
	//						return;

	//					}
	//				}
	//				//else {
	//				//	std::cout << "thread-tid:" << std::this_thread::get_id() << "get lock success." << std::endl;
	//				//	if (taskSize_ > 0) {
	//				//		std::cout << "thread-tid:" << std::this_thread::get_id() << "get lock success. go to exec task" << std::endl;
	//				//		break;
	//				//	}
	//				//}
	//				////else break;
	//			}
	//			else {
	//				//若任务队列不为空，进行执行队列，消费队列
	//				//若任务为空，需要在条件变量notEmpty下等待任务队列不为空，生产线程进行通知
	//				//条件变量下等待消费线程将锁释放，消费线程进入等待状态
	//				//生产线程生产了任务通知消费线程消费，消费线程由等待状态变为阻塞状态，阻塞状态可以获取互斥锁
	//				//若没有获取互斥锁则继续阻塞获取互斥锁，若获取互斥锁了，进入就绪状态，再到运行状态，向下消费任务

	//				notEmpty_.wait(lock);

	//			}
	//			//线程池回收释放资源
	//			/*if (isPoolRunning_ == false) {
	//				threads_.erase(threadId);
	//				curThreadSize_--;
	//				idleThreadSize_--;
	//				std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
	//				if (curThreadSize_ == 0)exitCond_.notify_all();
	//				return;
	//			}*/
	//			
	//		}
	//		
	//		if (isPoolRunning_ == false) {
	//			threads_.erase(threadId);
	//			curThreadSize_--;
	//			idleThreadSize_--;
	//			std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
	//			if (curThreadSize_ == 0)exitCond_.notify_all();
	//			return;
	//		}




	//		//else {
	//		//	//若任务队列不为空，进行执行队列，消费队列
	//		////若任务为空，需要在条件变量notEmpty下等待任务队列不为空，生产线程进行通知
	//		////条件变量下等待消费线程将锁释放，消费线程进入等待状态
	//		////生产线程生产了任务通知消费线程消费，消费线程由等待状态变为阻塞状态，阻塞状态可以获取互斥锁
	//		////若没有获取互斥锁则继续阻塞获取互斥锁，若获取互斥锁了，进入就绪状态，再到运行状态，向下消费任务

	//		//	notEmpty_.wait(lock,
	//		//		[this]()->bool {return tasks_.size() > 0; });

	//		//}







	//		
	//		//线程状态改变，将空闲线程的数量--
	//		idleThreadSize_--;



	//		//消费任务，将任务从队列中拿出来
	//		task = tasks_.front();
	//		tasks_.pop();
	//		taskSize_--;

	//		std::cout << "thread-tid:" << std::this_thread::get_id() << " get a task success." << std::endl;


	//		//任务消费，将任务从队列中取出来，发现还有任务队列中还有任务
	//		//通知等待在notEmpty条件变量下的等待任务队列不为空的消费线程，还有任务，准备获取互斥锁
	//		if (tasks_.size() > 0) {
	//			notEmpty_.notify_all();
	//		}


	//		//通知生产线程，任务队列不为空，可以准备获取锁，生产任务
	//		notFull_.notify_all();


	//		//出作用域将消费线程锁释放
	//	}

	//	//在出执行任务之前将互斥锁释放
	//	//为什么呢
	//	//因为互斥保证线程安全的访问任务队列，执行任务前已经将任务从队列中取出来
	//	//不再访问任务队列，没必要继续占着锁
	//	//原因2如果执行任务之前不把锁释放，在执行任务的过程中其他线程不能获取锁，不能从任务队列中将任务取出来
	//	//其他消费线程不能将任务取出来，肯定就不能执行任务。这样就只能一个消费线程在执行任务
	//	//也就是cpu中只有一消费线程在执行，这样如果cpu有多个核，cpu的效率太低，不符合高并发的目的




	//	//消费线程执行任务
	//	//根据基类指针指向的派生类任务，执行派生类任务
	//	if(task.get()!=nullptr)task->exec();
	//	std::cout << "thread exec over" << std::endl;


	//	//线程执行完任务将状态即将改变,空闲线程的数量++
	//	idleThreadSize_++;

	//	//更新
	//	if (isPoolRunning_ == false) {
	//		threads_.erase(threadId);
	//		curThreadSize_--;
	//		//idleThreadSize_--;
	//		std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
	//		if (curThreadSize_ == 0)exitCond_.notify_all();
	//		return;
	//	}
	//}
	//std::cout << "ThreadFunc start... tid=" << std::this_thread::get_id() <<std::endl;
	//
	//消费线程消费一个任务，执行完一个任务需要继续执行其他的任务，所以需要循环













/////////////////////////////////////////
//任务队列中的任务执行完再析构回收




	//获取线程的开始空闲时间点（没有执行任务的时间点）
	auto lastTime = std::chrono::high_resolution_clock().now();
	for(;;) {

		//访问共享资源任务队列需要获取互斥锁保证线程安全


		std::shared_ptr<Task> task;

		{

			


			std::unique_lock <std::mutex> lock(taskQueMtx_);
			std::cout << "thread-tid:" << std::this_thread::get_id() << "try to get a task." << std::endl;

			//线程每执行完一个任务，检查是否有超时的空闲线程需要销毁
			//cached模式下，创建的线程过多，导致有很多超过60s没有获取任务的线程
			//需要将这些线程销毁掉
			//当没有任务
			while (taskSize_ == 0) {
				if (isPoolRunning_ == false) {
					threads_.erase(threadId);
					curThreadSize_--;
					//idleThreadSize_--;
					std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
					if (curThreadSize_ == 0)exitCond_.notify_all();
					return;
				}
				//等待任务就绪获取锁，wait返回有两种结果
				//1.超时返回，每过1秒返回一次检查是否有超时空闲线程
				//2.有任务到来且获取到了锁
				if (poolMode_ == ThreadPoolMode::MODE_CACHED) {
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						//超时返回
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() > THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							//当前线程空闲超时，回收
							//将当前线程信息从线程列表中删除
							//修改线程数量有关的变量
							threads_.erase(threadId);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
							//线程函数执行线程就释放了
							return;

						}
					}
					//else {
					//	std::cout << "thread-tid:" << std::this_thread::get_id() << "get lock success." << std::endl;
					//	if (taskSize_ > 0) {
					//		std::cout << "thread-tid:" << std::this_thread::get_id() << "get lock success. go to exec task" << std::endl;
					//		break;
					//	}
					//}
					////else break;
				}
				else {
					//若任务队列不为空，进行执行队列，消费队列
					//若任务为空，需要在条件变量notEmpty下等待任务队列不为空，生产线程进行通知
					//条件变量下等待消费线程将锁释放，消费线程进入等待状态
					//生产线程生产了任务通知消费线程消费，消费线程由等待状态变为阻塞状态，阻塞状态可以获取互斥锁
					//若没有获取互斥锁则继续阻塞获取互斥锁，若获取互斥锁了，进入就绪状态，再到运行状态，向下消费任务

					notEmpty_.wait(lock);

				}
				//线程池回收释放资源
				/*if (isPoolRunning_ == false) {
					threads_.erase(threadId);
					curThreadSize_--;
					idleThreadSize_--;
					std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
					if (curThreadSize_ == 0)exitCond_.notify_all();
					return;
				}*/
				
			}
			
			/*if (isPoolRunning_ == false) {
				threads_.erase(threadId);
				curThreadSize_--;
				idleThreadSize_--;
				std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
				if (curThreadSize_ == 0)exitCond_.notify_all();
				return;
			}*/




			//else {
			//	//若任务队列不为空，进行执行队列，消费队列
			////若任务为空，需要在条件变量notEmpty下等待任务队列不为空，生产线程进行通知
			////条件变量下等待消费线程将锁释放，消费线程进入等待状态
			////生产线程生产了任务通知消费线程消费，消费线程由等待状态变为阻塞状态，阻塞状态可以获取互斥锁
			////若没有获取互斥锁则继续阻塞获取互斥锁，若获取互斥锁了，进入就绪状态，再到运行状态，向下消费任务

			//	notEmpty_.wait(lock,
			//		[this]()->bool {return tasks_.size() > 0; });

			//}







			
			//线程状态改变，将空闲线程的数量--
			idleThreadSize_--;



			//消费任务，将任务从队列中拿出来
			task = tasks_.front();
			tasks_.pop();
			taskSize_--;

			std::cout << "thread-tid:" << std::this_thread::get_id() << " get a task success." << std::endl;


			//任务消费，将任务从队列中取出来，发现还有任务队列中还有任务
			//通知等待在notEmpty条件变量下的等待任务队列不为空的消费线程，还有任务，准备获取互斥锁
			if (tasks_.size() > 0) {
				notEmpty_.notify_all();
			}


			//通知生产线程，任务队列不为空，可以准备获取锁，生产任务
			notFull_.notify_all();


			//出作用域将消费线程锁释放
		}

		//在出执行任务之前将互斥锁释放
		//为什么呢
		//因为互斥保证线程安全的访问任务队列，执行任务前已经将任务从队列中取出来
		//不再访问任务队列，没必要继续占着锁
		//原因2如果执行任务之前不把锁释放，在执行任务的过程中其他线程不能获取锁，不能从任务队列中将任务取出来
		//其他消费线程不能将任务取出来，肯定就不能执行任务。这样就只能一个消费线程在执行任务
		//也就是cpu中只有一消费线程在执行，这样如果cpu有多个核，cpu的效率太低，不符合高并发的目的




		//消费线程执行任务
		//根据基类指针指向的派生类任务，执行派生类任务
		if(task.get()!=nullptr)task->exec();
		std::cout << "thread exec over" << std::endl;


		//线程执行完任务将状态即将改变,空闲线程的数量++
		idleThreadSize_++;

		//更新
		//if (isPoolRunning_ == false) {
		//	threads_.erase(threadId);
		//	curThreadSize_--;
		//	//idleThreadSize_--;
		//	std::cout << "threadId:" << std::this_thread::get_id() << "exit!!!" << std::endl;
		//	if (curThreadSize_ == 0)exitCond_.notify_all();
		//	return;
		//}
	}
	
}


//开启线程池
//线程开启，用户可以传入初始线程的数量来设置初始线程数量，用户可以根据硬件条件进行合理的设置
void ThreadPool::start(int initThreadSize){
	//线程池启动了
	isPoolRunning_ = true;
	//记录初始线程的个数
	initThreadSize_ = initThreadSize;

	curThreadSize_ = initThreadSize_;

	//线程的创建
	//创建线程需要提供线程需要执行的线程函数
	//由于线程函数在ThreadPool中，调用线程函数需要ThreadPool对象或者指针
	//但是在Thread中没有ThreadPool对象和指针，那这么办呢
	//通过bind绑定器将TreadPool的this指针绑定到线程函数上
	//这样在Thread中就能调用在ThreadPool中的线程函数了
	//make_unique返回临时的unique_ptr对象
	//unique_ptr没有左值的拷贝构造函数
	//只有右值的拷贝构造函数
	//所以对ptr做move
	for (int i = 0; i < initThreadSize_; ++i) {
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
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






//关于线程成员函数的实现





//线程的构造函数
//用function接受线程函数对象

//静态成员变量初始化

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

//生成线程对象，去执行线程函数
//线程去执行线程函数
void Thread::start() {
	std::thread t(func_,threadId_);
	//防止线程对象出作用域析构，将线程对象与主线程分离
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
	sem_.wait();//task任务如果没有执行完，用户线程会在	这里阻塞住
	//any里面的unique_ptr<Base>没有左值的拷贝构造函数，所以需要使用move将左值转为右值进行右值引用拷贝函数，unique进行右值引用拷贝函数
	return std::move(any_);
}


void Result::setVal(Any any) {
	//消费线程执行完任务将封装了返回值的any对象返回
	this->any_ = std::move(any);
	//获取到获取到有返回值的any对象，对信号量++，
	//信号记录的就是返回值的数量
	sem_.post();
}





/////////////////////////////////////////////////////////////////////////////

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








//////////////////////////////////////////////////////////////////////////
// 应该有个对象和任务对象的提交关联起来，通过这个对象能获取任务的返回值
// 这个对象的类型需要实现一下，把这个对象的类型叫做Result
// 如何让Result对象与Task任务对象关联起来，可以在Task对象中定义Result对象
// 也可以在Result对象中定义Task对象，这两种方案都可以将结果对象和任务对象关联起来。
// 选择那种方案，且看下面的分析
// 前者通过Task对象获取结果对象，后者直接返回结果对象。
// 应选择在Result中定义Task，返回Result对象
// 如果在Task中定义Result,Task对象会在执行完任务后,将返回值存储在Result中后,就析构掉了，Task一析构完，成员对象Result就开始析构
// 外部的Result和Task可定是同一个Result对象或者是Task中Result对象的拷贝，
// 但是Task析构会将成员对象也析构，外部就不能访问已析构的对象。	
// 
//提交任务返回结果对象
// 结果类型应该嵌套在shared_ptr<Task>中还是shared_ptr<Task> 嵌套在结果类型中
// 
//由于返回值的类型不确定，所以需要将返回值封装成统一类型的对象进行返回
//若任务提交失败，通过结果对象获取返回值，不应该阻塞等待线程执行完任务返回结果，因为根本没有提交任务成功，
//通过结果对象应该立即返回空“”，如何标识提交任务没有有成功呢，通过结果对象中的isValid标识










///////////////////////////////////////////////////////////////////////
//关于cached模式的三个问题
// cached模式应用于任务多而小的场景下，多余任务时间长的线程应该使用fixed模式
//任务过多应该创建更多的新线程
//创建的线程过多，线程空闲时间超过60s将多余的线程回收掉
//对于空闲线程数量的处理
//在提交任务的接口函数中，根据空闲线程的数量和任务的数量，决定是否需要增加新的线程







/////////////////////////
//任务队列里面如果还有还有任务是等任务队列中的任务全部执行完成才结束？还是不执行剩下的任务了？

