#ifndef TIME_WHEEL_H 
#define TIME_WHEEL_H

#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <queue>
#include <memory>
#include <functional>
#include <ctime>
#include <stdint.h>
#include <type_traits>
#include <utility>

typedef uint64_t Tick;

struct Task
{
	template<typename F>
	Task(uint32_t id, uint32_t delay, F&& func, bool isLoopExecution):
		_id(id), _delay(delay), _func(std::forward(func)), _isLoopExecution(isLoopExecution)
	{
		using storedFunc = std::decay_t<F>;
		static_assert(std::is_nothrow_invocable_r_v<void, storedFunc>, "Task callback must be callable as void() noexcept");
	};

	const uint32_t _id;
	const uint32_t _delay;
	const std::function<void()> _func;
	const bool _isLoopExecution;
};

struct TimerEntry
{
	Tick _expireTick; 
	std::unique_ptr<Task> _task;
};

class TimeWheel
{
public:	
	TimeWheel() = default;
	void Update();
	bool addTask(std::unique_ptr<Task>&& task);
private:
	bool executeTask(std::unique_ptr<Task>&& task);	
	bool place(TimerEntry&& entry); 
	void cascadeL2(unsigned int pos);
	void cascadeL3(unsigned int pos);
private:
	static constexpr int wheelSize = 1024;
	static constexpr int tickDurationL2 = wheelSize;
	static constexpr int tickDurationL3 = tickDurationL2 *  wheelSize;
	static constexpr int maxTick = tickDurationL3 * wheelSize;
	typedef std::array<std::queue<TimerEntry>, static_cast<size_t>(wheelSize)> Slot;
	
	Slot slotL1;
	Slot slotL2;
	Slot slotL3;
	Tick currentTick = 0;
};
#endif
