#include "TimeWheel.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
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
		1, 0, []() noexcept {}, false);
	runner.expect(!wheel.addTask(std::move(zeroDelay)),
				  "拒绝 delay 为 0 的任务");
	runner.expect(static_cast<bool>(zeroDelay),
				  "拒绝任务时不取得其所有权");

	auto tooFar = std::make_unique<Task>(
		2, kMaxTick, []() noexcept {}, false);
	runner.expect(!wheel.addTask(std::move(tooFar)),
				  "拒绝超出时间轮范围的任务");

	using Callback = void (*)() noexcept;
	Callback nullCallback = nullptr;
	auto emptyFunction = std::make_unique<Task>(
		3, 1, nullCallback, false);
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
		10, delay, [&calls]() noexcept { ++calls; }, false);

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
			[&order, &next, value]() noexcept
			{
				if(next < order.size())
					order[next++] = value;
			},
			false);
		return wheel.addTask(std::move(task));
	};

	runner.expect(addOrderedTask(1) && addOrderedTask(2) && addOrderedTask(3),
				  "同一槽位可以添加多个任务");
	advance(wheel, 5);
	runner.expect(next == 3, "同一槽位的所有任务都会执行");
	runner.expect(order == std::array<int, 3>{1, 2, 3},
				  "同一槽位按加入顺序执行任务");
}

void testLoopTask(TestRunner& runner)
{
	TimeWheel wheel;
	int calls = 0;
	auto task = std::make_unique<Task>(
		20, 3, [&calls]() noexcept { ++calls; }, true);

	runner.expect(wheel.addTask(std::move(task)), "添加循环任务");
	advance(wheel, 2);
	runner.expect(calls == 0, "循环任务首次到期前不执行");

	wheel.Update();
	runner.expect(calls == 1, "循环任务在第 3 tick 首次执行");
	advance(wheel, 3);
	runner.expect(calls == 2, "循环任务在第 6 tick 再次执行");
	advance(wheel, 3);
	runner.expect(calls == 3, "循环任务保持固定时间间隔");
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
				   "在非零 currentTick 时添加 delay=1048576 的 L3 任务",
				   37, kL3Duration);
	testTasksInSameSlot(runner);
	testLoopTask(runner);

	return runner.finish();
}
