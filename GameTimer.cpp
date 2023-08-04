#include "GameTimer.h"

GameTimer::GameTimer()
	: mSecondsPerCount(0.0), mDeltaTime(-1.0), mBaseTime(0),
	mPausedTime(0), mPrevTime(0), mCurrTime(0), mStopTime(0), mStopped(false)
{
	__int64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	mSecondsPerCount = 1.0 / (double)countsPerSec;
}

float GameTimer::TotalTime()
{
	if (mStopped)
	{
		return static_cast<float>((mStopTime - mPausedTime - mBaseTime) * mSecondsPerCount);
	}
	else
	{
		return static_cast<float>((mCurrTime - mPausedTime - mBaseTime) * mSecondsPerCount);
	}
}

float GameTimer::DeltaTime()
{
	return static_cast<float>(mDeltaTime);
}

void GameTimer::Reset()	// 消息循环之前调用
{
	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

	mBaseTime = currTime;
	mPrevTime = currTime;
	mStopTime = 0;
	mStopped = false;
}

void GameTimer::Start()	// 接触暂停
{
	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);
	if (mStopped)
	{
		mPausedTime += startTime - mStopTime;
		mPrevTime = startTime;
		mCurrTime = startTime; // 我加了这句

		mStopTime = 0;
		mStopped = false;
	}
}

void GameTimer::Stop()	// 暂停计时器
{
	if (!mStopped)
	{
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

		mStopTime = currTime;
		mStopped = true;
	}
}

void GameTimer::Tick()	// 每帧调用
{
	if (mStopped)
	{
		mDeltaTime = 0.0;
		return;
	}

	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	mCurrTime = currTime;

	mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;

	mPrevTime = mCurrTime;

	// 在某些特定节能模式，处理器切换的时候可能会出现计时减少的情况
	if (mDeltaTime < 0.0)
	{
		mDeltaTime = 0.0;
	}
}
