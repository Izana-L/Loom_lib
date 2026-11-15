#pragma once
#include <functional>
#include <atomic>
namespace Loom
{
    struct Task
    {
        std::function<void()> function;
        std::atomic<int>* pending = nullptr;
    };
}
