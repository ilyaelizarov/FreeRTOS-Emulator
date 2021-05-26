2.3 Using the mouse

How does the mouse guarantee thread-safe functionality?

It uses semaphore functionality. For example, xGetMouseX() takes a mouse.lock semaphore and gives it back after reading coordinates. This way, the mouse can't be accessed from other threads during coordinate reading.


3.1 General Questions

What is the kernel tick? Experiment with its frequency.

Tick - elementary unit of time that is used by a kernel, or more precesely, by ascheduler to distribute computation power. The scheduler is invoked in the periodic manner by SysTick. The tick frequency can be adjusted. 

What is a tickless kernel?

It's when the scheluder is not invoked by SysTick, but by other ways e.g. by an interrupt. This approach can help to decrease power consumption.

What happens if there's not enough stack?
The task won'r return, it will return error code
