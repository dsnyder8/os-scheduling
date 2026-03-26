#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <ncurses.h>
#include "configreader.h"
#include "process.h"

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex queue_mutex;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    bool all_terminated;
} SchedulerData;

void coreRunProcesses(uint8_t core_id, SchedulerData *data);
void printProcessOutput(std::vector<Process*>& processes);
std::string makeProgressString(double percent, uint32_t width);
uint64_t currentTime();
std::string processStateToString(Process::State state);

int main(int argc, char *argv[])
{
    // Ensure user entered a command line parameter for configuration file name
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Declare variables used throughout main
    int i;
    SchedulerData *shared_data = new SchedulerData();
    std::vector<Process*> processes;

    // Read configuration file for scheduling simulation
    SchedulerConfig *config = scr::readConfigFile(argv[1]);

    // Store number of cores in local variable for future access
    uint8_t num_cores = config->cores;

    // Store configuration parameters in shared data object
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // Create processes
    uint64_t start = currentTime();
    for (i = 0; i < config->num_processes; i++)
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        // If process should be launched immediately, add to ready queue
        if (p->getState() == Process::State::Ready)
        {
            shared_data->ready_queue.push_back(p);
        }
    }

    // Free configuration data from memory
    scr::deleteConfig(config);

    // Launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
    }

    // Main thread work goes here
    initscr();
    while (!(shared_data->all_terminated)){
        // Do the following:
        //   - Get current time
        uint64_t current_time = currentTime();
        uint64_t elapsed_time = current_time - start;


        //lock ready queue mutex before accessing; remember to unlock when done
        shared_data->queue_mutex.lock();
        for(int i = 0; i < processes.size(); i++){

        //   - *Check if any processes need to move from NotStarted to Ready (based on elapsed time), and if so put that process in the ready queue
       
            if(processes[i]->getState() == Process::State::NotStarted){
                //check the algorithm tpye here?
                if(elapsed_time >= processes[i]->getStartTime()){
                    processes[i]->setState(Process::State::Ready, currentTime());
                    //here a big if else statement to check which algorithm we want to use putting this back on to the ready queue
                    if(shared_data->algorithm == ScheduleAlgorithm::FCFS){
                        shared_data->ready_queue.push_back(processes[i]);
                    }
                    else if(shared_data->algorithm == ScheduleAlgorithm::SJF){
                        //place process in ready queue based on shortest job first (i.e. total CPU time for all bursts)
                        //have to use iterator here because the ready queue is a list not a vector, so we can't use indexing
                        //thoughts before implementing: iterate through the ready que and find the first process with a longer remaining CPU burst time and insert the new process before it
                        std::list<Process*>::iterator it = shared_data->ready_queue.begin();

                        while(it != shared_data->ready_queue.end()){
                            if((*it)->getRemainingTime() > processes[i]->getRemainingTime()){
                                shared_data->ready_queue.insert(it, processes[i]);
                                break;
                            }
                            ++it;
                        }
                        if(it == shared_data->ready_queue.end()){
                            shared_data->ready_queue.push_back(processes[i]);
                        }


                    
                    }else if(shared_data->algorithm == ScheduleAlgorithm::PP){
                        //we have a getPriority function that returns a number from 0-4 
                        //Thought before implementing: loop through the ready queue checking each priority number until we find a process with a lower priority number than the new process,
                        // and insert the new process before it. If we reach the end of the ready queue without finding a lower priority number, insert the new process at the end of the ready queue.
                        std::list<Process*>::iterator it = shared_data->ready_queue.begin();
                        while(it != shared_data->ready_queue.end()){
                            if((*it)->getPriority() > processes[i]->getPriority()){
                                shared_data->ready_queue.insert(it, processes[i]);
                                break;
                            }
                            ++it;
                        }
                        if(it == shared_data->ready_queue.end()){
                            shared_data->ready_queue.push_back(processes[i]);
                        }


                    }else{
                        //default to RR for now, we don't care about ordering in RR so we want to just place it in the back of the queue and do the time splice logic where needed
                        //thoughts: Will go similar to FCFS but we need to implement the time slice, unsure yet *update later*
                        shared_data->ready_queue.push_back(processes[i]);
                    }

                }
            }
        //   -Check if any processes have finished their I/O burst, and if so put that process back in the ready queue
            if(processes[i]->getState() == Process::State::IO){
                uint64_t current_io_time = current_time - processes[i]->getBurstStartTime();
                if(current_io_time >= processes[i]->getCurrentBurstTime()){
                    processes[i]->changeBurst(); 
                    processes[i]->setState(Process::State::Ready, currentTime());
                    //check the algorithm type here to determine how we want to place it on the queue
                    if(shared_data->algorithm == ScheduleAlgorithm::FCFS){
                        shared_data->ready_queue.push_back(processes[i]);
                    }
                    else if(shared_data->algorithm == ScheduleAlgorithm::SJF){
                        //place process in ready queue based on shortest job first (i.e. total CPU time for all bursts)
                        std::list<Process*>::iterator it = shared_data->ready_queue.begin();
                        while(it != shared_data->ready_queue.end()){
                            if((*it)->getRemainingTime() > processes[i]->getRemainingTime()){
                                shared_data->ready_queue.insert(it, processes[i]);
                                break;
                            }
                            ++it;
                        }
                        if(it == shared_data->ready_queue.end()){
                            shared_data->ready_queue.push_back(processes[i]);
                        }

                    }else if(shared_data->algorithm == ScheduleAlgorithm::PP){
                        std::list<Process*>::iterator it = shared_data->ready_queue.begin();
                        while(it != shared_data->ready_queue.end()){
                            if((*it)->getPriority() > processes[i]->getPriority()){
                                shared_data->ready_queue.insert(it, processes[i]);
                                break;
                            }
                            ++it;
                        }
                        if(it == shared_data->ready_queue.end()){
                            shared_data->ready_queue.push_back(processes[i]);
                        }

                    }else{
                        //default to RR for now, we don't care about ordering in RR so we want to just place it in the back of the queue and do the time splice logic where needed
                        shared_data->ready_queue.push_back(processes[i]);
                    }
                }
                //   - *Check if any running process need to be interrupted (RR time slice expires or newly ready process has higher priority)
                    if(shared_data->algorithm == ScheduleAlgorithm::RR){
                        //thoughts for this: loop through all the processes that are running, if their currentBurstTime is greater than the time slice, then we need to interrupt them and place them bac on the ready queue. 
                        for(int j = 0; j < processes.size();j++ ){
                            if (processes[j]->getState() == Process::State::Running) {
                                uint64_t current_burst_time = processes[j]->getCurrentBurstTime();
                                if(current_burst_time > shared_data->time_slice){
                                    processes[j]->interrupt();
                                    processes[j]->setState(Process::State::Ready, currentTime());
                                    //place back on ready queue based on algorithm
                                    shared_data->ready_queue.push_back(processes[j]);
                                }
                            }
                        }
                    }

                    if(shared_data->algorithm == ScheduleAlgorithm::PP && !(shared_data->ready_queue.empty())){
                        //thoughts for this: loop through all the processes that are running, if their priority is lower than the priority of the process at the front of the ready queue, then
                        // we need to interrupt them and place them back on the ready queue in their proper position.
                        Process* front_of_read_queue_priority = shared_data->ready_queue.front();
                        
                        for(int j = 0; j < processes.size();j++ ){
                            if (processes[j]->getState() == Process::State::Running) {
                                if(processes[j]->getPriority() > shared_data->ready_queue.front()->getPriority()){
                                    processes[j]->interrupt();
                                    processes[j]->setState(Process::State::Ready, currentTime());
                                    std::list<Process*>::iterator it = shared_data->ready_queue.begin();
                                    while(it != shared_data->ready_queue.end()){
                                        if((*it)->getPriority() > processes[j]->getPriority()){
                                            shared_data->ready_queue.insert(it, processes[j]);
                                            break;
                                        }
                                        ++it;
                                    }
                                    if(it == shared_data->ready_queue.end()){
                                    shared_data->ready_queue.push_back(processes[j]);
                        }
                                }
                            }
                        }

                    }

                //     - NOTE: ensure processes are inserted into the ready queue at the proper position based on algorithm
                
                    //check if any running process needs to be interrupted based on time slice
                shared_data->queue_mutex.unlock();

        //   - Determine if all processes are in the terminated state
        bool all_processes_terminated = true;
        for(int i = 0; i < processes.size(); i++){
            if(processes[i]->getState() != Process::State::Terminated){
                all_processes_terminated = false;
                break;
            }
            shared_data->all_terminated = all_processes_terminated;
        }
        //   - * = accesses shared data (ready queue), so be sure to use proper synchronization

        // Maybe simply print progress bar for all procs?
        printProcessOutput(processes);

                // sleep 50 ms
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                // clear outout
                erase();
            }
        
        }
    }
    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }

    // print final statistics (use `printw()` for each print, and `refresh()` after all prints)
    //  - CPU utilization
    printw("CPU Utilization: %.2f%%\n", /* TODO: calculate this */ 0.0);
    //  - Throughput
    //     - Average for first 50% of processes finished
    printw("Throughput (first 50%% of processes): %.2f processes/sec\n", /* TODO: calculate this */ 0.0);
    //     - Average for second 50% of processes finished
    printw("Throughput (second 50%% of processes): %.2f processes/sec\n", /* TODO: calculate this */ 0.0);
    //     - Overall average
    printw("Overall Throughput: %.2f processes/sec\n", /* TODO: calculate this */ 0.0);
    //  - Average turnaround time
    printw("Average Turnaround Time: %.2f ms\n", /* TODO: calculate this */ 0.0);
    //  - Average waiting time
    printw("Average Waiting Time: %.2f ms\n", /* TODO: calculate this */ 0.0);


    // Clean up before quitting program
    processes.clear();
    endwin();

    return 0;
}


void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    // Work to be done by each core idependent of the other cores
    // Repeat until all processes in terminated state:
    //   - *Get process at front of ready queue
    //   - IF READY QUEUE WAS NOT EMPTY
    //    - Wait context switching load time
    //    - Simulate the processes running (i.e. sleep for short bits, e.g. 5 ms, and call the processes `updateProcess()` method)
    //      until one of the following:
    //      - CPU burst time has elapsed
    //      - Interrupted (RR time slice has elapsed or process preempted by higher priority process)
    //   - Place the process back in the appropriate queue
    //      - I/O queue if CPU burst finished (and process not finished) -- no actual queue, simply set state to IO
    //      - Terminated if CPU burst finished and no more bursts remain -- set state to Terminated
    //      - *Ready queue if interrupted (be sure to modify the CPU burst time to now reflect the remaining time)
    //   - Wait context switching save time
    //  - IF READY QUEUE WAS EMPTY
    //   - Wait short bit (i.e. sleep 5 ms)
    //  - * = accesses shared data (ready queue), so be sure to use proper synchronization

    while (!(shared_data->all_terminated))
    {
        //lock ready queue mutex before accessing; remember to unlock when done
        shared_data->queue_mutex.lock();
        if (!(shared_data->ready_queue.empty()))
        {
            Process *p = shared_data->ready_queue.front();
            shared_data->ready_queue.pop_front();
            shared_data->queue_mutex.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(shared_data->context_switch));
            p->setState(Process::State::Running, currentTime());
            p->setCpuCore(core_id);

            p->setBurstStartTime(currentTime());

            uint32_t time_on_core = 0; // Local counter for RR

            while (p->getState() == Process::State::Running) 
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                uint64_t now = currentTime();
                time_on_core += now - p->getBurstStartTime();
                p->updateProcess(currentTime());

                // Check for interruptions
                if (shared_data->algorithm == ScheduleAlgorithm::RR && time_on_core >= shared_data->time_slice) 
                {
                    p->setState(Process::State::Ready, currentTime());
                } 
                else if (shared_data->algorithm == ScheduleAlgorithm::PP) 
                {
                    // PP Preemption Check
                    shared_data->queue_mutex.lock();
                    if (!shared_data->ready_queue.empty() && shared_data->ready_queue.front()->getPriority() < p->getPriority()) {
                        p->setState(Process::State::Ready, currentTime());
                    }
                    shared_data->queue_mutex.unlock();
                }
                // Note: If updateProcess() sees the burst hit 0, it changes state to IO or Terminated, 
                // which naturally breaks this while loop!
            }

            // POST-RUN CLEANUP
            p->setCpuCore(-1);

            // If interrupted (RR or PP), put it back in the queue
            if (p->getState() == Process::State::Ready) 
            {
                shared_data->queue_mutex.lock();
                // TODO: If PP, you might need to sort. For RR, just push_back.
                shared_data->ready_queue.push_back(p); 
                shared_data->queue_mutex.unlock();
            }

            // Wait context switching SAVE time
            std::this_thread::sleep_for(std::chrono::milliseconds(shared_data->context_switch));

        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

    }

}


void printProcessOutput(std::vector<Process*>& processes)
{
    printw("|   PID | Priority |    State    | Core |               Progress               |\n"); // 36 chars for prog
    printw("+-------+----------+-------------+------+--------------------------------------+\n");
    for (int i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double total_time = processes[i]->getTotalRunTime();
            double completed_time = total_time - processes[i]->getRemainingTime();
            std::string progress = makeProgressString(completed_time / total_time, 36);
            printw("| %5u | %8u | %11s | %4s | %36s |\n", pid, priority,
                   process_state.c_str(), cpu_core.c_str(), progress.c_str());
        }
    }
    refresh();
}

std::string makeProgressString(double percent, uint32_t width)
{
    uint32_t n_chars = percent * width;
    std::string progress_bar(n_chars, '#');
    progress_bar.resize(width, ' ');
    return progress_bar;
}

uint64_t currentTime()
{
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}
