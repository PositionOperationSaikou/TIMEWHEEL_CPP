#include "TimeWheel.h"
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

bool TimeWheel::place(TimerEntry&& entry)
{
	Tick remaining = entry._nextAttemptTick - currentTick;
	
	if(remaining < tickDurationL2)
	{
		const auto pos = 
				entry._nextAttemptTick % tickDurationL2;
		slotL1[pos].push(std::move(entry));
	}
	else if(remaining < tickDurationL3)
	{
		const auto pos = 
				(entry._nextAttemptTick % tickDurationL3) / 
				tickDurationL2;
		
		slotL2[pos].push(std::move(entry));
	}
	else if(remaining < maxTick)
	{
		const auto pos = 
				(entry._nextAttemptTick % maxTick) / 
				tickDurationL3;
		
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
	if(!task || 
		!task->_func || 
		task->_delay >= maxTick || 
		!task->_delay)
	{	
		return false;	
	}
	
	if(task->_retry)
	{
		if (!task->_retry->_retryDelay ||
			task->_retry->_retryDelay >= maxTick)
		{
			return false;
		}	
	}

	const Tick scheduledTick = currentTick + task->_delay;
	return place(TimerEntry(
		scheduledTick,
		scheduledTick,
		std::move(task)));	
}

void TimeWheel::executeTask(TimerEntry&& entry)
{
	bool success(false);	
	auto& task = entry._task;
	
	try
	{
		task->_func();
		success = true;
	}
	catch(const std::exception& error)
	{
		std::cerr << "task:" << task->_id 
				<< "error:" << error.what() << std::endl;
	}
	catch(...)
	{
		std::cerr << "task:" << task->_id 
				<< "error:" << "unknown error" << std::endl;
	}
	
	if(success)
	{
		Tick elapsed = currentTick - entry._scheduledTick;
		Tick periods = elapsed / task->_delay + 1;
		entry._scheduledTick += periods * task->_delay;
		entry._nextAttemptTick = entry._scheduledTick;	
		task->_retryCount = 0;

		if(task->_isLoopExecution)
			place(std::move(entry));	
	}	
	else if(task->_retry &&
			task->_retry->_retryDelay &&
			task->_retryCount < task->_retry->_maxRetries)	
	{
			++task->_retryCount;
			entry._nextAttemptTick = 
					currentTick + task->_retry->_retryDelay;
			
			place(std::move(entry));
	}
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
		const int pos = (currentTick % maxTick) / 
				tickDurationL3;
		
		cascadeL3(pos);
	}

	if(!(currentTick % tickDurationL2))
	{
		const int pos = (currentTick % tickDurationL3) / 
				tickDurationL2;
		
		cascadeL2(pos);
	}	

	const int pos = currentTick % tickDurationL2;	
	while (!slotL1[pos].empty())
   	{	
		auto entry = std::move(slotL1[pos].front());
		slotL1[pos].pop();
		executeTask(std::move(entry));
	} 
}
