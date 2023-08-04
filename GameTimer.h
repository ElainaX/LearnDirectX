#pragma once

#include <Windows.h> // for performance timer


class GameTimer
{
public:
	GameTimer();

	float TotalTime();
	float DeltaTime();

	void Reset();	// ��Ϣѭ��֮ǰ����
	void Start();	// �Ӵ���ͣ
	void Stop();	// ��ͣ��ʱ��
	void Tick();	// ÿ֡����

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