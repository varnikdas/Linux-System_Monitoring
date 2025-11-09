#include <curses.h>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <csignal>
#include "format.h"
#include "ncurses_display.h"
#include "system.h"

using std::string;
using std::to_string;

// Sorting mode enumeration
enum SortMode {
  SORT_PID,
  SORT_CPU,
  SORT_RAM
};

// Global sort mode
static SortMode currentSortMode = SORT_CPU;
static int selectedProcess = 0;

// 50 bars uniformly displayed from 0 - 100 %
// 2% is one bar(|)
std::string NCursesDisplay::ProgressBar(float percent) {
  std::string result{"0%"};
  int size{50};
  float bars{percent * size};
  for (int i{0}; i < size; ++i) {
    result += i <= bars ? '|' : ' ';
  }
  string display{to_string(percent * 100).substr(0, 4)};
  if (percent < 0.1 || percent == 1.0)
    display = " " + to_string(percent * 100).substr(0, 3);
  return result + " " + display + "/100%";
}

void NCursesDisplay::DisplaySystem(System& system, WINDOW* window) {
  int row{0};
  mvwprintw(window, ++row, 2, ("OS: " + system.OperatingSystem()).c_str());
  mvwprintw(window, ++row, 2, ("Kernel: " + system.Kernel()).c_str());
  mvwprintw(window, ++row, 2, "CPU: ");
  wattron(window, COLOR_PAIR(1));
  mvwprintw(window, row, 10, "");
  wprintw(window, ProgressBar(system.Cpu().Utilization()).c_str());
  wattroff(window, COLOR_PAIR(1));
  mvwprintw(window, ++row, 2, "Memory: ");
  wattron(window, COLOR_PAIR(1));
  mvwprintw(window, row, 10, "");
  wprintw(window, ProgressBar(system.MemoryUtilization()).c_str());
  wattroff(window, COLOR_PAIR(1));
  mvwprintw(window, ++row, 2,
            ("Total Processes: " + to_string(system.TotalProcesses())).c_str());
  mvwprintw(
      window, ++row, 2,
      ("Running Processes: " + to_string(system.RunningProcesses())).c_str());
  mvwprintw(window, ++row, 2,
            ("Up Time: " + Format::ElapsedTime(system.UpTime())).c_str());
  wrefresh(window);
}

// Sort processes based on current sort mode
void SortProcesses(std::vector<Process>& processes) {
  switch(currentSortMode) {
    case SORT_CPU:
      std::sort(processes.begin(), processes.end(), 
                [](Process& a, Process& b) {
                  return a.CpuUtilization() > b.CpuUtilization();
                });
      break;
    case SORT_RAM:
      std::sort(processes.begin(), processes.end(),
                [](Process& a, Process& b) {
                  return std::stof(a.Ram()) > std::stof(b.Ram());
                });
      break;
    case SORT_PID:
      std::sort(processes.begin(), processes.end(),
                [](Process& a, Process& b) {
                  return a.Pid() < b.Pid();
                });
      break;
  }
}

void NCursesDisplay::DisplayProcesses(std::vector<Process> processes,
                                      WINDOW* window, int n) {
  // Sort processes
  SortProcesses(processes);
  
  int row{0};
  int const pid_column{2};
  int const user_column{9};
  int const cpu_column{20};
  int const ram_column{28};
  int const time_column{37};
  int const command_column{48};
  
  wattron(window, COLOR_PAIR(2));
  mvwprintw(window, ++row, pid_column, "PID");
  mvwprintw(window, row, user_column, "USER");
  
  // Highlight current sort column
  if(currentSortMode == SORT_CPU) wattron(window, A_BOLD);
  mvwprintw(window, row, cpu_column, "CPU[%%]");
  if(currentSortMode == SORT_CPU) wattroff(window, A_BOLD);
  
  if(currentSortMode == SORT_RAM) wattron(window, A_BOLD);
  mvwprintw(window, row, ram_column, "RAM[MB]");
  if(currentSortMode == SORT_RAM) wattroff(window, A_BOLD);
  
  mvwprintw(window, row, time_column, "TIME+");
  mvwprintw(window, row, command_column, "COMMAND");
  wattroff(window, COLOR_PAIR(2));
  
  // Display sort mode help with current mode highlighted
  std::string sortInfo = "Sort: ";
  if(currentSortMode == SORT_CPU) {
    sortInfo += ">>CPU<< [M]emory [P]ID";
  } else if(currentSortMode == SORT_RAM) {
    sortInfo += "[C]PU >>MEMORY<< [P]ID";
  } else {
    sortInfo += "[C]PU [M]emory >>PID<<";
  }
  sortInfo += " | [K]ill | [Q]uit | UP/DOWN arrows";
  
  wattron(window, COLOR_PAIR(2));
  mvwprintw(window, 0, 2, sortInfo.c_str());
  wattroff(window, COLOR_PAIR(2));
  
  for (int i = 0; i < n && i < static_cast<int>(processes.size()); ++i) {
    // Highlight selected process
    if(i == selectedProcess) {
      wattron(window, A_REVERSE);
    }
    
    mvwprintw(window, ++row, pid_column, to_string(processes[i].Pid()).c_str());
    mvwprintw(window, row, user_column, processes[i].User().c_str());
    float cpu = processes[i].CpuUtilization() * 100;
    mvwprintw(window, row, cpu_column, to_string(cpu).substr(0, 4).c_str());
    mvwprintw(window, row, ram_column, processes[i].Ram().c_str());
    mvwprintw(window, row, time_column,
              Format::ElapsedTime(processes[i].UpTime()).c_str());
    mvwprintw(window, row, command_column,
              processes[i].Command().substr(0, getmaxx(window) - 46).c_str());
    
    if(i == selectedProcess) {
      wattroff(window, A_REVERSE);
    }
  }
}

bool KillProcess(int pid) {
  if(kill(pid, SIGTERM) == 0) {
    return true;
  }
  // If SIGTERM fails, try SIGKILL
  if(kill(pid, SIGKILL) == 0) {
    return true;
  }
  return false;
}

void NCursesDisplay::Display(System& system, int n) {
  initscr();      // start ncurses
  noecho();       // do not print input values
  cbreak();       // terminate ncurses on ctrl + c
  start_color();  // enable color
  nodelay(stdscr, TRUE); // Non-blocking input
  keypad(stdscr, TRUE);  // Enable arrow keys
  
  int x_max{getmaxx(stdscr)};
  WINDOW* system_window = newwin(9, x_max - 1, 0, 0);
  WINDOW* process_window =
      newwin(3 + n, x_max - 1, getmaxy(system_window) + 1, 0);
  
  // Message window for feedback
  WINDOW* message_window = newwin(3, x_max - 1, getmaxy(system_window) + getmaxy(process_window) + 2, 0);
  
  std::string message = "";
  auto messageTime = std::chrono::steady_clock::now();
  
  while (1) {
    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);
    
    box(system_window, 0, 0);
    box(process_window, 0, 0);
    
    DisplaySystem(system, system_window);
    auto processes = system.Processes().GetProcesses();
    DisplayProcesses(processes, process_window, n);
    
    // Display message if recent
    if(!message.empty()) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - messageTime).count();
      if(elapsed < 3) {
        box(message_window, 0, 0);
        mvwprintw(message_window, 1, 2, message.c_str());
        wrefresh(message_window);
      } else {
        wclear(message_window);
        wrefresh(message_window);
        message = "";
      }
    }
    
    wrefresh(system_window);
    wrefresh(process_window);
    refresh();
    
    // Handle keyboard input
    int ch = getch();
    if(ch != ERR) {
      switch(ch) {
        case 'q':
        case 'Q':
          endwin();
          return;
        case 'c':
        case 'C':
          currentSortMode = SORT_CPU;
          selectedProcess = 0;
          message = "Sorting by CPU Usage";
          messageTime = std::chrono::steady_clock::now();
          break;
        case 'm':
        case 'M':
          currentSortMode = SORT_RAM;
          selectedProcess = 0;
          message = "Sorting by Memory/RAM Usage";
          messageTime = std::chrono::steady_clock::now();
          break;
        case 'p':
        case 'P':
          currentSortMode = SORT_PID;
          selectedProcess = 0;
          message = "Sorting by Process ID (PID)";
          messageTime = std::chrono::steady_clock::now();
          break;
        case KEY_UP:
          if(selectedProcess > 0) selectedProcess--;
          break;
        case KEY_DOWN:
          if(selectedProcess < n - 1 && selectedProcess < static_cast<int>(processes.size()) - 1) 
            selectedProcess++;
          break;
        case 'k':
        case 'K':
          if(selectedProcess < static_cast<int>(processes.size())) {
            int pid = processes[selectedProcess].Pid();
            if(KillProcess(pid)) {
              message = "Process " + to_string(pid) + " killed successfully!";
            } else {
              message = "Failed to kill process " + to_string(pid) + ". Try with sudo.";
            }
            messageTime = std::chrono::steady_clock::now();
          }
          break;
      }
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  endwin();
}