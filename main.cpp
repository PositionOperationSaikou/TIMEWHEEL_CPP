#include "TimeWheel.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
constexpr std::uint32_t kWheelSize = 1024;
constexpr std::uint32_t kL3Duration = kWheelSize * kWheelSize;
constexpr std::uint32_t kMaxTick = kL3Duration * kWheelSize;

class TestRunner
{
public:
	void expect(bool condition, std::string_view description)
	{
		if(condition)
		{
			++passed_;
			std::cout << "[PASS] " << description << '\n';
			return;
		}

		++failed_;
		std::cout << "[FAIL] " << description << '\n';
	}

	int finish() const
	{
		std::cout << "\n测试完成: " << passed_ << " 项通过, "
				  << failed_ << " 项失败。\n";
		return failed_ == 0 ? 0 : 1;
	}

private:
	int passed_ = 0;
	int failed_ = 0;
};

void advance(TimeWheel& wheel, Tick ticks)
{
	for(Tick i = 0; i < ticks; ++i)
		wheel.Update();
}

void testInvalidTasks(TestRunner& runner)
{
	TimeWheel wheel;

	std::unique_ptr<Task> nullTask;
	runner.expect(!wheel.addTask(std::move(nullTask)),
				  "拒绝空任务指针");

	auto zeroDelay = std::make_unique<Task>(
		1, 0, []() {}, false, std::nullopt);
	runner.expect(!wheel.addTask(std::move(zeroDelay)),
				  "拒绝 delay 为 0 的任务");
	runner.expect(static_cast<bool>(zeroDelay),
				  "拒绝任务时不取得其所有权");

	auto tooFar = std::make_unique<Task>(
		2, kMaxTick, []() {}, false, std::nullopt);
	runner.expect(!wheel.addTask(std::move(tooFar)),
				  "拒绝超出时间轮范围的 delay");

	auto invalidRetry = std::make_unique<Task>(
		3, 1, []() {}, false, Retry{kMaxTick, 1});
	runner.expect(!wheel.addTask(std::move(invalidRetry)),
				  "拒绝超出时间轮范围的 retryDelay");

	using Callback = void (*)();
	Callback nullCallback = nullptr;
	auto emptyFunction = std::make_unique<Task>(
		4, 1, nullCallback, false, std::nullopt);
	runner.expect(!wheel.addTask(std::move(emptyFunction)),
				  "拒绝空回调函数");
}

void testOneShotAtDelay(TestRunner& runner,
						std::string_view name,
						Tick initialTicks,
						std::uint32_t delay)
{
	TimeWheel wheel;
	advance(wheel, initialTicks);

	int calls = 0;
	auto task = std::make_unique<Task>(
		10, delay, [&calls]() { ++calls; }, false, std::nullopt);

	const bool accepted = wheel.addTask(std::move(task));
	runner.expect(accepted, name);
	if(!accepted)
		return;

	advance(wheel, static_cast<Tick>(delay) - 1);
	runner.expect(calls == 0, "到期前一 tick 不执行");

	wheel.Update();
	runner.expect(calls == 1, "到期 tick 恰好执行一次");

	wheel.Update();
	runner.expect(calls == 1, "一次性任务到期后不重复执行");
}

void testTasksInSameSlot(TestRunner& runner)
{
	TimeWheel wheel;
	std::array<int, 3> order{};
	std::size_t next = 0;

	auto addOrderedTask = [&](int value)
	{
		auto task = std::make_unique<Task>(
			static_cast<std::uint32_t>(value),
			5,
			[&order, &next, value]()
			{
				if(next < order.size())
					order[next++] = value;
			},
			false,
			std::nullopt);

		return wheel.addTask(std::move(task));
	};

	runner.expect(addOrderedTask(1) && addOrderedTask(2) && addOrderedTask(3),
				  "同一槽位可以添加多个任务");
	advance(wheel, 5);
	runner.expect(next == 3, "同一槽位的所有任务都会执行");
	runner.expect(order == std::array<int, 3>{1, 2, 3},
				  "同一槽位按加入顺序执行任务");
}

void testFixedRateLoop(TestRunner& runner)
{
	TimeWheel wheel;
	int calls = 0;
	auto task = std::make_unique<Task>(
		20, 3, [&calls]() { ++calls; }, true, std::nullopt);

	runner.expect(wheel.addTask(std::move(task)), "添加 fixed-rate 循环任务");
	advance(wheel, 9);
	runner.expect(calls == 3, "循环任务在 tick 3、6、9 执行");
}

void testShortRetry(TestRunner& runner)
{
	TimeWheel wheel;
	int attempts = 0;
	auto task = std::make_unique<Task>(
		30, 3,
		[&attempts]()
		{
			++attempts;
			if(attempts < 3)
				throw std::runtime_error("short retry");
		},
		false,
		Retry{2, 2});

	runner.expect(wheel.addTask(std::move(task)), "添加短时间重试任务");
	advance(wheel, 6);
	runner.expect(attempts == 2, "tick 6 前完成首次执行和第一次重试");
	wheel.Update();
	runner.expect(attempts == 3, "任务在 tick 7 第二次重试成功");
}

void testLongRetry(TestRunner& runner)
{
	TimeWheel wheel;
	int attempts = 0;
	auto task = std::make_unique<Task>(
		31, 1,
		[&attempts]()
		{
			++attempts;
			if(attempts == 1)
				throw std::runtime_error("long retry");
		},
		false,
		Retry{1024, 1});

	runner.expect(wheel.addTask(std::move(task)), "添加跨 L2 的重试任务");
	advance(wheel, 1024);
	runner.expect(attempts == 1, "跨 L2 重试到期前不执行第二次");
	wheel.Update();
	runner.expect(attempts == 2, "跨 L2 重试在 tick 1025 成功");
}

void testRetryLimit(TestRunner& runner)
{
	TimeWheel wheel;
	int attempts = 0;
	auto task = std::make_unique<Task>(
		32, 1,
		[&attempts]()
		{
			++attempts;
			throw 42;
		},
		false,
		Retry{2, 3});

	runner.expect(wheel.addTask(std::move(task)),
				  "添加抛出非 std::exception 的任务");
	advance(wheel, 20);
	runner.expect(attempts == 4,
				  "maxRetries=3 表示首次执行外最多重试三次");
}

void testRetryCountReset(TestRunner& runner)
{
	TimeWheel wheel;
	int attempts = 0;
	auto task = std::make_unique<Task>(
		33, 2,
		[&attempts]()
		{
			++attempts;
			if(attempts == 1 || attempts == 3)
				throw std::runtime_error("intermittent");
		},
		true,
		Retry{1, 1});

	runner.expect(wheel.addTask(std::move(task)),
				  "添加间歇失败的循环任务");
	advance(wheel, 6);
	runner.expect(attempts == 5,
				  "成功后重置重试次数并恢复 fixed-rate 时间轴");
}

void testFixedRateAfterRetry(TestRunner& runner)
{
	TimeWheel wheel;
	int attempts = 0;
	auto task = std::make_unique<Task>(
		34, 10,
		[&attempts]()
		{
			++attempts;
			if(attempts == 1)
				throw std::runtime_error("retry then fixed-rate");
		},
		true,
		Retry{3, 1});

	runner.expect(wheel.addTask(std::move(task)),
				  "添加重试后恢复时间轴的循环任务");
	advance(wheel, 19);
	runner.expect(attempts == 2, "tick 10 失败并在 tick 13 重试成功");
	wheel.Update();
	runner.expect(attempts == 3,
				  "重试成功后仍在原时间轴 tick 20 执行");
}

void testLoopCancellation(TestRunner& runner)
{
	{
		TimeWheel wheel;
		int attempts = 0;
		auto task = std::make_unique<Task>(
			40, 2,
			[&attempts]()
			{
				++attempts;
				throw std::runtime_error("cancel without retry");
			},
			true,
			std::nullopt);

		runner.expect(wheel.addTask(std::move(task)),
					  "添加无重试策略的异常循环任务");
		advance(wheel, 10);
		runner.expect(attempts == 1,
					  "无 Retry 配置时异常后取消循环任务");
	}

	{
		TimeWheel wheel;
		int attempts = 0;
		auto task = std::make_unique<Task>(
			41, 2,
			[&attempts]()
			{
				++attempts;
				throw std::runtime_error("retry exhausted");
			},
			true,
			Retry{1, 1});

		runner.expect(wheel.addTask(std::move(task)),
					  "添加最多重试一次的循环任务");
		advance(wheel, 10);
		runner.expect(attempts == 2,
					  "重试耗尽后取消整个循环任务");
	}
}

void testFailureIsolation(TestRunner& runner)
{
	TimeWheel wheel;
	int secondTaskCalls = 0;
	auto throwingTask = std::make_unique<Task>(
		50, 1,
		[]() { throw std::runtime_error("isolated"); },
		false,
		std::nullopt);
	auto normalTask = std::make_unique<Task>(
		51, 1,
		[&secondTaskCalls]() { ++secondTaskCalls; },
		false,
		std::nullopt);

	runner.expect(wheel.addTask(std::move(throwingTask)),
				  "添加无重试的异常任务");
	runner.expect(wheel.addTask(std::move(normalTask)),
				  "添加同槽正常任务");
	wheel.Update();
	runner.expect(secondTaskCalls == 1,
				  "一个任务抛异常不阻断同槽后续任务");
}
}

int main()
{
	TestRunner runner;

	testInvalidTasks(runner);
	testOneShotAtDelay(runner, "添加 delay=1 的 L1 任务", 0, 1);
	testOneShotAtDelay(runner, "添加 delay=1023 的 L1 边界任务", 0,
				   kWheelSize - 1);
	testOneShotAtDelay(runner, "添加 delay=1024 的 L2 边界任务", 0,
				   kWheelSize);
	testOneShotAtDelay(runner,
				   "在 currentTick=1000 时添加 delay=1500 的 L2 任务",
				   1000, 1500);
	testOneShotAtDelay(runner,
				   "在非零 currentTick 时添加 L3 边界任务",
				   37, kL3Duration);
	testTasksInSameSlot(runner);
	testFixedRateLoop(runner);
	testShortRetry(runner);
	testLongRetry(runner);
	testRetryLimit(runner);
	testRetryCountReset(runner);
	testFixedRateAfterRetry(runner);
	testLoopCancellation(runner);
	testFailureIsolation(runner);

	return runner.finish();
}
