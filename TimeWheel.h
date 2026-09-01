#ifndef TIME_WHEEL_H 
#define TIME_WHEEL_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <memory>
#include <functional>
#include <ctime>
#include <ratio>
#include <stdint.h>

typedef uint64_t Tick;

struct Retry
{
	const uint32_t _retryDelay = 1;
	const uint32_t _maxRetries = 3;
};

struct Task
{
	template<typename F>
	Task(uint32_t id, uint32_t delay, 
		F&& callback, bool isLoopExecution, std::optional<Retry> retry): 
		_id(id), _delay(delay), _func(std::forward<F>(callback)), 
		_isLoopExecution(isLoopExecution), _retry(retry)
	{
		using FuncStore = std::decay_t<F>;
		static_assert(std::is_invocable_r_v<void, FuncStore&>, 
			"Task callback must be callbale as void()");
	}

	const uint32_t _id;
	const uint32_t _delay;
	const std::function<void()> _func;
	const bool _isLoopExecution;
	const std::optional<Retry> _retry;
	uint32_t _retryCount = 0;
};

struct TimerEntry
{
	TimerEntry(Tick scheduledTick, 
		Tick nextAttemptTick, 
		std::unique_ptr<Task> task):
			_scheduledTick(scheduledTick), 
			_nextAttemptTick(nextAttemptTick),
			_task(std::move(task))
	{
		
	}
	Tick _scheduledTick;
	Tick _nextAttemptTick;
	std::unique_ptr<Task> _task;
};


class TimeWheel
{
public:	
	TimeWheel() = default;
	void Update();
	bool addTask(std::unique_ptr<Task>&& task);
private:
	void executeTask(TimerEntry&& timerEntry);	
	bool place(TimerEntry&& entry); 
	void cascadeL2(unsigned int pos);
	void cascadeL3(unsigned int pos);
private:
	static constexpr int wheelSize = 1024;
	static constexpr int tickDurationL2 = wheelSize;
	static constexpr int tickDurationL3 = tickDurationL2 *  wheelSize;
	static constexpr int maxTick = tickDurationL3 * wheelSize;
	typedef std::array<
			std::queue<TimerEntry>, 
			static_cast<size_t>(wheelSize)> Slot;
	
	Slot slotL1;
	Slot slotL2;
	Slot slotL3;
	Tick currentTick = 0;
};

#endif
