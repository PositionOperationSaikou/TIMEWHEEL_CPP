#include "TimeWheel.h"
#include <memory>
#include <utility>

bool TimeWheel::place(TimerEntry&& entry)
{
	Tick remaining = entry._expireTick - currentTick;
	
	if(remaining < tickDurationL2)
	{
		const auto pos = 
				entry._expireTick % tickDurationL2;
		slotL1[pos].push(std::move(entry));
	}
	else if(remaining < tickDurationL3)
	{
		const auto pos = 
				(entry._expireTick % tickDurationL3) / tickDurationL2;
		slotL2[pos].push(std::move(entry));
	}
	else if(remaining < maxTick)
	{
		const auto pos = 
				(entry._expireTick % maxTick) / tickDurationL3;
		slotL3[pos].push(std::move(entry));
	}
	else
	{
		return false; 
	}

	return true;
}

bool TimeWheel::addTask(std::unique_ptr<Task>&& task)
{
	if(!task || !task->_func || 
		task->_delay >= maxTick || !task->_delay)
		return false;	

	TimerEntry timerEntry = {
		currentTick + task->_delay,
		std::move(task)
	};
	
	return place(std::move(timerEntry));	
}

bool TimeWheel::executeTask(std::unique_ptr<Task>&& task)
{
	task->_func();
	
	if(task->_isLoopExecution)
		return addTask(std::move(task));	

	return true;
}


void TimeWheel::cascadeL3(unsigned int pos)
{
	while(!slotL3[pos].empty())
	{
		auto entry = std::move(slotL3[pos].front());
		slotL3[pos].pop();
		place(std::move(entry));	
	}	
}

void TimeWheel::cascadeL2(unsigned int pos)
{
	while(!slotL2[pos].empty())
	{
		auto entry = std::move(slotL2[pos].front());
		slotL2[pos].pop();
		place(std::move(entry));
	}	
}

void TimeWheel::Update()
{	
	++currentTick;
	
	if(!(currentTick % tickDurationL3))
	{
		const int pos = (currentTick % maxTick) / tickDurationL3;
		cascadeL3(pos);
	}

	if(!(currentTick % tickDurationL2))
	{
		const int pos = (currentTick % tickDurationL3) / tickDurationL2;
		cascadeL2(pos);
	}	

	const int pos = currentTick % tickDurationL2;	
	while (!slotL1[pos].empty())
   	{	
		auto entry = std::move(slotL1[pos].front());
		slotL1[pos].pop();
		executeTask(std::move(entry._task));
	} 
}
