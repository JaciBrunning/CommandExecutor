#include "WPILib.h"
#include "executor.hpp"

#include <iostream>

CommandExecutor::Executor exec;
bool _command_running = false;

class MyCommand : CommandExecutor::Command {
    MyCommand() {
        timeout(1000);      // This is optional, implement Command::complete() if you want to stop on another condition
    }

    // Optional Method: Called when started
    void start() {
        std::cout << "Started" << std::endl;
        _command_running = true;
    }

    // Required Method: Called when ticked
    void periodic() {
        std::cout << "Periodic" << std::endl;
    }

    // Optional Method: Called when stopped
    void stop() {
        std::cout << "Stopped" << std::endl;
        _command_running = false;
    }

    // Optional Method, return false if you don't want to allow this command
    // to run. If returns false, the command will be discarded.
    // In this case, we're only allowing it to run if it's not already running
    // (one at once)
    bool can_start() {
        return !_command_running;
    }
}

class Robot: public IterativeRobot
{
private:
	void RobotInit()
	{
        
	}

    void TeleopInit() {
        exec.push(new MyCommand());         // Start now
        exec.push(new MyCommand(), 2000);   // Start after 2 seconds
    }

    void TeleopPeriodic() {
        exec.tick();
    }
};