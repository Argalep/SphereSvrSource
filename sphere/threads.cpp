// this thing is somehow required to be able to initialise OLE
#define _WIN32_DCOM

#include "../common/common.h"
#include "../common/CException.h"
#include "../graysvr/graysvr.h"
#include "threads.h"
// #include "mutex.h"

// number of exceptions after which we restart thread and think that the thread have gone in exceptioning loops
#define EXCEPTIONS_ALLOWED	10

// Normal Buffer
// SimpleMutex g_tmpStringMutex;
volatile long g_tmpStringIndex = 0;
char g_tmpStrings[THREAD_TSTRING_STORAGE][THREAD_STRING_LENGTH];

// TemporaryString Buffer
// SimpleMutex g_tmpTemporaryStringMutex;
volatile long g_tmpTemporaryStringIndex = 0;
char g_tmpTemporaryStringUsed[THREAD_STRING_STORAGE];
char g_tmpTemporaryStrings[THREAD_STRING_STORAGE][THREAD_STRING_LENGTH];


/**
 * ThreadHolder
 * NOTE: due to it is difficult to create a good sync on this level, instead of storying a list of threads
 * we store an array and mark records being removed, which is absolutely thread-safe
**/
IThread *ThreadHolder::m_threads[MAX_THREADS];
int ThreadHolder::m_threadCount = 0;
bool ThreadHolder::m_inited = false;

IThread *ThreadHolder::current()
{
	init();
#ifdef _WIN32
	unsigned id = ::GetCurrentThreadId();
#else
	unsigned id = (unsigned)pthread_self();
#endif


	for( int i = 0; i < MAX_THREADS; i++ )
	{
		if( m_threads[i] != NULL )
		{
			if( m_threads[i]->getId() == id )
			{
				return m_threads[i];
			}
		}
	}

	return DummySphereThread::getInstance();
}

void ThreadHolder::push(IThread *thread)
{
	init();
	if( m_threadCount >= MAX_THREADS-1 )
	{
		throw new CException(LOGL_FATAL, 0, "Too many opened threads");
	}

	for( int i = 0; i < MAX_THREADS; i++ )
	{
		if( m_threads[i] == NULL )
		{
			m_threads[i] = thread;
			m_threadCount++;
			return;
		}
	}

	throw new CException(LOGL_FATAL, 0, "Unable to find an empty slot for thread");
}

void ThreadHolder::pop(IThread *thread)
{
	init();
	if( m_threadCount <= 0 )
	{
		throw new CException(LOGL_ERROR, 0, "Trying to dequeue thread while no threads are active");
	}

	for( int i = 0; i < MAX_THREADS; i++ )
	{
		if( m_threads[i] != NULL )
		{
			if( m_threads[i]->getId() == thread->getId() )
			{
				m_threads[i] = NULL;
				m_threadCount--;
				return;
			}
		}
	}

	throw new CException(LOGL_ERROR, 0, "Unable to dequeue a thread (not registered)");
}

int ThreadHolder::getActiveThreads()
{
	return m_threadCount;
}

void ThreadHolder::init()
{
	if( !m_inited )
	{
		for( int i = 0; i < MAX_THREADS; i++ )
		{
			m_threads[i] = NULL;
		}

		memset(g_tmpStrings, 0, sizeof(g_tmpStrings));
		memset(g_tmpTemporaryStringUsed, 0, sizeof(g_tmpTemporaryStringUsed));
		memset(g_tmpTemporaryStrings, 0, sizeof(g_tmpTemporaryStrings));

		m_inited = true;
	}
}

/*
 * AbstractThread
*/
int AbstractThread::m_threadsAvailable = 0;

AbstractThread::AbstractThread(const char *name, IThread::Priority priority)
{
	if( AbstractThread::m_threadsAvailable == 0 )
	{
		// no threads were started before - initialise thread subsystem
#ifdef _WIN32
		if( CoInitializeEx(NULL, COINIT_MULTITHREADED) != S_OK )
		{
			throw new CException(LOGL_FATAL, 0, "OLE is not available, threading model unimplementable");
		}
#endif
		AbstractThread::m_threadsAvailable++;
	}
	m_id = 0;
	m_name = name;
	m_handle = NULL;
	m_hangCheck = 0;
	m_priority = priority;
}

AbstractThread::~AbstractThread()
{
	terminate();
	AbstractThread::m_threadsAvailable--;
	if( AbstractThread::m_threadsAvailable == 0 )
	{
		// all running threads have gone, the thread subsystem is no longer needed
#ifdef _WIN32
		CoUninitialize();
#else
		// No pthread equivalent
#endif
	}
}

unsigned AbstractThread::getId()
{
	return m_id;
}

const char *AbstractThread::getName()
{
	return m_name;
}

void AbstractThread::start()
{
#ifdef _WIN32
	m_handle = (spherethread_t) _beginthreadex(NULL, 0, &runner, this, 0, &m_id);
#else
	// pthread_create doesn't return the new thread id, so we set it in runner?
	if ( pthread_create( &m_handle, NULL, &runner, this ) )
	{
		m_handle = (spherethread_t) NULL;
		throw new CException(LOGL_FATAL, 0, "Unable to spawn a new thread");
	}
	else
		m_id = (unsigned) m_handle; //pthread_self() and m_handle should be the same
#endif
	push(this);
}

void AbstractThread::terminate()
{
	if( isActive() )
	{
#ifdef _WIN32
		if( getId() == ::GetCurrentThreadId() )
		{
			_endthreadex(0);
		}
		else
		{
			TerminateThread(m_handle, 0);
		}
		CloseHandle(m_handle);
#else
		if( pthread_equal(m_handle,pthread_self()) )
		{
			pthread_exit(0);
		}
		else
		{
			pthread_cancel(m_handle); // IBM say it so
		}
#endif
		// Common things
		pop(this);
		m_id = 0;
		m_handle = NULL;
	}
}

void AbstractThread::run()
{
	// is the very first since there is a possibility of something being altered there
	onStart();

	// detect a sleep period for thread depending on priority
	int tickPeriod;
	switch( m_priority )
	{
		case IThread::Idle:
			tickPeriod = 1000;
			break;
		case IThread::Low:
			tickPeriod = 200;
			break;
		case IThread::Normal:
			tickPeriod = 100;
			break;
		case IThread::High:
			tickPeriod = 50;
			break;
		case IThread::Highest:
			tickPeriod = 25;
			break;
		case IThread::RealTime:
			tickPeriod = 0;
			break;
		default:
			throw new CException(LOGL_FATAL, 0, "Unable to determine thread priority");
	}

	int exceptions = 0;
	bool lastWasException = false;
	while( true )
	{
		bool gotException = false;

		//	report me being alive if I am being checked for status
		if( m_hangCheck != 0 )
		{
			m_hangCheck = 0;
		}

		try
		{
			tick();
		}
		catch( CException e )
		{
			gotException = true;
			//	TODO: notify of exceptions
		}
		catch( ... )
		{
			gotException = true;
			//	TODO: notify of exceptions
		}

		if( gotException )
		{
			if( lastWasException )
			{
				exceptions++;
			}
			else
			{
				lastWasException = true;
				exceptions = 0;
			}
			if( exceptions >= EXCEPTIONS_ALLOWED )
			{
				// a bad thing really happened. ALL previous EXCEPTIONS_ALLOWED ticks resulted in exception
				// almost for sure we have looped somewhere and have no way to get out from this situation
				// probably a thread restart can fix the problems
				// but there is no real need to restart a thread, we will just simulate a thread restart,
				// notifying a subclass like we have been just restarted, so it will restart it's operations
				//	TODO: notify the logger
				onStart();
				lastWasException = false;
			}
		}
		else
		{
			lastWasException = false;
		}

		if( shouldExit() )
		{
			terminate();
		}
		Sleep(tickPeriod);
	}
}

SPHERE_THREADENTRY_RETNTYPE AbstractThread::runner(void *callerThread)
{
	AbstractThread *caller = (AbstractThread*)callerThread;

	caller->run();

#ifdef _WIN32
	_endthreadex(0);
#else
	pthread_exit(0);
#endif
	return 0;
}

bool AbstractThread::isActive()
{
	return m_handle != NULL;
}

void AbstractThread::waitForClose()
{
	int count = 150; // 15 seconds = 15000ms
	while( isActive() && count-- )
	{
		Sleep(100);
	}
	terminate();
}

void AbstractThread::checkStuck()
{
	if( isActive() )
	{
		if( m_hangCheck == 0 )
		{
			//	initiate hang check
			m_hangCheck = 0xDEAD;
		}
		else if( m_hangCheck == 0xDEAD )
		{
			//	one time period was not answered, wait a bit more
			m_hangCheck = 0xDEADDEADl;
			//	TODO:
			//g_Log.Event(LOGL_CRIT, "'%s' thread seems being hang (frozen) at '%s'?\n", m_name, m_action);
		}
		else if( m_hangCheck == 0xDEADDEADl )
		{
			//	TODO:
			//g_Log.Event(LOGL_CRIT, "'%s' thread hang, restarting...\n", m_name);
			terminate();
			start();
		}
	}
}

void AbstractThread::onStart()
{
	//	empty. override if need in subclass
}

bool AbstractThread::shouldExit()
{
	return false;
}

/*
 * AbstractSphereThread
*/
AbstractSphereThread::AbstractSphereThread(const char *name, Priority priority)
	: AbstractThread(name, priority)
{
}

// IMHO we need a lock on allocateBuffer and allocateStringBuffer

char *AbstractSphereThread::allocateBuffer()
{
//	SimpleThreadLock stlBuffer(g_tmpStringMutex);

	char * buffer = NULL; 
	g_tmpStringIndex++;

	if( g_tmpStringIndex >= THREAD_TSTRING_STORAGE )
	{
		g_tmpStringIndex %= THREAD_TSTRING_STORAGE;
	}

	buffer = g_tmpStrings[g_tmpStringIndex];
	*buffer = '\0';

	return buffer;
}

char *AbstractSphereThread::allocateStringBuffer()
{
//	SimpleThreadLock stlBuffer(g_tmpTemporaryStringMutex);

	long initialPosition = g_tmpTemporaryStringIndex;
	while( true )
	{
		long index = ++g_tmpTemporaryStringIndex;
		if( g_tmpTemporaryStringIndex >= THREAD_STRING_STORAGE )
		{
			index = g_tmpTemporaryStringIndex %= THREAD_STRING_STORAGE;
		}

		if( g_tmpTemporaryStringUsed[index] == 0 )
		{
			char *buffer = g_tmpTemporaryStrings[index];
			*buffer = '\0';
			return buffer;
		}

		// a protection against deadlock. All string buffers are marked as being used somewhere, so we
		// have few possibilities (the case shows that we have a bug and temporary strings used not such):
		// a) return NULL and wait for exceptions in the program
		// b) allocate a string from a heap
		if( initialPosition == index )
		{
			// but the best is to throw an exception to give better formed information for end users
			// rather than access violations
			DEBUG_WARN(( "Thread temporary string buffer is full.\n" ));
			throw new CException(LOGL_FATAL, 0, "Thread temporary string buffer is full");
		}
	}
}

String AbstractSphereThread::allocateString()
{
	TemporaryString s(allocateStringBuffer(), &g_tmpTemporaryStringUsed[g_tmpTemporaryStringIndex]);

	return s;
}

void AbstractSphereThread::allocateString(TemporaryString &string)
{
	string.init(allocateStringBuffer(), &g_tmpTemporaryStringUsed[g_tmpTemporaryStringIndex]);
}

bool AbstractSphereThread::shouldExit()
{
	if( g_Serv.m_iModeCode == SERVMODE_Exiting )
	{
		return true;
	}
	return false;
}

/*
 * DummySphereThread
*/
DummySphereThread *DummySphereThread::instance = NULL;

DummySphereThread::DummySphereThread()
	: AbstractSphereThread("dummy", IThread::Normal)
{
}

DummySphereThread *DummySphereThread::getInstance()
{
	if( instance == NULL )
	{
		instance = new DummySphereThread();
	}
	return instance;
}

void DummySphereThread::tick()
{
}
