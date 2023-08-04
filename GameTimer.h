#pragma once

#include <Windows.h> // for performance timer


class GameTimer
{
public:
	GameTimer();

	float TotalTime();
	float DeltaTime();

	void Reset();	// 消息循环之前调用
	void Start();	// 接触暂停
	void Stop();	// 暂停计时器
	void Tick();	// 每帧调用

private:
	double mSecondsPerCount;
	double mDeltaTime;


	__int64 mCurrTime;
	__int64 mPrevTime;
	__int64 mBaseTime;
	__int64 mPausedTime;
	__int64 mStopTime;

	bool mStopped;
};